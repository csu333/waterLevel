/*********
  Jean-Pierre Cadiat

  Credits:
   * Power reduction by OppoverBakke: https://www.bakke.online/index.php/2017/06/24/esp8266-wifi-power-reduction-avoiding-network-scan/
   * Battery voltage from zenz: https://github.com/LilyGO/LILYGO-T-OI/issues/12
   * Distance measurement by Probots: https://tutorials.probots.co.in/communicating-with-a-waterproof-ultrasonic-sensor-aj-sr04m-jsn-sr04t/
*********/
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include "BufferPrint.h"
#include "PBPrint.h"
#include "config.h"

#define MSG_BUFFER_SIZE  (50)
char msg[MSG_BUFFER_SIZE];

const int analogInPin = A0;  // ESP8266 Analog Pin ADC0 = A0

#define rxPin 13
#define txPin 15

/****************************************************************************************************************/

/*************\
 * Constants *
\*************/

// Sensor config
#define MIN_LEVEL 4
#define MAX_LEVEL 8
#define SLEEP_TIME 16
#define MAX_DIFFERENCE 32
#define EEPROM_SIZE 36

const String LOG_TOPIC = ROOT_TOPIC + "/log";

bool removeConfigMsg = false;

/****************************************************************************************************************/

/*************\
 * Structure *
\*************/

// See https://www.bakke.online/index.php/2017/06/24/esp8266-wifi-power-reduction-avoiding-network-scan/
// The ESP8266 RTC memory is arranged into blocks of 4 bytes. The access methods read and write 4 bytes at a time,
// so the RTC data structure should be padded to a 4-byte multiple.
struct {
  uint32_t crc32;               // 4 bytes
  uint8_t  channel;             // 1 byte,    5 in total
  uint8_t  bssid[6];            // 6 bytes,   11 in total
  uint8_t  failedConnection;    // 1 byte,    12 in total
  uint8_t  batteryAlertSent;    // 1 byte,    13 in total
  uint8_t  waterLevelAlertSent; // 1 byte,    14 in total
  uint8_t  logLevel;            // 1 byte,    15 in total
  uint16_t lastMeasure;         // 2 bytes,   17 in total
  uint16_t bufferPosition;      // 2 bytes,   19 in total
  uint8_t  logBuffer[400];      // 400 bytes, 419 in total
} rtcData;

/****************************************************************************************************************/

/**********\
 * Global *
\**********/

// Initializes the espClient. You should change the espClient name if you have multiple ESPs running in your home automation system
WiFiClient espClient;
PubSubClient client(espClient);
PBPrint mqttLog = PBPrint(&client, "");

float batteryLevel = 0;  // value read from A0
uint32_t sleepTime;
int minLevel;
int maxLevel;
uint8_t maxDifference;
bool batteryAlertSent = false;
bool waterLevelAlertSent = false;

bool rtcLoaded = false;
bool rtcValid = false;
bool rtcDirty = false;

/****************************************************************************************************************/

/********\
 * WIFI *
\********/

void initWiFi() {
  // Disable persistence in flash for power savings
  WiFi.persistent(false);
  
  WiFi.forceSleepWake();
  delay( 1 );
  
  loadRtc();

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  if( rtcValid && rtcData.failedConnection < 4) {
    // The RTC data was good, make a quick connection
    WiFi.begin( WLAN_SSID, WLAN_PASSWD, rtcData.channel, rtcData.bssid, true );
  }
  else {
    // The RTC data was not valid, so make a regular connection
    WiFi.begin( WLAN_SSID, WLAN_PASSWD );
  }
  WiFi.config(staticIP, gateway, subnet);
  Log.noticeln("Connecting to WiFi");
  
  // Wait for successful connection
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (++retries % 20 == 0) {
      Serial.print('.');
    }

    if (retries == 100 && rtcValid) {
      // Quick connect is not working, reset WiFi and try regular connection
      Log.warningln(F("Fast connect failed"));
      Log.noticeln(F("Channel: %d"), rtcData.channel); 
      Log.noticeln(F("BSSID: %x:%x:%x:%x:%x:%x"), rtcData.bssid[0], rtcData.bssid[1], rtcData.bssid[2], rtcData.bssid[3], rtcData.bssid[4], rtcData.bssid[5]);
      WiFi.disconnect();
      delay( 10 );
      WiFi.forceSleepBegin();
      delay( 10 );
      WiFi.forceSleepWake();
      delay( 10 );
      WiFi.begin( WLAN_SSID, WLAN_PASSWD );
    }
    
    if (retries == 600) {
      Log.errorln(F("something happened, trying to reset"));
      // Giving up after 30 seconds and going back to sleep
      WiFi.disconnect( true );
      delay( 1 );
      WiFi.mode( WIFI_OFF );
      
      rtcData.failedConnection++;
      rtcDirty = true;
      saveRtc();
      
      ESP.deepSleepInstant(sleepTime, WAKE_RF_DISABLED);
    }
    
    delay(50);
  }
  Serial.println("");
  Log.noticeln(F("RSSI: %d dB"), WiFi.RSSI()); 

  // Write current connection info back to RTC
  rtcData.channel = WiFi.channel();
  rtcData.failedConnection = 0;
  memcpy( rtcData.bssid, WiFi.BSSID(), 6 ); // Copy 6 bytes of BSSID (AP's MAC address)
  rtcDirty = true;
}

/****************************************************************************************************************/

/********\
 * MQTT *
\********/

void reconnect() {
  // Init MQTT
  client.setServer(MQTT_SERVER, 1883);
  
  // Loop until we're reconnected
  while (!client.connected()) {
    Log.noticeln(F("Connecting to MQTT"));

    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      // ... and resubscribe
      client.setCallback(callback);
      client.subscribe((ROOT_TOPIC + "/config").c_str());
      delay(100);
    } else {
      Log.errorln(F("Failed, rc=%d try again in 5 seconds"), client.state());
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Stop sending log to MQTT to avoid deadlocks
  mqttLog.setSuspend(true);
  
  /* Process the configuration command */

  // Allocate the JSON document
  const size_t capacity = JSON_OBJECT_SIZE(10);
  DynamicJsonDocument doc(capacity);

  // Parse JSON object
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Log.errorln(F("deserializeJson() failed: %s"), error.c_str());
    return;
  }

  JsonObject conf = doc.as<JsonObject>();

  //Init EEPROM
  EEPROM.begin(EEPROM_SIZE);
  bool commit = false;

  Log.traceln(F("Number of keypairs: %d"), conf.size());

  // Loop through all the key-value pairs in obj
  for (JsonPair p : conf) {
    
    const char* key = p.key().c_str();
    Log.traceln("Processing: { \"%s\": %s }", p.key().c_str(), p.value().as<String>());
    
    if (strcmp(key, "minLevel") == 0) {
      if (isnan(minLevel) || minLevel != (int)p.value()){
        minLevel = (int)p.value();
        EEPROM.put(MIN_LEVEL, minLevel);
        commit = true;
        
        Log.noticeln(F("New minimum level set: %d"), minLevel);
      } else {
        Log.verboseln(F("Value unchanged. Ignoring"));
      }
    } else if (strcmp(key, "maxLevel") == 0) {
      if (isnan(maxLevel) || maxLevel != (int)p.value()){
        maxLevel = (int)p.value();
        if (maxLevel < CLOSEST) {
          maxLevel = CLOSEST;
          Log.warningln(F("Max level in blind zone. Setting to %d mm"), CLOSEST);
        }
        EEPROM.put(MAX_LEVEL, maxLevel);
        commit = true;
        
        Log.noticeln(F("New maximum level set: %d"), maxLevel);
      } else {
        Log.verboseln(F("Value unchanged. Ignoring"));
      }
    } else if (strcmp(key, "sleepTime") == 0) {
      if (isnan(sleepTime) || sleepTime / 1e6 != p.value()){
        sleepTime = p.value();
        
        if (sleepTime * 1e6 > ESP.deepSleepMax()) {
          sleepTime = ESP.deepSleepMax();
        } else {
          sleepTime *= 1e6;
        }
        
        EEPROM.put(SLEEP_TIME, sleepTime);
        commit = true;
        
        Log.noticeln(F("New sleep time set: %D s"), (sleepTime / 1e6));
      } else {
        Log.verboseln(F("Value unchanged. Ignoring"));
      }
    } else if (strcmp(key, "maxDifference") == 0) {
      if (maxDifference != p.value()){
        if (p.value() <= 0 || p.value() > 100) {
          Log.warningln("Incorrect max difference value. Must be between 0 and 100");
          continue;
        }

        if (p.value() < 1) {
          maxDifference = (float)p.value() * 100;
        } else {
          maxDifference = p.value();
        }
        
        EEPROM.put(MAX_DIFFERENCE, maxDifference);
        commit = true;
        
        Log.noticeln(F("New max difference set: %d"), maxDifference);
      } else {
        Log.verboseln(F("Value unchanged. Ignoring"));
      }
    } else if (strcmp(key, "logLevel") == 0) {
      if (rtcData.logLevel != p.value()){
        rtcData.logLevel = p.value();
        
        if (rtcData.logLevel > LOG_LEVEL_VERBOSE) {
          rtcData.logLevel = LOG_LEVEL_VERBOSE;
        }
        rtcDirty = true;
        Log.setLevel(rtcData.logLevel);
        
        Log.noticeln(F("New log level set: %d"), rtcData.logLevel);
      } else {
        Log.verboseln(F("Value unchanged. Ignoring"));
      }
    } else {
      Log.warningln(F("Unknown config parameter: %s"), p.key().c_str());
    }
  }

  if (commit) {
    bool commited = EEPROM.commit();
    Log.noticeln(F("New config committed: %T"), commited);
  }

  if (minLevel < maxLevel) {
    Log.warningln("Minimum level should be bigger than maximum level because the water is further away from the sensor when the level is at its minimum.");
  }

  removeConfigMsg = true;
}

/****************************************************************************************************************/

uint32_t calculateCRC32( const uint8_t *data, size_t length ) {
  uint32_t crc = 0xffffffff;
  while( length-- ) {
    uint8_t c = *data++;
    for( uint32_t i = 0x80; i > 0; i >>= 1 ) {
      bool bit = crc & 0x80000000;
      if( c & i ) {
        bit = !bit;
      }

      crc <<= 1;
      if( bit ) {
        crc ^= 0x04c11db7;
      }
    }
  }

  return crc;
}

bool loadRtc() {
  if (!rtcLoaded) {
    // Try to read WiFi settings from RTC memory
    if( ESP.rtcUserMemoryRead( 0, (uint32_t*)&rtcData, sizeof( rtcData ) ) ) {
      rtcLoaded = true;
      // Calculate the CRC of what we just read from RTC memory, but skip the first 4 bytes as that's the checksum itself.
      uint32_t crc = calculateCRC32( ((uint8_t*)&rtcData) + 4, sizeof( rtcData ) - 4 );
      if( crc == rtcData.crc32 ) {
        rtcValid = true;

        // Load values from RTC memory
        batteryAlertSent = rtcData.batteryAlertSent;
        waterLevelAlertSent = rtcData.waterLevelAlertSent;

        // Restore logging buffer
        mqttLog.pos = rtcData.bufferPosition;
        memcpy(mqttLog._buffer, rtcData.logBuffer, min(sizeof(rtcData.logBuffer), (unsigned int)mqttLog.pos));
        
        Log.noticeln("RTC load success");
      } else {
        Log.errorln("Unable to reload values from RTC: CRC failed");
      }
    } else {
        Log.errorln("Unable to reload values from RTC");
    }
  }
  return rtcValid;
}

void saveRtc() {
  // Save logging buffer
  if (mqttLog.pos != rtcData.bufferPosition) {
    rtcData.bufferPosition = min(sizeof(rtcData.logBuffer), (unsigned int)mqttLog.pos);
    memcpy(rtcData.logBuffer, mqttLog._buffer, rtcData.bufferPosition);
    rtcDirty = true;
  }
  
  if (rtcDirty) {
    rtcData.crc32 = calculateCRC32( ((uint8_t*)&rtcData) + 4, sizeof( rtcData ) - 4 );
    ESP.rtcUserMemoryWrite( 0, (uint32_t*)&rtcData, sizeof( rtcData ) );
  }
}

float getVoltage() {
  float floatVoltage = 2 * (3.3 * analogRead(PIN_A0)) / 1023; 
  return floatVoltage;
}

int getWaterLevel() {
  unsigned int distance = -1;
  byte startByte, h_data, l_data, sum = 0;
  byte buf[4];
  Serial.flush();
  Serial.swap();

  // Requesting reading
  Serial.write(0x01);
  // Wait for the reply to be ready
  delay((FARTHEST / SPEED_OF_SOUND) + 1);

  Serial.readBytes(buf, 4);
  startByte = buf[0];
  if(startByte == 255){
    Serial.swap();
    h_data = buf[1];
    l_data = buf[2];
    sum = buf[3];
    distance = (h_data<<8) + l_data;
    
    if((( h_data + l_data)&0xFF) != sum){
      Log.errorln(F("Invalid result: %d %d (%d)"), buf[1], buf[2], buf[3]);
    }
  }

  delay(1);
  Log.verboseln(F("Remaining in buffer: %d"), Serial.available());
  
  return distance;
}

/****************************************************************************************************************/

void setup() {

  /***********************************
   *     Initialisation
   */
  // Keep Wifi disabled at first 
  WiFi.mode( WIFI_OFF );
  WiFi.forceSleepBegin();
  delay( 1 );
  
  // initialize serial communication at 9600
  // It is mandatory to be able to read data comming from the ultrasound sensor
  Serial.begin(9600);
  
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  mqttLog = PBPrint(&client, LOG_TOPIC.c_str());
  mqttLog.setSuspend(true);
  
  if (!loadRtc()) {
    // Set default values on cold start
    rtcData.logLevel = LOG_LEVEL_NOTICE;
    rtcDirty = true;
  }

  BufferPrint *bp = new BufferPrint();
  bp->addOutput(&Serial);
  Log.begin(rtcData.logLevel, bp);
  // Comment next line if you don’t want logging by MQTT
  bp->addOutput(&mqttLog);
  
  Log.noticeln(F("Logging ready"));

  // Read config from EEPROM
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(MIN_LEVEL, minLevel);
  EEPROM.get(MAX_LEVEL, maxLevel);
  EEPROM.get(SLEEP_TIME, sleepTime);
  EEPROM.get(MAX_DIFFERENCE, maxDifference);

  // Checking the recorded value (should only be useful on the first start)
  if (isnan(sleepTime) || sleepTime == 0 || sleepTime > ESP.deepSleepMax()) {
    sleepTime = DEFAULT_SLEEP_TIME;
    Log.warningln(F("Set default sleep time"));
  }
  Log.noticeln(F("Sleep time %D s"), (sleepTime / 1e6));

  Log.noticeln(F("Levels: %d - %d"), minLevel, maxLevel);

  if (isnan(maxDifference) || maxDifference == 0 || maxDifference > 100) {
    Log.warningln(F("Set default max difference"));
    maxDifference = DEFAULT_MAX_DIFFERENCE;
    EEPROM.put(MAX_DIFFERENCE, maxDifference);
    bool committed = EEPROM.commit();
    if (!committed) {
      Log.errorln("Failed to commit new value");
    }
  }

  /***********************************
   *     Measures
   */
 
  // Read the battery level
  float batteryLevel = getVoltage();
  Log.verboseln(F("Battery voltage = %F V"), batteryLevel);

  // sends alert if battery low
  if (batteryLevel <= BATTERY_ALERT_THRESHOLD) {
    Log.warningln("Battery low!");
    client.publish((ROOT_TOPIC + "/alert").c_str(), "Battery low");
    rtcData.batteryAlertSent = true;
    rtcDirty = true;
  } else if (batteryAlertSent && batteryLevel > BATTERY_ALERT_REARM) {
    // Clear alert
    rtcData.batteryAlertSent = false;
    rtcDirty = true;
    client.publish((ROOT_TOPIC + "/alert").c_str(), new byte[0], 0, true);
  }  
   
  int waterLevel = getWaterLevel();
  Log.noticeln("Distance: %d mm", waterLevel);

  // Check that returned value makes sense
  if (waterLevel < CLOSEST) {
    Log.warningln("Distance too short for the sensor. Trying again.");
    waterLevel = getWaterLevel();
  }

  int i = 0;
  while (abs(waterLevel - rtcData.lastMeasure) * 100 / waterLevel > 10 && i < 10) {
    Log.warningln("There is more than 10%% difference with last measure. Trying again");
    rtcData.lastMeasure = waterLevel;
    waterLevel = getWaterLevel();
    i++;
  }
  
  // Check that configuration values are correct
  if (waterLevel > minLevel) {
    Log.warningln("Measured water level is lower than configured level. Adapting setting to %d.", waterLevel);
    // minimum level is lower than expected
    EEPROM.put(MIN_LEVEL, waterLevel);
    bool committed = EEPROM.commit();
    if (!committed) {
      Log.errorln("Failed to commit new value");
    }
  } 
  
  if (waterLevel < maxLevel && waterLevel > CLOSEST) {
    Log.warningln("Measured water level is higher than configured level. Adapting setting to %d.", waterLevel);
    // maximum level is higher than expected
    EEPROM.put(MAX_LEVEL, waterLevel);
    bool committed = EEPROM.commit();
    if (!committed) {
      Log.errorln("Failed to commit new value");
    }
  }

  if (minLevel > FARTHEST) {
    Log.warningln(F("Min level too far. Setting to %d mm"), FARTHEST);
    EEPROM.put(MIN_LEVEL, FARTHEST);
    bool committed = EEPROM.commit();
    if (!committed) {
      Log.errorln("Failed to commit new value");
    }
  }

  // Making sure that both levels are different
  if (minLevel == maxLevel) {
    minLevel = maxLevel+1;
  }

  /***********************************
   *     Reporting
   */

  // Init WiFi (as late as possible to save power)
  initWiFi();

  // Connect to MQTT
  if (!client.connected()) {
    reconnect();
  }
  
  mqttLog.setSuspend(false);
  client.loop();

  if (waterLevel > CLOSEST && waterLevel < FARTHEST) {
    // compute percentage of filled volume
    float filledLevel = (minLevel - waterLevel * 1.0) / (minLevel - maxLevel) * 100;
    client.publish((ROOT_TOPIC + "/level").c_str(), (String(waterLevel)).c_str());
    client.publish((ROOT_TOPIC + "/levelPercentage").c_str(), (String(filledLevel)).c_str());
  } else {
    Log.warningln(F("Not reporting the measurement as it si invalid"));
  }

  // Reporting voltage
  client.publish((ROOT_TOPIC + "/voltage").c_str(), (String(batteryLevel)).c_str());
  client.loop();

  // Make sure that buffered messages got sent
  mqttLog.setSuspend(false);
  delay(50);
  client.loop();

  // Empty Wifi reception buffer
  while (espClient.available()) {
    mqttLog.setSuspend(false);
    delay(10);
    client.loop();
  }

  if (removeConfigMsg) {
    // This config message is intended for me only so I can delete it
    Log.noticeln(F("Config message processed"));
    client.publish((ROOT_TOPIC + "/config").c_str(), new byte[0], 0, true);
    client.loop();
    Log.traceln("Message removed from topic");
  }

  // Preparing for sleep
  client.unsubscribe((ROOT_TOPIC + "/config").c_str());
  client.disconnect();
  delay(50);
 
  WiFi.disconnect( true );
  delay(1);
  Serial.println("Disconnected");
  saveRtc();
  EEPROM.end();
  ESP.deepSleepInstant(sleepTime, WAKE_RF_DISABLED);

  Serial.println(F("What... I'm not asleep?!?"));  // it will never get here
  delay(5000);
}

void loop() {

 }

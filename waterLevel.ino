/*********
  Jean-Pierre Cadiat

  Credits:
   * Power reduction by OppoverBakke: https://www.bakke.online/index.php/2017/06/24/esp8266-wifi-power-reduction-avoiding-network-scan/
   * Battery voltage from zenz: https://github.com/LilyGO/LILYGO-T-OI/issues/12
   * Distance measurement by Probots: https://tutorials.probots.co.in/communicating-with-a-waterproof-ultrasonic-sensor-aj-sr04m-jsn-sr04t/
*********/
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <SoftwareSerial.h>
#include "BufferPrint.h"
#include "PBPrint.h"
#include "config.h"

#define MSG_BUFFER_SIZE  (50)
char msg[MSG_BUFFER_SIZE];

const int analogInPin = A0;  // ESP8266 Analog Pin ADC0 = A0

#define rxPin0 0
#define txPin0 2
#define gndPin0 12

/****************************************************************************************************************/

/*************\
 * Constants *
\*************/

// Sensor config
#define MIN_LEVEL 0
#define MAX_LEVEL       MIN_LEVEL + PROBE_COUNT * 4
#define SLEEP_TIME      MAX_LEVEL + PROBE_COUNT * 4
#define MAX_DIFFERENCE  SLEEP_TIME + 4
#define EEPROM_SIZE     MAX_DIFFERENCE + 4

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
  uint32_t crc32;                             // 4 bytes
  uint8_t  channel;                           // 1 byte,    5 in total
  uint8_t  bssid[6];                          // 6 bytes,   11 in total
  uint8_t  failedConnection;                  // 1 byte,    12 in total
  uint8_t  batteryAlertSent;                  // 1 byte,    13 in total
  uint8_t  waterLevelAlertSent;               // 1 byte,    14 in total
  uint8_t  logLevel;                          // 1 byte,    15 in total
  uint16_t lastMeasure[PROBE_COUNT];          // 2*PROBE_COUNT bytes
  uint16_t bufferPosition;                    // 2 bytes
  uint8_t  logBuffer[400-2*PROBE_COUNT];      // 400 bytes
} rtcData;

/****************************************************************************************************************/

/**********\
 * Global *
\**********/

// Initializes the espClient. You should change the espClient name if you have multiple ESPs running in your home automation system
WiFiClient espClient;
PubSubClient client(espClient);
PBPrint mqttLog = PBPrint(&client, "");

SoftwareSerial swSer;

float batteryLevel = 0;  // value read from A0
uint32_t sleepTime;
int minLevel[PROBE_COUNT];
int maxLevel[PROBE_COUNT];
uint8_t maxDifference;
bool batteryAlertSent = false;
bool waterLevelAlertSent = false;

long wifiStart;

bool rtcLoaded = false;
bool rtcValid = false;
bool rtcDirty = false;

bool commit = false;

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
    Log.traceln(F("Using fast WiFi connect"));
    WiFi.begin(WLAN_SSID, WLAN_PASSWD, rtcData.channel, rtcData.bssid, true);
  }
  else {
    // The RTC data was not valid, so make a regular connection
    Log.warningln(F("Number of connection failure too high (%d). Using regular connection instead"), rtcData.failedConnection);
    WiFi.begin( WLAN_SSID, WLAN_PASSWD );
  }

  wifiStart = millis();

  WiFi.config(staticIP, gateway, subnet);
  Log.noticeln("Waiting WiFi connection");

  // Wait for successful connection
  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - wifiStart) >= 10000 && rtcValid) {
      // Quick connect is not working, reset WiFi and try regular connection
      Log.warningln(F("Fast connect failed"));
      Log.noticeln(F("Channel: %d"), rtcData.channel); 
      Log.noticeln(F("BSSID: %x:%x:%x:%x:%x:%x"), rtcData.bssid[0], rtcData.bssid[1], rtcData.bssid[2], rtcData.bssid[3], rtcData.bssid[4], rtcData.bssid[5]);
      WiFi.disconnect( true );
      delay( 50 );
      WiFi.forceSleepBegin();
      delay( 100 );
      WiFi.forceSleepWake();
      delay( 50 );
      WiFi.begin( WLAN_SSID, WLAN_PASSWD );
    }
    
    if ((millis() - wifiStart) >= 20000) {
      Log.errorln(F("Something happened, giving up"));
      // Giving up after 30 seconds and going back to sleep
      WiFi.disconnect( true );
      delay( 1 );
      WiFi.mode( WIFI_OFF );
      
      rtcData.failedConnection++;
      rtcDirty = true;
      saveRtc();
      
      EEPROM.end();
      ESP.deepSleepInstant(sleepTime, WAKE_RF_DISABLED);
    }
    
    delay(50);
  }

  Log.noticeln(F("RSSI: %d dB"), WiFi.RSSI()); 

  if (rtcData.failedConnection > 0) {
    Log.warningln(F("Wifi connected after %d failures"), rtcData.failedConnection);
  }

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

bool reconnect() {
  // Init MQTT
  client.setServer(MQTT_SERVER, 1883);
  int i = 4;
  
  // Loop until we're reconnected
  while (!client.connected() && i > 0) {
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
    i--;
  }

  if (i <= 0) {
    Log.warningln(F("Failed to send MQTT message"));
    return false;
  }

  return true;
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

  Log.traceln(F("Number of keypairs: %d"), conf.size());

  // Loop through all the key-value pairs in obj
  for (JsonPair p : conf) {
    
    const char* key = p.key().c_str();
    char cKey[20];
    strncpy(cKey, key, sizeof(cKey) - 1);
    String sKey = String(cKey);
    String value = p.value().as<String>();
    Log.traceln(F("Processing: { \"%s\": %s }"), sKey, value);
    
    // Check for indexed keys
    int len = strlen(key);
    byte index = 0;
    if (len > 3 && key[len-3] == '[' && isDigit(key[len-2]) && key[len-1] == ']'){
      index = (byte)key[len-2] - '0';
      cKey[len-3] = 0;

      if (index >= PROBE_COUNT){
        Log.error(F("Configuration impossible as index (%d) is higher than the number of probes supported when compiling (%d)."), index, PROBE_COUNT);
        continue;
      }

      Log.traceln(F("Indexed key found: %s[%d]"), cKey, index);
    }

    // Process setting
    if (strcmp(cKey, "minLevel") == 0) {
      if (isnan(minLevel[index]) || minLevel[index] != (int)p.value()){
        minLevel[index] = (int)p.value();
        if (minLevel[index] > FARTHEST) {
          minLevel[index] = FARTHEST;
          Log.warningln(F("Min level in beyond max range. Setting probe %d to %d mm"), index, FARTHEST);
        }
        EEPROM.put(MIN_LEVEL+index*sizeof(minLevel[0]), minLevel[index]);
        commit = true;
        
        Log.noticeln(F("New minimum level set for probe %d: %d"), index, minLevel[index]);
      } else {
        Log.verboseln(F("Value unchanged. Ignoring"));
      }
    } else if (strcmp(cKey, "maxLevel") == 0) {
      if (isnan(maxLevel[index]) || maxLevel[index] != (int)p.value()){
        maxLevel[index] = (int)p.value();
        if (maxLevel[index] < CLOSEST) {
          maxLevel[index] = CLOSEST;
          Log.warningln(F("Max level in blind zone. Setting probe %d to %d mm"), index, CLOSEST);
        }
        EEPROM.put(MAX_LEVEL+index*sizeof(maxLevel[0]), maxLevel[index]);
        commit = true;
        
        Log.noticeln(F("New maximum level set for probe %d: %d"), index, maxLevel[index]);
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
        if (p.value() <= 0 || p.value() > maxLevel[0]) {
          Log.warningln("Incorrect max difference value. Must be between 0 and maxLevel[0] (%D)", maxLevel[0]);
          continue;
        }
        
        maxDifference = p.value();
        
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

  if (minLevel[0] < maxLevel[0]) {
    Log.warningln("Minimum level 0 should be bigger than maximum level because the water is further away from the sensor when the level is at its minimum.");
  }
  if (minLevel[1] < maxLevel[1]) {
    Log.warningln("Minimum level 1 should be bigger than maximum level because the water is further away from the sensor when the level is at its minimum.");
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
  if (rtcLoaded) {
    return true;
  }
  
  // Try to read WiFi settings from RTC memory
  bool readSucceed = ESP.rtcUserMemoryRead( 0, (uint32_t*)&rtcData, sizeof( rtcData ) );
  rtcLoaded = true;

  if (!readSucceed) {
    rtcData = { 0, 0, 0, 128, 0, 0, 3, 0, 0, {} };
    Log.errorln("Unable to read values from RTC");
    return false;
  }
    
  // Calculate the CRC of what we just read from RTC memory, but skip the first 4 bytes as that's the checksum itself.
  uint32_t crc = calculateCRC32( ((uint8_t*)&rtcData) + 4, sizeof( rtcData ) - 4 );
  if( crc != rtcData.crc32 ) {
    rtcData = { 0, 0, 0, 128, 0, 0, 3, 0, 0, {} };
    Log.errorln("Unable to reload values from RTC: CRC failed");
    return false;
  }

  rtcValid = true;

  // Load values from RTC memory
  batteryAlertSent = rtcData.batteryAlertSent;
  waterLevelAlertSent = rtcData.waterLevelAlertSent;

  // Restore logging buffer
  mqttLog.pos = rtcData.bufferPosition;
  memcpy(mqttLog._buffer, rtcData.logBuffer, min(sizeof(rtcData.logBuffer), (unsigned int)mqttLog.pos));
  
  Log.noticeln("RTC load success");

  return true;
}

void saveRtc() {
  // Save logging buffer
  if (mqttLog.pos != rtcData.bufferPosition) {
    Log.traceln(F("Saving logs"));
    rtcData.bufferPosition = min(sizeof(rtcData.logBuffer), (unsigned int)mqttLog.pos);
    memcpy(rtcData.logBuffer, mqttLog._buffer, rtcData.bufferPosition);
    rtcDirty = true;
  }
  
  if (rtcDirty) {
    Log.traceln(F("Saving data in RTC memory"));
    rtcData.crc32 = calculateCRC32( ((uint8_t*)&rtcData) + 4, sizeof( rtcData ) - 4 );
    ESP.rtcUserMemoryWrite( 0, (uint32_t*)&rtcData, sizeof( rtcData ) );
  }
}

float getVoltage() {
  float floatVoltage = 2 * (3.3 * analogRead(PIN_A0)) / 1023; 
  return floatVoltage;
}

int getWaterReading(Stream* serial, byte index) {
  long distance = -1;
  byte startByte, h_data, m_data, l_data, sum;
  byte buf[] = { 0, 0, 0, 0 };

  Log.noticeln(F("getWaterReading"));

  // Swap Serial from USB to RX/TX pins
  if (serial == &Serial) {
    Log.traceln(F("Trying to use Serial. Swapping to pins 13/15"));
    Serial.flush();
    Serial.swap();
  }

  // Empty buffer
  while (serial->available() > 0) {
    int inByte = serial->read();
  }

  // Requesting reading
  serial->write(0x01);
  // Wait for the reply to be ready
  delay((FARTHEST / SPEED_OF_SOUND) + 1);

  serial->readBytes(buf, 4);
  startByte = buf[0];
  
  // Swap Serial from RX/TX pins to USB
  if (serial == &Serial) {
    Serial.swap();
    Log.verboseln(F("Swapping back to TX/RX pins"));
  }

  if(startByte != 255){
    Log.errorln(F("Invalid header: %d "), startByte);
    return -1;
  }

  h_data = buf[1];
  l_data = buf[2];
  sum = buf[3];
  distance = (h_data<<8) + l_data;
    
  if((( h_data + l_data)&0xFF) != sum){
    Log.errorln(F("Invalid result: %d %d (%d)"), buf[1], buf[2], buf[3]);
  }

  delay(1);
  Log.verboseln(F("Remaining in buffer: %d"), serial->available());

  if (distance < CLOSEST || distance > FARTHEST) {
    Log.errorln(F("Reading beyond bounds (%d - %d): %d"), CLOSEST, FARTHEST, distance);
    return -1;
  }
  
  return distance;
}

int getWaterLevel(Stream* serial, byte index) {

  int distance = getWaterReading(serial, index);

  if (distance < 0) {
    Log.errorln(F("Error reading water level %d"), index);
    return -1;
  }

  Log.noticeln("Distance %d: %d mm", index, distance);

  // Check that returned value makes sense
  if (distance < CLOSEST) {
    Log.warningln("Distance too short for the sensor. Trying again.");
    distance = getWaterReading(serial, index);
  }

  int i = 0;
  // Check reading plausibility
  while (abs(distance - rtcData.lastMeasure[index]) > maxDifference && i < 2) {
    Log.warningln("There is more than %dmm difference with last measure (was %dmm compared to %dmm now). Trying again after 5s", maxDifference, rtcData.lastMeasure[index], distance);
    delay(5000);
    rtcData.lastMeasure[index] = (uint16_t)distance;
    distance = getWaterReading(serial, index);

    if (distance < 0) {
      Log.errorln(F("Error reading water level %d"), index);
      return -1;
    }

    Log.noticeln("Distance %d: %d mm", index, distance);

    i++;
  }

  if (abs(distance - rtcData.lastMeasure[index]) > maxDifference) {
    Log.warningln(F("Distance not stabilising. Giving up"));
    return -1;
  }

  // Check that configuration values are correct
  if (distance > minLevel[index]) {
    Log.warningln("Measured water level is lower than configured level. Adapting setting probe %d to %d.", index, distance);
    // minimum level is lower than expected
    minLevel[index] = distance;
    EEPROM.put(MIN_LEVEL+index*sizeof(minLevel[0]), distance);
    commit = true;
  }

  if (minLevel[index] > FARTHEST) {
    Log.warningln(F("Min level too far. Setting probe %d to %d mm"), index, FARTHEST);
    EEPROM.put(MIN_LEVEL+index*sizeof(minLevel[0]), FARTHEST);
    commit = true;
  }
  
  if (minLevel[index] == maxLevel[index]) {
    minLevel[index] = maxLevel[index] + 1;
    Log.warningln(F("Min and max levels are the sameon probe %d. Min set to %d mm"), index, minLevel[index]);
    EEPROM.put(MIN_LEVEL+index*sizeof(minLevel[0]), minLevel[index]);
    commit = true;
  }
  
  return distance;
}

void printPrefix(Print* _logOutput, int logLevel) {
    printTimestamp(_logOutput);
    //printLogLevel (_logOutput, logLevel);
}

void printTimestamp(Print* _logOutput) {

  // Division constants
  const unsigned long MSECS_PER_SEC       = 1000;

  // Total time
  const unsigned long msecs               =  millis();
  const unsigned long secs                =  msecs / MSECS_PER_SEC;

  // Time in components
  const unsigned int MilliSeconds        =  msecs % MSECS_PER_SEC;
  const unsigned int Seconds             =  secs  % 100 ;

  // Time as string
  char timestamp[16];
  sprintf(timestamp, "%d-%02d.%03d ", rtcData.failedConnection, Seconds, MilliSeconds);
  _logOutput->print(timestamp);
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

  // Init pin for second probe
  pinMode(rxPin0, INPUT);
  pinMode(txPin0, OUTPUT);

  Log.setPrefix(printPrefix); // set prefix similar to NLog
  
  // initialize serial communication at 9600
  // It is mandatory to be able to read data comming from the ultrasound sensor
  Serial.begin(9600);
  swSer.begin(9600, SWSERIAL_8N1, rxPin0, txPin0, false);
  if (!swSer) { // If the object did not initialize, then its configuration is invalid
    Log.errorln("Invalid SoftwareSerial pin configuration, check config"); 
  } 
  
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
  
  Log.traceln(F("Logging ready"));  

  // Read config from EEPROM
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < PROBE_COUNT; i++) {
    EEPROM.get(MIN_LEVEL + i * sizeof(minLevel[0]), minLevel[i]);
    EEPROM.get(MAX_LEVEL + i * sizeof(maxLevel[0]), maxLevel[i]);
  }
  EEPROM.get(SLEEP_TIME, sleepTime);
  EEPROM.get(MAX_DIFFERENCE, maxDifference);

  // Checking the recorded value (should only be useful on the first start)
  if (isnan(sleepTime) || sleepTime <= 0 || sleepTime > ESP.deepSleepMax()) {
    sleepTime = DEFAULT_SLEEP_TIME;
    EEPROM.put(SLEEP_TIME, sleepTime);
    commit = true;
    Log.warningln(F("Set default sleep time"));
  }
  Log.noticeln(F("Sleep time %D s"), (sleepTime / 1e6));

  for (int i = 0; i < PROBE_COUNT; i++) {
    if (minLevel[i] < CLOSEST || minLevel[i] > FARTHEST) {
      Log.warningln(F("minLevel[%d] incorrect: %d. Resetting to default"), i, minLevel[i]);
      minLevel[i] = CLOSEST;
      EEPROM.put(MIN_LEVEL+i*sizeof(minLevel[0]), minLevel[i]);
      commit = true;
    }

    if (maxLevel[i] < CLOSEST || maxLevel[i] > FARTHEST) {
      Log.warningln(F("maxLevel[%d] incorrect: %d. Resetting to default"), i, maxLevel[i]);
      maxLevel[i] = FARTHEST;
      EEPROM.put(MAX_LEVEL+i*sizeof(maxLevel[0]), maxLevel[i]);
      commit = true;
    }

    if (minLevel[i] == maxLevel[i]) {
      Log.warningln(F("minLevel[%d] and maxLevel[%d] are the same: %d. Resetting to default"), i, i, maxLevel[i]);
      minLevel[i] = CLOSEST;
      maxLevel[i] = FARTHEST;
      EEPROM.put(MIN_LEVEL+i*sizeof(minLevel[0]), minLevel[i]);
      EEPROM.put(MAX_LEVEL+i*sizeof(maxLevel[0]), maxLevel[i]);
      commit = true;
    }

    Log.noticeln(F("Levels[%d]: %d (deepest) - %d (highest)"), i, minLevel[i], maxLevel[i]);
  }

  if (isnan(maxDifference) || maxDifference == 0 || maxDifference > 1000) {
    Log.warningln(F("Set default max difference"));
    maxDifference = DEFAULT_MAX_DIFFERENCE;
    EEPROM.put(MAX_DIFFERENCE, maxDifference);
    commit = true;
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

  long waterLevel[PROBE_COUNT];
  if (PROBE_COUNT >= 1) {
    waterLevel[0] = getWaterLevel(&Serial, 0);
  }
  if (PROBE_COUNT >= 2) {
    waterLevel[1] = getWaterLevel(&swSer, 1);
  }

  /***********************************
   *     Reporting
   */

  // Init WiFi (as late as possible to save power)
  initWiFi();

  bool connected = client.connected();

  // Connect to MQTT
  if (!connected) {
    connected = reconnect();
  }
  
  if (connected) {
    mqttLog.setSuspend(false);
    client.loop();

    for (int i = 0; i < PROBE_COUNT; i++) {
      if (waterLevel[i] < CLOSEST || waterLevel[i] > FARTHEST) {
        Log.warningln(F("Not reporting the measurement %d as it si invalid"), i);
        continue;
      }

      // compute percentage of filled volume
      float filledLevel = (minLevel[i] - waterLevel[i] * 1.0) / (minLevel[i] - maxLevel[i]) * 100;
      client.publish((ROOT_TOPIC + "/level" + String(i)).c_str(), (String(waterLevel[i])).c_str());
      client.publish((ROOT_TOPIC + "/level" + String(i) + "Percentage").c_str(), (String(filledLevel)).c_str());
    }

    // Reporting voltage
    client.publish((ROOT_TOPIC + "/voltage").c_str(), (String(batteryLevel)).c_str());
    client.loop();

    // Make sure that buffered messages got sent
    mqttLog.setSuspend(false);
    delay(50);
    client.loop();
    delay(100);

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
      delay(10);
      client.loop();
    }

    // Preparing for sleep
    client.unsubscribe((ROOT_TOPIC + "/config").c_str());
    client.disconnect();
    delay(5);
  }
 
  WiFi.disconnect( true );

  if (commit) {
    bool commited = EEPROM.commit();
    Log.noticeln(F("New config committed: %T"), commited);
  }

  saveRtc();
  EEPROM.end();
  
  ESP.deepSleepInstant(sleepTime, WAKE_RF_DISABLED);

  Serial.println(F("What... I'm not asleep?!?"));  // it will never get here
  delay(5000);
}

void loop() {
  Serial.println(F("What... I'm not asleep?!?"));  // it will never get here
 }

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

/***************************************************************************************************************/

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

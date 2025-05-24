#ifndef GLOBAL_VARS_H
#define GLOBAL_VARS_H

#include "Arduino.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "PubSubPrint.h"

// Constants
#define BATTERY_ALERT_THRESHOLD 2.0 // V
#define BATTERY_ALERT_REARM 2.1     // V

#define DEFAULT_MAX_DIFFERENCE 200// mm

#define DEFAULT_SLEEP_TIME 5e6   // us

#define CLOSEST 200                   // mm
#define FARTHEST 8000                 // mm
#define SPEED_OF_SOUND 330            // m

#define BAT_ADC    2

#define MSG_BUFFER_SIZE  (50)

// Number of probes
#define PROBE_COUNT 2

// Debug (not used)
#define DEBUG true

// GPIO mapping
#define trigPin0 2
#define echoPin0 4
#define trigPin1 10
#define echoPin1 7
#define gndPin0 6

// Memory mapping
#define SETTINGS_NAMESPACE "settings"
//extern Preferences preferences;

// File logging config
#define MAX_LOG_FILE_NUMBER 20
#define BASE_PATH "/littlefs"
#define MAX_OPEN_FILE 2U
#define PARTITION_LABEL "storage"

// Water level mapping
extern RTC_DATA_ATTR int minLevel[];
extern RTC_DATA_ATTR int maxLevel[];

extern RTC_DATA_ATTR uint16_t lastMeasure[];
extern RTC_DATA_ATTR uint8_t  failedConnection;
extern RTC_DATA_ATTR bool     rtcValid;
extern RTC_DATA_ATTR uint8_t  channel;
extern RTC_DATA_ATTR uint8_t  bssid[6];
extern RTC_DATA_ATTR uint32_t run;
extern long wifiStart;

// Configuration
extern RTC_DATA_ATTR uint64_t sleepTime;
extern RTC_DATA_ATTR uint8_t maxDifference;
extern RTC_DATA_ATTR bool batteryAlertSent;
extern RTC_DATA_ATTR bool waterLevelAlertSent;
extern uint8_t logLevel;

extern WiFiClient espClient;
extern PubSubClient client;
extern PubSubPrint mqttLog;

// MQTT configuration
extern IPAddress MQTT_SERVER;
extern String ROOT_TOPIC;
extern bool callback_running;

// Network credentials
extern String WLAN_SSID;
extern String WLAN_PASSWD;

// Static IP to save power on DHCP dialog
extern IPAddress staticIP;
extern IPAddress gateway;
extern IPAddress subnet;
extern IPAddress dns;

// MQTT broker IP Address
extern IPAddress MQTT_SERVER;
extern String ROOT_TOPIC;

#endif
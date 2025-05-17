/*********
  Jean-Pierre Cadiat

  Credits:
   * Distance measurement by Probots: https://tutorials.probots.co.in/communicating-with-a-waterproof-ultrasonic-sensor-aj-sr04m-jsn-sr04t/
*********/
#include "main.h"

/****************************************************************************************************************/
RTC_DATA_ATTR char msg[MSG_BUFFER_SIZE];

/****************************************************************************************************************/
/*************\
 * Structure *
\*************/

// See https://www.bakke.online/index.php/2017/06/24/esp8266-wifi-power-reduction-avoiding-network-scan/
// The ESP8266 RTC memory is arranged into blocks of 4 bytes. The access methods read and write 4 bytes at a time,
// so the RTC data structure should be padded to a 4-byte multiple.
/*struct Rtc_Data {
  uint8_t  channel;                           // 1 byte,    5 in total
  uint8_t  bssid[6];                          // 6 bytes,   11 in total
  uint8_t  failedConnection;                  // 1 byte,    12 in total
  uint8_t  waterLevelAlertSent;               // 1 byte,    13 in total
  uint16_t lastMeasure[PROBE_COUNT];          // 2*PROBE_COUNT bytes
  uint16_t bufferPosition;                    // 2 bytes
  uint8_t  logBuffer[400-2*PROBE_COUNT];      // 400 bytes
};*/

// RTC_DATA_ATTR struct Rtc_Data rtcData;
RTC_DATA_ATTR uint8_t  channel;
RTC_DATA_ATTR uint8_t  bssid[6];
RTC_DATA_ATTR uint8_t  failedConnection;
RTC_DATA_ATTR uint16_t lastMeasure[PROBE_COUNT];
RTC_DATA_ATTR uint16_t bufferPosition;
RTC_DATA_ATTR uint8_t  logBuffer[1024];
RTC_DATA_ATTR uint16_t logBufferLength;
RTC_DATA_ATTR uint8_t  logLevel = LOG_LEVEL_NOTICE;
RTC_DATA_ATTR bool     rtcValid = false;
RTC_DATA_ATTR uint32_t run = 0;

/**********\
 * Global *
\**********/

// Initializes the espClient. You should change the espClient name if you have multiple ESPs running in your home automation system
WiFiClient   espClient;
PubSubClient client(espClient);
PubSubPrint  mqttLog = PubSubPrint(&client, "");
BufferPrint  bp;

bool callback_running = false;

float                  batteryLevel = 0; // value read from A0
RTC_DATA_ATTR uint64_t sleepTime = DEFAULT_SLEEP_TIME;
RTC_DATA_ATTR int      minLevel[PROBE_COUNT];
RTC_DATA_ATTR int      maxLevel[PROBE_COUNT];
RTC_DATA_ATTR uint8_t  maxDifference;
RTC_DATA_ATTR bool     batteryAlertSent = false;
RTC_DATA_ATTR bool     waterLevelAlertSent = false;

Preferences preferences;

long wifiStart = 0;

bool commit = false;

String LOG_TOPIC;

bool removeConfigMsg = false;

/*--------------------------------------------------------------------------------*/

void startSleep()
{
    if (commit)
    {
        bool commited = EEPROM.commit();
        Log.noticeln(F("New config committed: %T"), commited);
    }

    bp.flush();
    bp.removeOutput(&mqttLog);

    WiFi.disconnect(true);
    Log.noticeln(F("WiFi disconnected"));
    WiFi.mode(WIFI_OFF);

    size_t saved = mqttLog.saveBufferData(logBuffer);
    logBufferLength = saved;
    Log.noticeln(F("Saved %l bytes to log buffer"), saved);
    client.disconnect();
    espClient.flush();
    delay(10);

    EEPROM.end();
    rtcValid = true;
    printTimestamp(&Serial);
    Serial.println("Going down");
    run++;

    Serial.flush();
    // Shut down RTC (Low Power) Peripherals
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,   ESP_PD_OPTION_OFF);
    // Shut down RTC Slow Memory
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    // Shut down RTC Fast Memory
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
    // Shut down Crystal Oscillator XTAL 
    esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL,         ESP_PD_OPTION_OFF);
    // Enter Deep Sleep
    esp_sleep_enable_timer_wakeup(sleepTime);
    esp_deep_sleep_start();

    Serial.println(F("What... I'm not asleep?!?")); // it will never get here
    delay(5000);
}

void setup()
{

    /***********************************
     *     Initialisation
     */
    // Keep Wifi and bluetooth disabled at first
    btStop();
    WiFi.mode(WIFI_OFF);
    WiFi.setSleep(true);
    delayMicroseconds(10);

    Log.setPrefix(printPrefix); // set prefix similar to NLog

    Serial.begin(115200);

    pinMode(gndPin0, OUTPUT);
    digitalWrite(gndPin0, LOW);
    digitalWrite(5, LOW);

#if PROBE_COUNT >= 1
    pinMode(trigPin0, OUTPUT);
    pinMode(echoPin0, INPUT);
    digitalWrite(trigPin0, LOW);
#endif

#if PROBE_COUNT >= 2
    pinMode(trigPin1, OUTPUT);
    pinMode(echoPin1, INPUT);
    digitalWrite(trigPin1, LOW);
#endif

#if DEBUG
    /*while (!Serial)
    {
        ; // wait for serial port to connect. Needed for native USB port only
    }

    printTimestamp(&Serial);
    Serial.println("Serial started");
    Serial.setDebugOutput(true);

    esp_log_level_set("*", ESP_LOG_VERBOSE);
    log_e("EXAMPLE", "This doesn't show");*/
#endif

    LOG_TOPIC = ROOT_TOPIC + "/log";
    mqttLog = PubSubPrint(&client, LOG_TOPIC.c_str());
    mqttLog.setSuspend(true);
    size_t loaded = mqttLog.loadBufferData(logBuffer, logBufferLength);

    bp = BufferPrint();
    bp.addOutput(&Serial);
    Log.begin(logLevel, &bp);
    // Comment next line if you don’t want logging by MQTT
    bp.addOutput(&mqttLog);

    Log.traceln(F("Logging ready"));
    // Log.noticeln(F("Loaded %l bytes from log buffer"), loaded);

    preferences.begin("settings", false);
    preferences.end();

    // Read config from EEPROM
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < PROBE_COUNT; i++)
    {
        EEPROM.get(MIN_LEVEL + i * sizeof(minLevel[0]), minLevel[i]);
        EEPROM.get(MAX_LEVEL + i * sizeof(maxLevel[0]), maxLevel[i]);
    }
    EEPROM.get(SLEEP_TIME, sleepTime);
    EEPROM.get(MAX_DIFFERENCE, maxDifference);

    // Checking the recorded value (should only be useful on the first start)
    if (isnan(sleepTime) || sleepTime <= 0)
    {
        sleepTime = DEFAULT_SLEEP_TIME;
        EEPROM.put(SLEEP_TIME, sleepTime);
        commit = true;
        Log.warningln(F("Set default sleep time"));
    }
    Log.noticeln(F("Sleep time %i s"), (int)(sleepTime / 1e6));

    for (int i = 0; i < PROBE_COUNT; i++)
    {
        if (minLevel[i] < CLOSEST || minLevel[i] > FARTHEST)
        {
            Log.warningln(F("minLevel[%d] incorrect: %d. Resetting to default"), i, minLevel[i]);
            minLevel[i] = CLOSEST;
            EEPROM.put(MIN_LEVEL + i * sizeof(minLevel[0]), minLevel[i]);
            commit = true;
        }

        if (maxLevel[i] < CLOSEST || maxLevel[i] > FARTHEST)
        {
            Log.warningln(F("maxLevel[%d] incorrect: %d. Resetting to default"), i, maxLevel[i]);
            maxLevel[i] = FARTHEST;
            EEPROM.put(MAX_LEVEL + i * sizeof(maxLevel[0]), maxLevel[i]);
            commit = true;
        }

        if (minLevel[i] == maxLevel[i])
        {
            Log.warningln(F("minLevel[%d] and maxLevel[%d] are the same: %d. Resetting to default"), i, i, maxLevel[i]);
            minLevel[i] = CLOSEST;
            maxLevel[i] = FARTHEST;
            EEPROM.put(MIN_LEVEL + i * sizeof(minLevel[0]), minLevel[i]);
            EEPROM.put(MAX_LEVEL + i * sizeof(maxLevel[0]), maxLevel[i]);
            commit = true;
        }

        Log.noticeln(F("Levels[%d]: %d (deepest) - %d (highest)"), i, minLevel[i], maxLevel[i]);
    }

    if (isnan(maxDifference) || maxDifference == 0 || maxDifference > 1000)
    {
        Log.warningln(F("Set default max difference"));
        maxDifference = DEFAULT_MAX_DIFFERENCE;
        EEPROM.put(MAX_DIFFERENCE, maxDifference);
        commit = true;
    }

    Log.verboseln(F("RTC Data:"));
    Log.verboseln(F(" - rtcValid: %T"), rtcValid);
    Log.verboseln(F(" - BSSID: %x:%x:%x:%x:%x:%x"), bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    Log.verboseln(F(" - channel: %d"), channel);
    Log.verboseln(F(" - failedConnection: %d"), failedConnection);
    Log.verboseln(F(" - waterLevelAlertSent: %d"), waterLevelAlertSent);
    Log.verboseln(F(" - logLevel: %d"), logLevel);
    Log.verboseln(F(" - bufferPosition: %d"), bufferPosition);
    Log.verboseln(F(" - logBuffer: %s"), logBuffer);

    /***********************************
     *     Measures
     */

    // Read the battery level
    float batteryLevel = getVoltage();
    Log.verboseln(F("Battery voltage = %F V"), batteryLevel);

    // sends alert if battery low
    if (batteryLevel <= BATTERY_ALERT_THRESHOLD)
    {
        Log.warningln("Battery low!");
        client.publish((ROOT_TOPIC + "/alert").c_str(), "Battery low");
        batteryAlertSent = true;
    }
    else if (batteryAlertSent && batteryLevel > BATTERY_ALERT_REARM)
    {
        // Clear alert
        batteryAlertSent = false;
        client.publish((ROOT_TOPIC + "/alert").c_str(), new byte[0], 0, true);
    }

    long waterLevel[PROBE_COUNT];

#if PROBE_COUNT >= 1
    waterLevel[0] = getWaterLevel(trigPin0, echoPin0, 0);
    if (waterLevel[0] > 0)
    {
        lastMeasure[0] = waterLevel[0];
    }
#endif

#if PROBE_COUNT >= 2
    waterLevel[1] = getWaterLevel(trigPin1, echoPin1, 1);
    if (waterLevel[1] > 0)
    {
        lastMeasure[1] = waterLevel[1];
    }
#endif

    /***********************************
     *     Reporting
     */

    // Init WiFi (as late as possible to save power)
    if (!initWiFi())
    {
        startSleep();
    }

    if (reconnect())
    {
        mqttLog.setSuspend(false);
        client.loop();

        for (int i = 0; i < PROBE_COUNT; i++)
        {
            if (waterLevel[i] < CLOSEST || waterLevel[i] > FARTHEST)
            {
                Log.warningln(F("Not reporting the measurement %d as it is invalid"), i);
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
        Log.noticeln(F("Measurements sent"));

        // Make sure that buffered messages got sent
        mqttLog.setSuspend(false);
        delay(1);
        client.loop();

        // Empty Wifi reception buffer
        while (espClient.available())
        {
            mqttLog.setSuspend(false);
            delay(1);
            client.loop();
        }

        while (callback_running) {
            delay(1);
        }

        if (removeConfigMsg)
        {
            // This config message is intended for me only so I can delete it
            Log.noticeln(F("Config message processed"));
            client.publish((ROOT_TOPIC + "/config").c_str(), new byte[0], 0, true);
            delay(1);
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

    startSleep();
}

void loop()
{
    Serial.println(F("What... I'm not asleep?!?")); // it will never get here
}

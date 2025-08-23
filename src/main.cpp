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
MultiPrint   mp;
FilePrint    fileLog;

bool callback_running = false;

float                  batteryLevel = 0; // value read from A0
RTC_DATA_ATTR uint64_t sleepTime = DEFAULT_SLEEP_TIME;
RTC_DATA_ATTR uint64_t sleepTimeOnPower = DEFAULT_SLEEP_TIME;
RTC_DATA_ATTR float    onPowerThreshold = BATTERY_ON_POWER_THRESHOLD;
RTC_DATA_ATTR int      minLevel[PROBE_COUNT];
RTC_DATA_ATTR int      maxLevel[PROBE_COUNT];
RTC_DATA_ATTR uint8_t  maxDifference;
RTC_DATA_ATTR bool     batteryAlertSent = false;
RTC_DATA_ATTR bool     waterLevelAlertSent = false;

long waterLevel[PROBE_COUNT];

uint8_t lastFailedConnection = failedConnection;

long wifiStart = 0;

String LOG_TOPIC;

bool removeConfigMsg = false;

hw_timer_t *timer = NULL;
TaskHandle_t xHandleReport = NULL;
volatile bool timeoutFlag = false;

/*--------------------------------------------------------------------------------*/

void startSleep()
{
    Log.verboseln(F("Going to sleep"));

    uint64_t st = sleepTime;

    if (getVoltage() > onPowerThreshold) {
        st = sleepTimeOnPower;
        Log.verboseln(F("Power threshold reached."));
    }

    mp.flush();
    mp.removeOutput(&mqttLog);
    Log.verboseln(F("MQTT Logging disabled"));
    delay(1);

    size_t saved = mqttLog.saveBufferData(logBuffer);
    logBufferLength = saved;
    Log.noticeln(F("Saved %l bytes to log buffer"), saved);

    if (!timeoutFlag)
    {
        if (client.connected())
        {
            client.disconnect();
        }
        else
        {
            Log.warningln(F("MQTT client not connected"));
        }
        if (espClient.connected())
        {
            espClient.flush();
            delay(10);
            espClient.stop();
        }
        else
        {
            Log.warningln(F("ESP client not connected"));
        }

        if (WiFi.isConnected())
        {
            WiFi.disconnect(true, true);
            Log.noticeln(F("WiFi disconnected"));
            WiFi.mode(WIFI_OFF);
        }
        else
        {
            Log.warningln(F("WiFi not connected"));
        }

        // File log does not support multi threading very well
        Log.verboseln(F("Closing file and filesystem"));
        mp.removeOutput(&fileLog);
        fileLog.close();
        LittleFS.end();
    }
    else
    {
        Log.warningln(F("Timeout flag set"));
    }

    Log.verboseln(F("Keeping track on the run number"));
    run = ++run % 100000;

    Preferences preferences;
    if (preferences.begin(SETTINGS_NAMESPACE, false))
    {
        preferences.putUInt("run", run);
        preferences.end();
    }

    rtcValid = true;
    printTimestamp(&Serial);
    Serial.print("Going down for ");
    Serial.print(st / 1000);
    Serial.println("ms");

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
    esp_sleep_enable_timer_wakeup(st);
    esp_deep_sleep_start();

    Serial.println(F("What... I'm not asleep?!?")); // it should never get here
    delay(5000);
}

void IRAM_ATTR onTimer()
{
    // Put ESP to sleep if not connected when timer expires
    if (!WiFi.isConnected())
    {
        vTaskSuspend(xHandleReport);
        mp.removeOutput(&fileLog);
        // Decrease log level to avoid timeouts
        Log.setLevel(LOG_LEVEL_WARNING);
        esp_log_level_set("*", ESP_LOG_WARN);
        Log.warningln(F("WiFi connection timeout"));
        timeoutFlag = true;
        startSleep();
    }
}

void report()
{
    // Wifi needs 80+ MHz to work
    if (getCpuFrequencyMhz() < 80)
    {
        setCpuFrequencyMhz(80);
    }

    if (!initWiFi())
    {
        startSleep();
    }

    if (reconnect())
    {
        if (lastFailedConnection > 0)
        {
            fileGet(fileLog.getLastLogFileName());
        }

        // MQTT connection succeeded
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
            client.publish((ROOT_TOPIC + "/level" + String(i)).c_str(), (String(waterLevel[i])).c_str(), true);
            client.publish((ROOT_TOPIC + "/level" + String(i) + "Percentage").c_str(), (String(filledLevel)).c_str(), true);
        }

        // Reporting voltage
        client.publish((ROOT_TOPIC + "/voltage").c_str(), (String(batteryLevel)).c_str(), true);
        client.publish((ROOT_TOPIC + "/availability").c_str(), "online", false);
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

        while (callback_running)
        {
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
    while (!Serial)
    {
        ; // wait for serial port to connect. Needed for native USB port only
    }

    printTimestamp(&Serial);
    Serial.println("Serial started");
    Serial.setDebugOutput(true);

    esp_log_level_set("*", ESP_LOG_VERBOSE);
#endif

    LOG_TOPIC = ROOT_TOPIC + "/log";
    mqttLog = PubSubPrint(&client, LOG_TOPIC.c_str());
    mqttLog.setSuspend(true);
    size_t loaded = mqttLog.loadBufferData(logBuffer, logBufferLength);

    mp = MultiPrint();
    if (mp.instance != &mp)
    {
        Log.warningln(F("MultiPrint instance improperly set"));
        mp.instance = &mp;
    }
    mp.addOutput(&Serial);
    Log.begin(logLevel, &mp);
    // Comment next line if you don’t want logging by MQTT
    mp.addOutput(&mqttLog);

    // Check if any formatting is needed
    Preferences preferences;
    if (!preferences.begin(SETTINGS_NAMESPACE, false))
    {
        Log.errorln(F("Unable to read preferences. Rebooting"));
        ESP.restart();
    }

    if (!preferences.isKey("init"))
    {
        Log.warningln(F("First start"));
        preferences.end();
        nvs_flash_erase(); // erase the NVS partition and...
        nvs_flash_init();  // initialize the NVS partition.
        esp_littlefs_format(PARTITION_LABEL);
        preferences.begin(SETTINGS_NAMESPACE, false);
        preferences.putBool("init", true);
    }

    fileLog = FilePrint();
    mp.addOutput(&fileLog);
    if (preferences.isKey("logLevel"))
    {
        logLevel = preferences.getUShort("logLevel", LOG_LEVEL_NOTICE);
        Log.verboseln(F("Reading log level from Flash: %d"), logLevel);
    } else {
        Log.warningln(F("No log level found in Flash"));
    }
    Log.setLevel(logLevel);
    esp_log_level_set("*", (esp_log_level_t) (logLevel == 0 ? 0 : logLevel - 1));

    Log.traceln(F("Logging ready"));
    Log.noticeln(F("Loaded %l bytes from log buffer"), loaded);

    // Read config from Flash
    for (int i = 0; i < PROBE_COUNT; i++)
    {
        minLevel[i] = preferences.getInt(String("minLevel-" + i).c_str(), CLOSEST);
        maxLevel[i] = preferences.getInt(String("maxLevel-" + i).c_str(), FARTHEST);
    }
    // Sleep time
    sleepTime = preferences.getULong64("sleepTime", DEFAULT_SLEEP_TIME);

    // Sleep time on power
    sleepTimeOnPower = preferences.getULong64("sleepTimeOnPower", DEFAULT_SLEEP_TIME);
    onPowerThreshold = preferences.getFloat("onPowerThreshold", BATTERY_ON_POWER_THRESHOLD);

    // Max difference
    maxDifference = preferences.getUShort("maxDifference", DEFAULT_MAX_DIFFERENCE);

    // Checking the recorded value (should only be useful on the first start)
    if (isnan(sleepTime) || sleepTime <= 0)
    {
        sleepTime = DEFAULT_SLEEP_TIME;
        preferences.putULong64("sleepTime", DEFAULT_SLEEP_TIME);
        Log.warningln(F("Set default sleep time"));
    }
    Log.noticeln(F("Sleep time %i s"), (int)(sleepTime / 1e6));

    for (int i = 0; i < PROBE_COUNT; i++)
    {
        if (minLevel[i] < CLOSEST || minLevel[i] > FARTHEST)
        {
            Log.warningln(F("minLevel[%d] incorrect: %d. Resetting to default"), i, minLevel[i]);
            minLevel[i] = CLOSEST;
            preferences.putInt(String("minLevel-" + i).c_str(), minLevel[i]);
        }

        if (maxLevel[i] < CLOSEST || maxLevel[i] > FARTHEST)
        {
            Log.warningln(F("maxLevel[%d] incorrect: %d. Resetting to default"), i, maxLevel[i]);
            maxLevel[i] = FARTHEST;
            preferences.putInt(String("maxLevel-" + i).c_str(), maxLevel[i]);
        }

        if (minLevel[i] == maxLevel[i])
        {
            Log.warningln(F("minLevel[%d] and maxLevel[%d] are the same: %d. Resetting to default"), i, i, maxLevel[i]);
            minLevel[i] = CLOSEST;
            maxLevel[i] = FARTHEST;
            preferences.putInt(String("minLevel-" + i).c_str(), minLevel[i]);
            preferences.putInt(String("maxLevel-" + i).c_str(), maxLevel[i]);
        }

        Log.noticeln(F("Levels[%d]: %d (deepest) - %d (highest)"), i, minLevel[i], maxLevel[i]);
    }

    if (isnan(maxDifference) || maxDifference == 0 || maxDifference > 1000)
    {
        Log.warningln(F("Set default max difference"));
        maxDifference = DEFAULT_MAX_DIFFERENCE;
        preferences.putUShort("maxDifference", maxDifference);
    }

    if (run == 0)
    {
        Log.noticeln(F("Reading run from Flash"));
        if (preferences.isKey("run"))
        {
            run = preferences.getUInt("run", 0);
        }
    }

    preferences.end();

    Log.traceln(F("RTC Data:"));
    Log.traceln(F(" - rtcValid: %T"), rtcValid);
    Log.traceln(F(" - BSSID: %x:%x:%x:%x:%x:%x"), bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    Log.traceln(F(" - channel: %d"), channel);
    Log.traceln(F(" - failedConnection: %d"), failedConnection);
    Log.traceln(F(" - waterLevelAlertSent: %d"), waterLevelAlertSent);
    Log.traceln(F(" - logLevel: %d"), logLevel);
    Log.traceln(F(" - bufferPosition: %d"), bufferPosition);
    Log.traceln(F(" - logBuffer: %s"), logBuffer);

    /***********************************
     *     Measures
     */

    // Read the battery level
    batteryLevel = getVoltage();
    Log.traceln(F("Battery voltage = %F V"), batteryLevel);

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

    // Initialize timer (40MHz clock, prescaler 40 = 1MHz, count up)
    timer = timerBegin(0, 40, true);
    timerAttachInterrupt(timer, &onTimer, false);
    // Set timer for 45 seconds (45,000,000 microseconds)
    timerAlarmWrite(timer, 45000000, false);
    timerAlarmEnable(timer);
    // Init WiFi (as late as possible to save power)

    report();
}

void loop()
{
    Serial.println(F("What... I'm not asleep?!?")); // it will never get here
}

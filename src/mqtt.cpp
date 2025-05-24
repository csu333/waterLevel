#include "mqtt.h"
#include <base64.h>

extern bool removeConfigMsg;


/********\
 * MQTT *
\********/

void configMsg(String topic, String payload)
{

    /* Process the configuration command */

    // Allocate the JSON document
    JsonDocument doc;

    // Parse JSON object
    DeserializationError error = deserializeJson(doc, payload);
    if (error)
    {
        Log.errorln(F("deserializeJson() failed: %s"), error.c_str());
        return;
    }

    JsonObject conf = doc.as<JsonObject>();

    Log.traceln(F("Number of keypairs: %d"), conf.size());

    Preferences preferences;
    preferences.begin(SETTINGS_NAMESPACE, false);

    // Loop through all the key-value pairs in obj
    for (JsonPair p : conf)
    {

        const char *key = p.key().c_str();
        Log.traceln(F("Processing: { \"%s\": %s }"), key, p.value().as<String>().c_str());

        // Check for indexed keys

        char cKey[20];
        strncpy(cKey, key, sizeof(cKey) - 1);
        String sKey = String(cKey);
        int len = strlen(key);
        byte index = 0;
        if (len > 3 && key[len - 3] == '[' && isDigit(key[len - 2]) && key[len - 1] == ']')
        {
            index = (byte)key[len - 2] - '0';
            cKey[len - 3] = 0;

            if (index >= PROBE_COUNT)
            {
                Log.error(F("Configuration impossible as index (%d) is higher than the number of probes supported when compiling (%d)."), index, PROBE_COUNT);
                continue;
            }

            Log.traceln(F("Indexed key found: %s[%d]"), cKey, index);
        }

        // Process setting
        /*******************/
        //    min level
        /*******************/
        if (strcmp(cKey, "minLevel") == 0)
        {
            if (isnan(minLevel[index]) || minLevel[index] != (int)p.value())
            {
                minLevel[index] = (int)p.value();
                if (minLevel[index] > FARTHEST)
                {
                    minLevel[index] = FARTHEST;
                    Log.warningln(F("Min level in beyond max range. Setting probe %d to %d mm"), index, FARTHEST);
                }
                preferences.putInt(String("minLevel" + index).c_str(), minLevel[index]);

                Log.noticeln(F("New minimum level set for probe %d: %d"), index, minLevel[index]);
            }
            else
            {
                Log.verboseln(F("Value unchanged. Ignoring"));
            }
            /*******************/
            //    max level
            /*******************/
        }
        else if (strcmp(cKey, "maxLevel") == 0)
        {
            if (isnan(maxLevel[index]) || maxLevel[index] != (int)p.value())
            {
                maxLevel[index] = (int)p.value();
                if (maxLevel[index] < CLOSEST)
                {
                    maxLevel[index] = CLOSEST;
                    Log.warningln(F("Max level in blind zone. Setting probe %d to %d mm"), index, CLOSEST);
                }
                preferences.putInt(String("maxLevel" + index).c_str(), maxLevel[index]);

                Log.noticeln(F("New maximum level set for probe %d: %d"), index, maxLevel[index]);
            }
            else
            {
                Log.verboseln(F("Value unchanged. Ignoring"));
            }
            /*******************/
            //   sleep Time
            /*******************/
        }
        else if (strcmp(key, "sleepTime") == 0)
        {
            if (isnan(sleepTime) || sleepTime / 1e6 != p.value())
            {
                sleepTime = p.value();

                if (sleepTime > 1e12)
                {
                    sleepTime = 1e18;
                }
                else
                {
                    sleepTime *= 1e6;
                }

                preferences.putULong64("sleepTime", sleepTime);

                Log.noticeln(F("New sleep time set: %i s"), (int)(sleepTime / 1e6));
            }
            else
            {
                Log.verboseln(F("Value unchanged. Ignoring"));
            }
            /*******************/
            // max Difference
            /*******************/
        }
        else if (strcmp(key, "maxDifference") == 0)
        {
            if (maxDifference != p.value())
            {
                if (p.value() <= 0 || p.value() > maxLevel[0])
                {
                    Log.warningln("Incorrect max difference value. Must be between 0 and maxLevel[0] (%D)", maxLevel[0]);
                    continue;
                }

                maxDifference = p.value();

                preferences.putUShort("maxDifference", maxDifference);

                Log.noticeln(F("New max difference set: %d"), maxDifference);
            }
            else
            {
                Log.verboseln(F("Value unchanged. Ignoring"));
            }
            /*******************/
            //     Log level
            /*******************/
        }
        else if (strcmp(key, "logLevel") == 0)
        {
            if (logLevel != p.value())
            {
                logLevel = p.value();

                if (logLevel > LOG_LEVEL_VERBOSE)
                {
                    logLevel = LOG_LEVEL_VERBOSE;
                }
                Log.setLevel(logLevel);
                preferences.putUShort("logLevel", logLevel);
                Log.noticeln(F("New log level set: %d"), logLevel);
            }
            else
            {
                Log.verboseln(F("Value unchanged. Ignoring"));
            }
        }
        else
        {
            Log.warningln(F("Unknown config parameter: %s"), p.key().c_str());
        }
    }

    for (int i = 0; i < PROBE_COUNT; i++) {
        if (minLevel[i] < maxLevel[i])
        {
            Log.warningln("Minimum level %d should be bigger than maximum level because the water is further away from the sensor when the level is at its minimum.", i);
        }
    }

    preferences.end();
    removeConfigMsg = true;
}

void updateMsg(String topic, String payload)
{

    if (payload.length() == 0)
    {
        return;
    }

    Log.noticeln(F("Received an OTA message. Preparing to download from %s."), payload.c_str());

    if (update(payload, 80))
    {
        Log.noticeln(F("Ready to restart"));
        client.publish((ROOT_TOPIC + "/update/url").c_str(), new byte[0], 0, true);
        delay(1000);
        ESP.restart();
    }
}

void fileGet(String fileName)
{

    if (fileName.length() == 0)
    {
        Log.errorln(F("File name is empty"));
        return;
    }

    if (!LittleFS.begin(false, BASE_PATH, MAX_OPEN_FILE, PARTITION_LABEL)) {
        Log.errorln(F("Failed to mount LittleFS"));
        return;
    }

    if (!LittleFS.exists(fileName))
    {
        Log.errorln(F("File does not exist"));
        return;
    }

    File file = LittleFS.open(fileName, "r");
    if (!file || file.isDirectory())
    {
        Log.errorln(F("Failed to open file"));
        return;
    }

    String dataTopic = ROOT_TOPIC + "/file/data" + fileName;
    Log.noticeln(F("Sending file %s (size = %d) on topic '%s'"), fileName.c_str(), file.size(), dataTopic.c_str());
    String content = file.readString();
    
    if (content.length() == 0)
    {
        Log.warningln(F("Failed to read file"));
        byte *contentByte = new byte[file.size()];
        file.readBytes((char *)contentByte, file.size());
        content = base64::encode(contentByte, file.size());
    }
    file.close();
    
    String contentPart = String(MQTT_MAX_PACKET_SIZE / 2);
    for (int i = 0; i < content.length(); i += contentPart.length() + 1)
    {
        contentPart = content.substring(i, i + MQTT_MAX_PACKET_SIZE / 2);
        contentPart = contentPart.substring(0, contentPart.lastIndexOf('\n'));
        client.publish(dataTopic.c_str(), contentPart.c_str(), false);
        client.flush();
    }
    
    client.publish((ROOT_TOPIC + "/file/get").c_str(), new byte[0], 0, true);
}

void dirList(String dirName)
{
    Log.noticeln(F("Listing directory %s"), dirName.c_str());
    if (dirName.length() == 0)
    {
        Log.errorln(F("Directory name is empty"));
        return;
    }

    if (!LittleFS.begin(false, BASE_PATH, MAX_OPEN_FILE, PARTITION_LABEL)) {
        Log.errorln(F("Failed to mount LittleFS"));
        return;
    }

    if (!LittleFS.exists(dirName))
    {
        Log.errorln(F("Directory %s does not exist"), dirName.c_str());
        return;
    }

    File file = LittleFS.open(dirName, "r");
    if (!file || !file.isDirectory())
    {
        Log.errorln(F("Failed to open directory %s"), dirName.c_str());
        return;
    }

    String dataTopic = ROOT_TOPIC + "/file/dir" + dirName;
    file.rewindDirectory();

    String nextFileName = file.getNextFileName();
    while (nextFileName != "") {
        File f = LittleFS.open(dirName + "/" + nextFileName, "r");
        String data = nextFileName + "," + String(f.size());
        f.close();
        client.publish(dataTopic.c_str(), data.c_str(), false);
        nextFileName = file.getNextFileName();
    }

    client.flush();
    
    client.publish((ROOT_TOPIC + "/file/dirlist").c_str(), new byte[0], 0, true);
}

void callback(char *topic, byte *payload, unsigned int length)
{
    if (length == 0)
    {
        return;
    }

    callback_running = true;
    // Stop sending log to MQTT to avoid deadlocks
    // mqttLog.setSuspend(true);
    payload[length] = '\0';

    String _topic = String(topic);
    String _payload = String((char *)payload);
    Log.noticeln(F("Message received on topic: %s"), _topic.c_str());

    if (_topic.equals(ROOT_TOPIC + "/update/url") == 1)
    {
        updateMsg(_topic, _payload);
    }

    if (_topic.equals(ROOT_TOPIC + "/config") == 1)
    {
        configMsg(_topic, _payload);
    }

    if (_topic.equals(ROOT_TOPIC + "/file/get") == 1)
    {
        fileGet(_payload);
    }

    if (_topic.equals(ROOT_TOPIC + "/file/dirlist") == 1)
    {
        dirList(_payload);
    }

    callback_running = false;
}

bool reconnect()
{
    // Init MQTT
    client.setServer(MQTT_SERVER, 1883);
    int i = 3;

    // Loop until we're reconnected
    while (!client.connected() && i > 0)
    {
        Log.noticeln(F("Connecting to MQTT"));

        // Create a random client ID
        String clientId = "waterLevel-";
        clientId += String(random(0xffff), HEX);

        // Attempt to connect
        if (client.connect(clientId.c_str()))
        {
            // ... and resubscribe
            client.setCallback(callback);
            client.subscribe((ROOT_TOPIC + "/config").c_str());
            client.subscribe((ROOT_TOPIC + "/update/url").c_str());
            client.subscribe((ROOT_TOPIC + "/file/get").c_str());
            client.subscribe((ROOT_TOPIC + "/file/dirlist").c_str());
            Log.noticeln(F("Subscription done"));
            delay(100);
            return true;
        }
        else
        {
            Log.errorln(F("Failed, rc=%d try again in 1 seconds"), client.state());
            // Wait 1 seconds before retrying
            delay(1000);
        }
        i--;
    }

    if (i <= 0)
    {
        Log.warningln(F("Failed to send MQTT message"));
        return false;
    }

    return true;
}

/********\
 * MQTT *
\********/

void configMsg(String topic, String payload) {
  
  /* Process the configuration command */

  // Allocate the JSON document
  //const size_t capacity = JSON_OBJECT_SIZE(50);
  JsonDocument doc;

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
    Log.traceln(F("Processing: { \"%s\": %s }"), key, p.value().as<String>().c_str());
    
    // Check for indexed keys

    char cKey[20];
    strncpy(cKey, key, sizeof(cKey) - 1);
    String sKey = String(cKey);
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
    /*******************/
    //    min level
    /*******************/
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
    /*******************/
    //    max level
    /*******************/
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
    /*******************/
    //   sleep Time
    /*******************/
    } else if (strcmp(key, "sleepTime") == 0) {
      if (isnan(sleepTime) || sleepTime / 1e6 != p.value()){
        sleepTime = p.value();
        
        if (sleepTime  > 1e12) {
          sleepTime = 1e18;
        } else {
          sleepTime *= 1e6;
        }
        
        EEPROM.put(SLEEP_TIME, sleepTime);
        commit = true;
        
        Log.noticeln(F("New sleep time set: %i s"), (int)(sleepTime / 1e6));
      } else {
        Log.verboseln(F("Value unchanged. Ignoring"));
      }
    /*******************/
    // max Difference
    /*******************/
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
    /*******************/
    //     Log level
    /*******************/
    } else if (strcmp(key, "logLevel") == 0) {
      if (logLevel != p.value()){
        logLevel = p.value();
        
        if (logLevel > LOG_LEVEL_VERBOSE) {
          logLevel = LOG_LEVEL_VERBOSE;
        }
        Log.setLevel(logLevel);
        
        Log.noticeln(F("New log level set: %d"), logLevel);
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

void updateMsg(String topic, String payload) {

  if (payload.length() == 0) {
    return;
  }

  Log.noticeln(F("Received an OTA message. Preparing to download from %s."), payload.c_str());
  
  if (update(payload, 80)) {
      Log.noticeln(F("Ready to restart"));
      client.publish((ROOT_TOPIC + "/update/url").c_str(), new byte[0], 0, true);
      delay(1000);
      ESP.restart();
  }
}
  

void callback(char* topic, byte* payload, unsigned int length) {
  if (length == 0) {
    return;
  }

  // Stop sending log to MQTT to avoid deadlocks
  //mqttLog.setSuspend(true);
  payload[length] = '\0';

  String _topic = String(topic);
  String _payload = String((char*)payload);
  Log.noticeln(F("Message received on topic: %s"), _topic.c_str());

  if (_topic.equals(ROOT_TOPIC + "/update/url") == 1) {
      updateMsg(_topic, _payload);
  }

  if (_topic.equals(ROOT_TOPIC + "/config") == 1) {
      configMsg(_topic, _payload);
  }
}

bool reconnect() {
  // Init MQTT
  client.setServer(MQTT_SERVER, 1883);
  int i = 3;
  
  // Loop until we're reconnected
  while (!client.connected() && i > 0) {
    Log.noticeln(F("Connecting to MQTT"));

    // Create a random client ID
    String clientId = "waterLevel-";
    clientId += String(random(0xffff), HEX);
    
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      // ... and resubscribe
      client.setCallback(callback);
      client.subscribe((ROOT_TOPIC + "/config").c_str());
      client.subscribe((ROOT_TOPIC + "/update/url").c_str());
      Log.noticeln(F("Subscription done"));
      delay(100);
      return true;
    } else {
      Log.errorln(F("Failed, rc=%d try again in 1 seconds"), client.state());
      // Wait 1 seconds before retrying
      delay(1000);
					  
    }
    i--;
  }

  if (i <= 0) {
    Log.warningln(F("Failed to send MQTT message"));
    return false;
  }

  return true;
}




	   
		  
	 
  
				 
													  
				   
	 
  
				
   
  
  
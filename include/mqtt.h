#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <PubSubClient.h>
#include "ota.h"
#include "global_vars.h"
#include "Arduino.h"
#include <LittleFS.h>
#include <Preferences.h>

bool reconnect();
void fileGet(String fileName);
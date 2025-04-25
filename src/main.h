#include "Arduino.h"
#include <NTPClient.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include "BufferPrint.h"
#include "PubSubPrint.h"
#include "global_vars.h"
#include "mqtt.h"
#include <Preferences.h>
#include "measure.h"
#include "PrintUtils.h"
#include "Wifi.h"




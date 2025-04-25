#include "Arduino.h"
#include "global_vars.h"
#include <ArduinoLog.h>
#include <EEPROM.h>

float getVoltage();
int getWaterReading(uint8_t trigPin, uint8_t echoPin);
int getWaterLevel(uint8_t trigPin, uint8_t echoPin, uint8_t index);
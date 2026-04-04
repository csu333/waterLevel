#ifndef PRINT_UTILS_H
#define PRINT_UTILS_H

#include "Arduino.h"

extern RTC_DATA_ATTR uint32_t run;

void printTimestamp(Print* _logOutput);
void printPrefix(Print* _logOutput, int logLevel);

#endif
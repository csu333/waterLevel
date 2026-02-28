#ifndef FILE_PRINT_H
#define FILE_PRINT_H

#include <cstddef>
#include <Print.h>
#include <LittleFS.h>
#include <ArduinoLog.h>
#include "Arduino.h"
#include "global_vars.h"

class FilePrint : public Print
{
    private:
        File logFile;
        bool initialized = false;
        String lastLogFileName = "";

    public:
        FilePrint();

        size_t write(const uint8_t * buffer, size_t size) override;
        size_t write(uint8_t c) override;

        String getLastLogFileName();

        void close();
};

#endif

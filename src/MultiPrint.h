#include <cstddef>
#include <Print.h>
#include <cstdarg>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <cstdio>
#include <esp_log.h>
#include "PrintUtils.h"
#include <sys/_stdint.h>
#include <stdlib.h>

class MultiPrint : public Print
{
    private:
        Print ** output;
        int outputCount = 0;

        static int vprintf(const char *format, va_list args);
        SemaphoreHandle_t xMutex;
    public:
        MultiPrint();
        ~MultiPrint();
        static MultiPrint *instance;

        size_t write(const uint8_t * buffer, size_t size) override;
        size_t write(uint8_t c) override;

        bool addOutput(Print * printer);
        bool removeOutput(Print * printer);
        uint8_t getOutputCount();

        void flush();
};

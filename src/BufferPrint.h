#include <cstddef>
#include <Print.h>

class BufferPrint : public Print
{
    private:
        size_t bufferSize;
        int pos = 0;
        Print ** output;
        int outputCount = 0;
        uint8_t * _buffer;
    public:
        BufferPrint();
        BufferPrint(uint8_t * buffer[]);

        size_t write(const uint8_t * buffer, size_t size) override;
        size_t write(uint8_t c) override;

        bool addOutput(Print * printer);
        bool removeOutput(Print * printer);

        void flush();
};

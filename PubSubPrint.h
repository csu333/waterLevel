#include <cstddef>
#include <Print.h>
#include <PubSubClient.h>

class PubSubPrint : public Print
{
    private:
        PubSubClient * client;
        char * topic;
        bool suspended = false;

        uint8_t * _buffer;
        uint16_t pos = 0;

    public:
        PubSubPrint(PubSubClient * pbClient, const char * pbTopic);

        size_t write(const uint8_t * buffer, size_t size) override;
        size_t write(uint8_t c) override;

        void setSuspend(bool suspend);

        size_t saveBufferData(uint8_t * buffer);
        size_t loadBufferData(uint8_t * buffer, size_t length);

        void flush();
};

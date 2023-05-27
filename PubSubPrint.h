#include <Print.h>
#include <PubSubClient.h>

class PubSubPrint : public Print
{
    private:
        PubSubClient* client;
        const char* topic;
        bool suspended = false;
    public:
        PubSubPrint(PubSubClient* pbClient, const char* pbTopic);

        size_t write(const uint8_t *buffer, size_t size) override;
        size_t write(uint8_t c) override;

        void setSuspend(bool suspend);
        
        uint8_t _buffer[1024];
        uint16_t pos = 0;
};

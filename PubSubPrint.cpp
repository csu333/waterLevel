#include <cstddef>
#include "PubSubPrint.h"

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

PubSubPrint::PubSubPrint(PubSubClient* pbClient, const char* pbTopic) {
  client = pbClient;
  _buffer = (unsigned char *) malloc(1024);
  _buffer[0] = 0;

  size_t len = strlen(pbTopic);
  topic = (char *) malloc((len + 1) * sizeof(char));
  memcpy(topic, pbTopic, len * sizeof(char));
  topic[len] = 0;
}

size_t PubSubPrint::write(const uint8_t * buffer, size_t size) {
    bool sent = false;

    if (pos + size < 1024) {
      memcpy(&_buffer[pos], buffer, size);
      pos += size;
    }

    bool fullString = false;
    for (int i = 0; !fullString && i < pos; i++) {
      fullString = _buffer[i] == '\n' || _buffer[i] == 0;
    }
    //Serial.printf("Fullsting = %u, pos = %u, suspended = %u, connected = %u\n", fullString, pos, suspended, client->connected());

    if (!suspended && (fullString || pos > 800)) {
      if (!client->connected()) {
        String clientId = "waterLevel-";
        clientId += String(random(0xffff), HEX);
        client->connect(clientId.c_str());
      }
      int start = 0;
      int end = 0;
      while (end < pos) {
        while (end < pos && _buffer[end] != 0 && _buffer[end] != '\n') {
          end++;
        }
        //Serial.printf("Start = %u, end = %u\n", start, end);
        if (end > start && end < pos) {
          char message[end - start];
          memcpy(message, &_buffer[start], end - start);
          message[end - start] = 0;
          //Serial.printf("Sending to MQTT: %d bytes [%d - %d] (%s)\n", end - start, start, end, message);
          sent = client->publish(topic, (const char *) &message);
          //Serial.printf("Sent: %d on topic %s\n", sent, topic);
          client->flush();
          delay(20);
          start = end + 1;
          end = start;
        }
      }

      client->flush();
      //Serial.printf("Start: %d, end: %d, pos: %d\n", start, end, pos);

      if (start >= pos) {
        end = pos;
        pos = 0;
        return pos;
      }

      if (start < pos) {
        // Keep the messages not sent for later
        char swap[pos - start];
        memcpy(swap, &_buffer[start], pos - start);
        memcpy(_buffer, swap, pos - start);
        pos = pos - start;
        return start;
      }
    }

    return 0;
}

size_t PubSubPrint::write(uint8_t c) {
    return write(&c, 1);
}

size_t PubSubPrint::saveBufferData(uint8_t * buffer) {
  _buffer[pos] = 0;
  memcpy(buffer, _buffer, pos);

  return pos;
}

size_t PubSubPrint::loadBufferData(uint8_t * buffer, size_t length) {
  memcpy(_buffer, buffer, length);
  _buffer[length] = 0;
  pos = length;

  return pos;
}

void PubSubPrint::setSuspend(bool suspend) {
  suspended = suspend;
  if (!suspended) {
    flush();
  }
}

void PubSubPrint::flush() {
  PubSubPrint::write(0, 0);
}

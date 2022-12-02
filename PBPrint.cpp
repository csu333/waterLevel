#include "PBPrint.h"

PBPrint::PBPrint(PubSubClient* pbClient, const char* pbTopic) {
  client = pbClient;
  topic = pbTopic;
}

size_t PBPrint::write(const uint8_t *buffer, size_t size) {
    
    // Buffer incomplete messages and messages that arrive while the printer is suspended
    if (size > 0 && (suspended || buffer[size - 1] != '\n')) {
      // Shift _buffer to make room for new message
      if (pos + size > sizeof(_buffer)) {
        int shift = pos + size - sizeof(_buffer);
        for (int i = 0; i < pos - shift; i++) {
          _buffer[i] = _buffer[i + shift];
        }
        pos -= shift;
      }
      
      memcpy(&_buffer[pos], buffer, size);
      pos += size;
      
      return 0;
    }

    int sent = 0;
    // Flush messages in _buffer, assuming \n as message separator
    int start = 0;
    int i;
    for (i = 0; i < pos; i++) {
      if (_buffer[i] == '\n' && (i - start) > 1) {
        client->publish(topic, &_buffer[start], i - start);
        sent += i - start;
        start = i + 1;
      }
    }

    // Flush the end of the _buffer even if there is not \n
    if (pos - start > 1) {
        client->publish(topic, &_buffer[start], pos - start);
        sent += pos - start;
    }
    
    pos = 0;

    // Send current message if it contains more than \n
    if (size > 1) {
      client->publish(topic, buffer, size);
    }
    
    return sent;
}

size_t PBPrint::write(uint8_t c) {
    // Ignore empty messages
    if (c == '\n' && (pos == 0 || (pos == 1 && _buffer[0] < 32))) {
      return 0;
    }

    // Send complete messages
    const uint8_t buffer[] { c };
    return write(buffer, 1);
}

void PBPrint::setSuspend(bool suspend) {
  suspended = suspend;
  
  if (!suspended) {
    write('\n');
  }
}

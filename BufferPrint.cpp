#include "BufferPrint.h"

BufferPrint::BufferPrint(){
  _buffer = new uint8_t[495];
  bufferSize = sizeof(_buffer);
  output = new Print*[4];
}

BufferPrint::BufferPrint(uint8_t *buffer[]){
  _buffer = (uint8_t*)buffer;
  bufferSize = sizeof(buffer);
  output = new Print*[4];
}

size_t BufferPrint::write(const uint8_t *buffer, size_t size) {

    // Shift buffer to make room for new message
    if (pos + size > bufferSize) {
      int shift = pos + size - bufferSize;
      for (int i = 0; i < pos - shift; i++) {
        _buffer[i] = _buffer[i + shift];
      }
      pos -= shift;
    }

    memcpy(&_buffer[pos], buffer, size);
    pos += size;
    if (size == 1) {
      for (int i = 0; i < outputCount; i++) {
        output[i]->write(buffer[0]);
      }
    } else {
      for (int i = 0; i < outputCount; i++) {
        output[i]->write(buffer, size);
      }
    }
    
    return size;
}

size_t BufferPrint::write(uint8_t c) {
  uint8_t buffer[] = { c };
  return BufferPrint::write(buffer, 1);
}

bool BufferPrint::addOutput(Print* printer) {
  if (outputCount >= sizeof(output)) {
    return false;
  }

  output[outputCount++] = printer;

  if (pos > 0) {
    output[outputCount - 1]->write(_buffer, pos);
  }

  return false;
}

#include <sys/_stdint.h>
#include <stdlib.h>
#include "BufferPrint.h"

BufferPrint::BufferPrint(){
  _buffer = (uint8_t *) malloc(1024);
  bufferSize = sizeof(_buffer);
  output = new Print*[4];
}

BufferPrint::BufferPrint(uint8_t * buffer[]){
  _buffer = (uint8_t *)buffer;
  bufferSize = sizeof(buffer);
  output = new Print*[4];
}

size_t BufferPrint::write(const uint8_t * buffer, size_t size) {

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
    for (int i = 0; i < outputCount; i++) {
      output[i]->write(buffer, size);
    }
    
    return size;
}

size_t BufferPrint::write(uint8_t c) {
  return BufferPrint::write(&c, 1);
}

bool BufferPrint::addOutput(Print * printer) {
  if (outputCount >= sizeof(output)) {
    return false;
  }

  output[outputCount++] = printer;

  if (pos > 0) {
    output[outputCount - 1]->write(_buffer, pos);
  }

  return false;
}

bool BufferPrint::removeOutput(Print* printer) {
  bool found = false;
  for (int i = 0; i <= outputCount; i++) {
    if (output[i] == printer) {
      found = true;
      for (int j = i; j <= outputCount - 1; j++) {
        output[j] = output[j + 1];
      }
      outputCount--;
    }
  }
  return found;
}

void BufferPrint::flush() {
  for (int i = 0; i < outputCount; i++) {
    output[i]->flush();
  }
}

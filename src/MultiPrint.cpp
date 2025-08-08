#include "MultiPrint.h"

MultiPrint *MultiPrint::instance = nullptr;

MultiPrint::MultiPrint()
{
  output = new Print *[4];
  for (int i = 0; i < sizeof(output); i++)
  {
    output[i] = nullptr;
  }
  instance = this;
  xMutex = xSemaphoreCreateMutex();
  esp_log_set_vprintf(vprintf);
}

MultiPrint::~MultiPrint()
{
  /*if (instance == this) {
    instance = nullptr;
  }*/
  //esp_log_set_vprintf(nullptr);
}

size_t MultiPrint::write(const uint8_t *buffer, size_t size)
{
  xSemaphoreTake(xMutex, (TickType_t)10000);

  for (int i = 0; i < outputCount; i++)
  {
    if (output[i] == nullptr)
    {
      continue;
    }
    output[i]->write(buffer, size);
  }
  xSemaphoreGive(xMutex); // release mutex

  return size;
}

size_t MultiPrint::write(uint8_t c)
{
  return MultiPrint::write(&c, 1);
}

bool MultiPrint::addOutput(Print *printer)
{
  if (outputCount >= sizeof(output))
  {
    return false;
  }

  xSemaphoreTake(xMutex, (TickType_t)10000);
  output[outputCount++] = printer;
  xSemaphoreGive(xMutex); // release mutex

  return false;
}

bool MultiPrint::removeOutput(Print *printer)
{
  bool found = false;

  xSemaphoreTake(xMutex, (TickType_t)10000);

  for (int i = 0; i < outputCount; i++)
  {
    if (output[i] == printer)
    {
      found = true;
      for (int j = i; j < outputCount; j++)
      {
        output[j] = output[j + 1];
      }
      for (int j = outputCount; j < sizeof(output); j++)
      {
        output[j] = nullptr;
      }
      outputCount--;
    }
  }
  xSemaphoreGive(xMutex); // release mutex
  return found;
}

uint8_t MultiPrint::getOutputCount()
{
  return outputCount;
}

int MultiPrint::vprintf(const char *format, va_list args)
{
  if (!instance)
  {
    return 0;
  }

  char buffer[512];
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  if (len > 0)
  {
    printTimestamp(instance);
    return instance->write((uint8_t *)buffer, len);
  }
  return 0;
}

void MultiPrint::flush()
{
  for (int i = 0; i < outputCount; i++)
  {
    output[i]->flush();
  }
}

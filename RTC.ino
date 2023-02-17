

uint32_t calculateCRC32( const uint8_t *data, size_t length ) {
  uint32_t crc = 0xffffffff;
  while( length-- ) {
    uint8_t c = *data++;
    for( uint32_t i = 0x80; i > 0; i >>= 1 ) {
      bool bit = crc & 0x80000000;
      if( c & i ) {
        bit = !bit;
      }

      crc <<= 1;
      if( bit ) {
        crc ^= 0x04c11db7;
      }
    }
  }

  return crc;
}



bool loadRtc() {
  if (rtcLoaded) {
    return true;
  }
  
  // Try to read WiFi settings from RTC memory
  bool readSucceed = ESP.rtcUserMemoryRead( 0, (uint32_t*)&rtcData, sizeof( rtcData ) );
  rtcLoaded = true;

  if (!readSucceed) {
    rtcData = { 0, 0, 0, 128, 0, 0, 3, 0, 0, {} };
    Log.errorln("Unable to read values from RTC");
    return false;
  }
    
  // Calculate the CRC of what we just read from RTC memory, but skip the first 4 bytes as that's the checksum itself.
  uint32_t crc = calculateCRC32( ((uint8_t*)&rtcData) + 4, sizeof( rtcData ) - 4 );
  if( crc != rtcData.crc32 ) {
    rtcData = { 0, 0, 0, 128, 0, 0, 3, 0, 0, {} };
    Log.errorln("Unable to reload values from RTC: CRC failed");
    return false;
  }

  rtcValid = true;

  // Load values from RTC memory
  batteryAlertSent = rtcData.batteryAlertSent;
  waterLevelAlertSent = rtcData.waterLevelAlertSent;

  // Restore logging buffer
  mqttLog.pos = rtcData.bufferPosition;
  memcpy(mqttLog._buffer, rtcData.logBuffer, min(sizeof(rtcData.logBuffer), (unsigned int)mqttLog.pos));
  
  Log.noticeln("RTC load success");

  return true;
}

void saveRtc() {
  // Save logging buffer
  if (mqttLog.pos != rtcData.bufferPosition) {
    Log.traceln(F("Saving logs"));
    rtcData.bufferPosition = min(sizeof(rtcData.logBuffer), (unsigned int)mqttLog.pos);
    memcpy(rtcData.logBuffer, mqttLog._buffer, rtcData.bufferPosition);
    rtcDirty = true;
  }
  
  if (rtcDirty) {
    Log.traceln(F("Saving data in RTC memory"));
    rtcData.crc32 = calculateCRC32( ((uint8_t*)&rtcData) + 4, sizeof( rtcData ) - 4 );
    ESP.rtcUserMemoryWrite( 0, (uint32_t*)&rtcData, sizeof( rtcData ) );
  }
}

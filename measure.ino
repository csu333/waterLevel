float getVoltage() {
  float floatVoltage = 2 * (3.3 * analogRead(PIN_A0)) / 1023; 
  return floatVoltage;
}

int getWaterReading(Stream* serial, byte index) {
  long distance = -1;
  byte startByte, h_data, m_data, l_data, sum;
  byte buf[] = { 0, 0, 0, 0 };

  Log.noticeln(F("getWaterReading"));

  // Swap Serial from USB to RX/TX pins
  if (serial == &Serial) {
    Log.traceln(F("Trying to use Serial. Swapping to pins 13/15"));
    Serial.flush();
    Serial.swap();
  }

  // Empty buffer
  while (serial->available() > 0) {
    int inByte = serial->read();
  }

  // Requesting reading
  serial->write(0x01);
  // Wait for the reply to be ready
  delay((FARTHEST / SPEED_OF_SOUND) + 1);

  serial->readBytes(buf, 4);
  startByte = buf[0];
  
  // Swap Serial from RX/TX pins to USB
  if (serial == &Serial) {
    Serial.swap();
    Log.verboseln(F("Swapping back to TX/RX pins"));
  }

  if(startByte != 255){
    Log.errorln(F("Invalid header: %d "), startByte);
    return -1;
  }

  h_data = buf[1];
  l_data = buf[2];
  sum = buf[3];
  distance = (h_data<<8) + l_data;
    
  if((( h_data + l_data)&0xFF) != sum){
    Log.errorln(F("Invalid result: %d %d (%d)"), buf[1], buf[2], buf[3]);
  }

  delay(1);
  Log.verboseln(F("Remaining in buffer: %d"), serial->available());

  if (distance < CLOSEST || distance > FARTHEST) {
    Log.errorln(F("Reading beyond bounds (%d - %d): %d"), CLOSEST, FARTHEST, distance);
    return -1;
  }
  
  return distance;
}

int getWaterLevel(Stream* serial, byte index) {

  int distance = getWaterReading(serial, index);

  if (distance < 0) {
    Log.errorln(F("Error reading water level %d"), index);
    return -1;
  }

  Log.noticeln("Distance %d: %d mm", index, distance);

  // Check that returned value makes sense
  if (distance < CLOSEST) {
    Log.warningln("Distance too short for the sensor. Trying again.");
    distance = getWaterReading(serial, index);
  }

  int i = 0;
  // Check reading plausibility
  while (abs(distance - rtcData.lastMeasure[index]) > maxDifference && i < 2) {
    Log.warningln("There is more than %dmm difference with last measure (was %dmm compared to %dmm now). Trying again after 5s", maxDifference, rtcData.lastMeasure[index], distance);
    delay(5000);
    rtcData.lastMeasure[index] = (uint16_t)distance;
    distance = getWaterReading(serial, index);

    if (distance < 0) {
      Log.errorln(F("Error reading water level %d"), index);
      return -1;
    }

    Log.noticeln("Distance %d: %d mm", index, distance);

    i++;
  }

  if (abs(distance - rtcData.lastMeasure[index]) > maxDifference) {
    Log.warningln(F("Distance not stabilising. Giving up"));
    return -1;
  }

  // Check that configuration values are correct
  if (distance > minLevel[index]) {
    Log.warningln("Measured water level is lower than minimum configured level. Adapting setting probe %d to %d.", index, distance);
    // minimum level is lower than expected
    minLevel[index] = distance;
    EEPROM.put(MIN_LEVEL+index*sizeof(minLevel[0]), distance);
    commit = true;
  }

  if (minLevel[index] > FARTHEST) {
    Log.warningln(F("Min level too far. Setting probe %d to %d mm"), index, FARTHEST);
    EEPROM.put(MIN_LEVEL+index*sizeof(minLevel[0]), FARTHEST);
    commit = true;
  }

  if (distance < maxLevel[index]) {
    Log.warningln("Measured water level is higher than maximum configured level. Adapting setting probe %d to %d.", index, distance);
    // maximum level is higher than expected
    maxLevel[index] = distance;
    EEPROM.put(MAX_LEVEL+index*sizeof(maxLevel[0]), distance);
    commit = true;
  }

  if (maxLevel[index] < CLOSEST) {
    Log.warningln(F("Max level too close. Setting probe %d to %d mm"), index, CLOSEST);
    EEPROM.put(MAX_LEVEL+index*sizeof(maxLevel[0]), CLOSEST);
    commit = true;
  }
  
  if (minLevel[index] == maxLevel[index]) {
    minLevel[index] = maxLevel[index] + 1;
    Log.warningln(F("Min and max levels are the sameon probe %d. Min set to %d mm"), index, minLevel[index]);
    EEPROM.put(MIN_LEVEL+index*sizeof(minLevel[0]), minLevel[index]);
    commit = true;
  }
  
  return distance;
}
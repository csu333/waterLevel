#include "measure.h"

float getVoltage()
{
    //float floatVoltage = (analogReadMilliVolts(BAT_ADC)) * 1.55 / 1000;
    int rawValue = analogRead(BAT_ADC);
    float floatVoltage = rawValue * (3.3 / 1023.0 / 2);  // Convert ADC reading to voltage
    return floatVoltage;
}

int getWaterReading(uint8_t trigPin, uint8_t echoPin)
{
    long duration;     // variable for the duration of sound wave travel
    int distance = -1; // variable for the distance measurement

    // Sets the trigPin HIGH (ACTIVE) for 10 microseconds
    Log.traceln("Triggering on port %u and listening echo on port %u", trigPin, echoPin);
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    digitalWrite(trigPin, LOW); //
    delay(10);
    digitalWrite(trigPin, HIGH);
    delay(2);
    digitalWrite(trigPin, LOW);
    delay(1);
    // Reads the echoPin, returns the sound wave travel time in microseconds
    duration = pulseIn(echoPin, HIGH, 50000);
    // Calculating the distance
    distance = duration * 0.34 / 2; // Speed of sound wave divided by 2 (go and back)

    if (distance <= 0)
    {
        // Log.errorln(F("Reading too short: %d mm (%d microseconds)"), distance, duration);
        return -1;
    }

    // Displays the distance on the Serial Monitor
    return distance;
}

int getWaterLevel(uint8_t trigPin, uint8_t echoPin, uint8_t index)
{

    int distance = getWaterReading(trigPin, echoPin);

    // Check that returned value makes sense

    int i = 0;
    // Check reading plausibility
    while ((distance < CLOSEST || abs(distance - lastMeasure[index]) > maxDifference) && i < 4)
    {

        if (distance < CLOSEST)
        {
            Log.warningln("Distance too short for the sensor %d. Trying again.", index);
        }

        if (distance >= CLOSEST && abs(distance - lastMeasure[index]) > maxDifference)
        {
            Log.warningln("There is more than %dmm difference with last measure (was %dmm compared to %dmm now). Trying again", maxDifference, lastMeasure[index], distance);
            lastMeasure[index] = (uint16_t)distance;
        }

        delay(random(20, 100));
        distance = getWaterReading(trigPin, echoPin);

        if (distance < 0)
        {
            Log.errorln(F("Error reading water level %d: No value returned"), index);
        }

        i++;
    }

    if (distance < CLOSEST)
    {
        return -1;
    }

    if (abs(distance - lastMeasure[index]) > maxDifference)
    {
        Log.warningln(F("Distance not stabilising. Giving up"));
        return -1;
    }

    Log.noticeln("Distance %d: %d mm", index, distance);

    Preferences preferences;
    if (preferences.begin(SETTINGS_NAMESPACE, false)) {
        // Check that configuration values are correct
        if (distance > minLevel[index])
        {
            Log.warningln("Measured water level is lower than minimum configured level. Adapting setting probe %d to %d.", index, distance);
            // minimum level is lower than expected
            minLevel[index] = distance;
            preferences.putInt(String("minLevel-" + index).c_str(), distance);
        }

        if (minLevel[index] > FARTHEST)
        {
            Log.warningln(F("Min level too far. Setting probe %d to %d mm"), index, FARTHEST);
            preferences.putInt(String("minLevel-" + index).c_str(), FARTHEST);
        }

        if (distance < maxLevel[index])
        {
            Log.warningln("Measured water level is higher than maximum configured level. Adapting setting probe %d to %d.", index, distance);
            // maximum level is higher than expected
            maxLevel[index] = distance;
            preferences.putInt(String("maxLevel-" + index).c_str(), distance);
        }

        if (maxLevel[index] < CLOSEST)
        {
            Log.warningln(F("Max level too close. Setting probe %d to %d mm"), index, CLOSEST);
            preferences.putInt(String("maxLevel-" + index).c_str(), CLOSEST);
        }

        if (minLevel[index] == maxLevel[index])
        {
            minLevel[index] = maxLevel[index] + 1;
            Log.warningln(F("Min and max levels are the same on probe %d. Min set to %d mm"), index, minLevel[index]);
            preferences.putInt(String("minLevel-" + index).c_str(), minLevel[index]);
        }
        preferences.end();
    } else {
        Log.errorln(F("Error opening preferences"));
    }

    return distance;
}

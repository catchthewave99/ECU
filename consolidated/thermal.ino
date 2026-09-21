// Thermal control, on the consolidated board.
//
// Reads the coolant temperature, runs the cooling fan between two thresholds
// so it cannot chatter, and lights the over-temperature lamp if it gets
// dangerously hot.

#include "ecu.h"

int  coolantC = 0;
bool fanRunning = false;
bool overTemp = false;

static unsigned long lastSensorMs = 0;

// Reads the sensor and converts it to whole degrees C.
static int readCoolantC() {
#if DEFECT_BLOCKING_SENSOR
  // DEF-102: carried over from functions/thermal, where nothing else needed
  // the board. Sixteen readings 10 ms apart stop every other job for 160 ms,
  // which is longer than the hazard lamps are allowed to take.
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(PIN_TEMP_SENSOR);
    delay(10);
  }
  int counts = (int)(sum / 16);
#else
  int counts = analogRead(PIN_TEMP_SENSOR);  // 0 - 1023 for 0 - 5 V
#endif
  int millivolts = (int)((long)counts * 5000 / 1024);
  return millivolts / 10;
}

void thermalUpdate() {
  if (millis() - lastSensorMs < SENSOR_PERIOD_MS) return;
  lastSensorMs = millis();

  coolantC = readCoolantC();

  // Two thresholds, not one, so the fan does not chatter on and off when the
  // temperature sits right on the limit.
  if (coolantC >= FAN_ON_C) fanRunning = true;
  if (coolantC <= FAN_OFF_C) fanRunning = false;

  overTemp = (coolantC >= OVERTEMP_C);

  digitalWrite(PIN_FAN_RELAY, fanRunning ? HIGH : LOW);
  digitalWrite(PIN_OVERTEMP, overTemp ? HIGH : LOW);
}

// Consolidated ECU - Arduino Uno R3
//
// Three controllers on one board: thermal control, body control and hazard
// warning. Each one lives in its own file next to this one and does its work
// in a single update function. This file starts the board up, calls those
// update functions over and over, and prints one line of telemetry every
// 200 ms so the test bench can see what the board is doing.
//
// Nothing here waits. There is no delay() anywhere in a normal build, because
// a job that waits also holds up the hazard lamps, and the hazard lamps have
// 100 ms to respond (REQ-SAFE-100).
//
// Wiring: docs/07-wiring-chart.md
// Numbers:  tests/limits.env
//
// Build: arduino-cli compile -b arduino:avr:uno consolidated

#include "ecu.h"

// The flash clock, shared by the turn signals and the hazard lamps so that
// they cannot flash out of step with each other.
bool flashOn = false;
unsigned long lastFlashMs = 0;

unsigned long lastReportMs = 0;

// How long the slowest pass of loop() took. A test reads this to show that no
// job is hogging the board.
unsigned long loopMaxMs = 0;
unsigned long lastLoopMs = 0;

void flasherUpdate() {
  if (millis() - lastFlashMs >= FLASH_HALF_PERIOD_MS) {
    lastFlashMs = millis();
    flashOn = !flashOn;
  }
}

void report() {
  if (millis() - lastReportMs < REPORT_PERIOD_MS) return;
  lastReportMs = millis();

  Serial.print(millis());
  Serial.print('\t'); Serial.print(coolantC);
  Serial.print('\t'); Serial.print(fanRunning ? 1 : 0);
  Serial.print('\t'); Serial.print(overTemp ? 1 : 0);
  Serial.print('\t'); Serial.print(headlampOn ? 1 : 0);
  Serial.print('\t'); Serial.print(turnLeft ? 1 : 0);
  Serial.print('\t'); Serial.print(turnRight ? 1 : 0);
  Serial.print('\t'); Serial.print(hazardOn ? 1 : 0);
  Serial.print('\t'); Serial.println(loopMaxMs);
}

void setup() {
  pinMode(PIN_HAZARD_SW, INPUT);
  pinMode(PIN_HEADLAMP_SW, INPUT);
  pinMode(PIN_TURN_L_SW, INPUT);
  pinMode(PIN_TURN_R_SW, INPUT);

  pinMode(PIN_LAMP_LEFT, OUTPUT);
  pinMode(PIN_LAMP_RIGHT, OUTPUT);
  pinMode(PIN_HEADLAMP, OUTPUT);
  pinMode(PIN_FAN_RELAY, OUTPUT);
  pinMode(PIN_OVERTEMP, OUTPUT);
  pinMode(PIN_HEARTBEAT, OUTPUT);

  Serial.begin(115200);
  Serial.println(F("# consolidated ECU: thermal + body + hazard"));
#if DEFECT_SHARED_PIN || DEFECT_BLOCKING_SENSOR
  Serial.println(F("# WARNING: built with known defects enabled"));
#endif
  Serial.println(F("t_ms\tcoolantC\tfan\tovertemp\thead\tturnL\tturnR\thazard\tloop_max_ms"));

  lastLoopMs = millis();
}

void loop() {
  digitalWrite(PIN_HEARTBEAT, !digitalRead(PIN_HEARTBEAT));

  // Hazard first: it is the job with a deadline.
  flasherUpdate();
  hazardUpdate();
  bodyUpdate();
  thermalUpdate();
  report();

  unsigned long now = millis();
  if (now - lastLoopMs > loopMaxMs) loopMaxMs = now - lastLoopMs;
  lastLoopMs = now;
}

// Consolidated ECU - pins, settings and the list of jobs.
//
// One Arduino Uno R3 now does the work of the three separate controllers in
// functions/: thermal control, body control and hazard warning. This header is
// the single place where pins and settings live, so it can be checked against
// the wiring chart (tests/check_pinmap.sh) and against the acceptance numbers
// (tests/limits.env).

#ifndef ECU_H
#define ECU_H

// ---- Known defects, switched on at build time -----------------------------
// The consolidation was done the way it usually is done: each function was
// moved across keeping its own pin choices and its own habits. Two problems
// came with it. Both are kept here, behind a switch, so the failing build can
// be rebuilt on demand as test evidence. A normal build has both at 0.
//
//   DEFECT_SHARED_PIN      - the headlamp switch stayed on D2, which the
//                            hazard switch also uses (DEF-101)
//   DEFECT_BLOCKING_SENSOR - the thermal function still averages 16 sensor
//                            readings 10 ms apart, which stops everything
//                            else for 160 ms (DEF-102)
#ifndef DEFECT_SHARED_PIN
#define DEFECT_SHARED_PIN 0
#endif
#ifndef DEFECT_BLOCKING_SENSOR
#define DEFECT_BLOCKING_SENSOR 0
#endif

// ---- Pins -----------------------------------------------------------------
// Switch inputs are driven by the test rig: HIGH means the switch is closed.
const int PIN_TEMP_SENSOR = A0;  // coolant temperature sensor, 0 - 5 V
const int PIN_HAZARD_SW   = 2;   // hazard switch
const int PIN_TURN_L_SW   = 4;   // turn-signal stalk, left
const int PIN_TURN_R_SW   = 5;   // turn-signal stalk, right
#if DEFECT_SHARED_PIN
const int PIN_HEADLAMP_SW = 2;   // DEF-101: collides with the hazard switch
#else
const int PIN_HEADLAMP_SW = 7;   // headlamp switch
#endif

const int PIN_LAMP_LEFT   = 8;   // left turn lamp   (turn signal and hazard)
const int PIN_LAMP_RIGHT  = 9;   // right turn lamp  (turn signal and hazard)
const int PIN_HEADLAMP    = 10;  // headlamps
const int PIN_FAN_RELAY   = 11;  // cooling fan relay, HIGH = fan running
const int PIN_OVERTEMP    = 12;  // over-temperature warning lamp
const int PIN_HEARTBEAT   = 13;  // flips every pass, so a test can see us run

// ---- Settings -------------------------------------------------------------
// The sensor gives 10 mV for every degree C, so millivolts / 10 is degrees C.
const int FAN_ON_C   = 95;   // switch the fan on at or above this
const int FAN_OFF_C  = 90;   // switch it off again at or below this
const int OVERTEMP_C = 105;  // light the warning lamp at or above this

const unsigned long SENSOR_PERIOD_MS = 50;   // how often we read the sensor
const unsigned long FLASH_HALF_PERIOD_MS = 350;  // lamp on time, then off time
const unsigned long REPORT_PERIOD_MS = 200;  // how often we print a line

// ---- The jobs -------------------------------------------------------------
// loop() calls these and nothing else. None of them is allowed to wait: a job
// that waits also delays the others, and the hazard lamps have 100 ms to
// respond (REQ-SAFE-100).
void flasherUpdate();   // the shared on/off clock the flashing lamps follow
void hazardUpdate();
void bodyUpdate();
void thermalUpdate();

// What each job decided, shared so the telemetry line can report it.
extern int  coolantC;
extern bool fanRunning;
extern bool overTemp;
extern bool headlampOn;
extern bool turnLeft;
extern bool turnRight;
extern bool hazardOn;
extern bool flashOn;
extern unsigned long lastFlashMs;

#endif

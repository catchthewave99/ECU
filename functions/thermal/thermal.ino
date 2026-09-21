// Thermal control ECU - Arduino Uno R3
//
// What it does: watches the engine coolant temperature and switches the
// cooling fan on when it gets hot, off again when it has cooled down. If the
// temperature goes high enough to be dangerous it also lights a warning lamp.
//
// Wiring: docs/07-wiring-chart.md
// Numbers:  tests/limits.env
//
// Build: arduino-cli compile -b arduino:avr:uno functions/thermal

// ---- Pins -----------------------------------------------------------------
const int PIN_TEMP_SENSOR = A0;  // coolant temperature sensor, 0 - 5 V
const int PIN_FAN_RELAY   = 11;  // cooling fan relay, HIGH = fan running
const int PIN_OVERTEMP    = 12;  // over-temperature warning lamp
const int PIN_HEARTBEAT   = 13;  // flips every pass, so a test can see us run

// ---- Settings -------------------------------------------------------------
// The sensor gives 10 mV for every degree C, so millivolts / 10 is degrees C.
const int FAN_ON_C    = 95;   // switch the fan on at or above this
const int FAN_OFF_C   = 90;   // switch it off again at or below this
const int OVERTEMP_C  = 105;  // light the warning lamp at or above this

const unsigned long SENSOR_PERIOD_MS = 50;   // how often we read the sensor
const unsigned long REPORT_PERIOD_MS = 200;  // how often we print a line

// ---- State ----------------------------------------------------------------
int  coolantC = 0;
bool fanRunning = false;
bool overTemp = false;

unsigned long lastSensorMs = 0;
unsigned long lastReportMs = 0;

// Reads the sensor and converts it to whole degrees C.
int readCoolantC() {
  int counts = analogRead(PIN_TEMP_SENSOR);  // 0 - 1023 for 0 - 5 V
  int millivolts = (int)((long)counts * 5000 / 1024);
  return millivolts / 10;
}

// Decides what the fan and the warning lamp should do.
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

void report() {
  if (millis() - lastReportMs < REPORT_PERIOD_MS) return;
  lastReportMs = millis();

  Serial.print(millis());
  Serial.print('\t'); Serial.print(coolantC);
  Serial.print('\t'); Serial.print(fanRunning ? 1 : 0);
  Serial.print('\t'); Serial.println(overTemp ? 1 : 0);
}

void setup() {
  pinMode(PIN_FAN_RELAY, OUTPUT);
  pinMode(PIN_OVERTEMP, OUTPUT);
  pinMode(PIN_HEARTBEAT, OUTPUT);

  Serial.begin(115200);
  Serial.println(F("# thermal control ECU"));
  Serial.println(F("t_ms\tcoolantC\tfan\tovertemp"));
}

void loop() {
  digitalWrite(PIN_HEARTBEAT, !digitalRead(PIN_HEARTBEAT));
  thermalUpdate();
  report();
}

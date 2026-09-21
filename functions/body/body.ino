// Body control ECU - Arduino Uno R3
//
// What it does: the lighting a driver operates directly. The headlamp switch
// turns the headlamps on and off. The turn-signal stalk makes the lamp on that
// side flash on and off at a steady rate for as long as the stalk is held.
//
// Wiring: docs/07-wiring-chart.md
// Numbers:  tests/limits.env
//
// Build: arduino-cli compile -b arduino:avr:uno functions/body

// ---- Pins -----------------------------------------------------------------
// Switch inputs are driven by the test rig: HIGH means the switch is closed.
const int PIN_HEADLAMP_SW = 2;   // headlamp switch
const int PIN_TURN_L_SW   = 4;   // turn-signal stalk, left
const int PIN_TURN_R_SW   = 5;   // turn-signal stalk, right

const int PIN_LAMP_LEFT   = 8;   // left turn lamp
const int PIN_LAMP_RIGHT  = 9;   // right turn lamp
const int PIN_HEADLAMP    = 10;  // headlamps
const int PIN_HEARTBEAT   = 13;  // flips every pass, so a test can see us run

// ---- Settings -------------------------------------------------------------
// A turn lamp is on for this long, then off for this long, and so on.
const unsigned long FLASH_HALF_PERIOD_MS = 350;
const unsigned long REPORT_PERIOD_MS = 200;

// ---- State ----------------------------------------------------------------
bool headlampOn = false;
bool turnLeft = false;
bool turnRight = false;
bool flashOn = false;           // which half of the flash cycle we are in

unsigned long lastFlashMs = 0;
unsigned long lastReportMs = 0;

// Runs the headlamps and the turn-signal flasher.
void bodyUpdate() {
  headlampOn = (digitalRead(PIN_HEADLAMP_SW) == HIGH);
  turnLeft   = (digitalRead(PIN_TURN_L_SW) == HIGH);
  turnRight  = (digitalRead(PIN_TURN_R_SW) == HIGH);

  digitalWrite(PIN_HEADLAMP, headlampOn ? HIGH : LOW);

  // The flasher is just a clock: every half period, swap on for off.
  if (millis() - lastFlashMs >= FLASH_HALF_PERIOD_MS) {
    lastFlashMs = millis();
    flashOn = !flashOn;
  }

  // A lamp flashes only while its side of the stalk is held.
  digitalWrite(PIN_LAMP_LEFT,  (turnLeft  && flashOn) ? HIGH : LOW);
  digitalWrite(PIN_LAMP_RIGHT, (turnRight && flashOn) ? HIGH : LOW);
}

void report() {
  if (millis() - lastReportMs < REPORT_PERIOD_MS) return;
  lastReportMs = millis();

  Serial.print(millis());
  Serial.print('\t'); Serial.print(headlampOn ? 1 : 0);
  Serial.print('\t'); Serial.print(turnLeft ? 1 : 0);
  Serial.print('\t'); Serial.println(turnRight ? 1 : 0);
}

void setup() {
  pinMode(PIN_HEADLAMP_SW, INPUT);
  pinMode(PIN_TURN_L_SW, INPUT);
  pinMode(PIN_TURN_R_SW, INPUT);

  pinMode(PIN_LAMP_LEFT, OUTPUT);
  pinMode(PIN_LAMP_RIGHT, OUTPUT);
  pinMode(PIN_HEADLAMP, OUTPUT);
  pinMode(PIN_HEARTBEAT, OUTPUT);

  Serial.begin(115200);
  Serial.println(F("# body control ECU"));
  Serial.println(F("t_ms\thead\tturnL\tturnR"));
}

void loop() {
  digitalWrite(PIN_HEARTBEAT, !digitalRead(PIN_HEARTBEAT));
  bodyUpdate();
  report();
}

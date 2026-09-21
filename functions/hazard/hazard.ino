// Hazard warning ECU - Arduino Uno R3
//
// What it does: while the hazard switch is closed, both turn lamps flash
// together. This is the safety function of the bench: the driver presses the
// switch, and the lamps must start flashing within 100 ms. That limit is in
// tests/limits.env as HAZARD_RESPONSE_MAX_MS and it is not negotiable.
//
// Wiring: docs/07-wiring-chart.md
// Numbers:  tests/limits.env
//
// Build: arduino-cli compile -b arduino:avr:uno functions/hazard

// ---- Pins -----------------------------------------------------------------
const int PIN_HAZARD_SW  = 2;   // hazard switch, HIGH means pressed
const int PIN_LAMP_LEFT  = 8;   // left turn lamp
const int PIN_LAMP_RIGHT = 9;   // right turn lamp
const int PIN_HEARTBEAT  = 13;  // flips every pass, so a test can see us run

// ---- Settings -------------------------------------------------------------
const unsigned long FLASH_HALF_PERIOD_MS = 350;
const unsigned long REPORT_PERIOD_MS = 200;

// ---- State ----------------------------------------------------------------
bool hazardOn = false;
bool flashOn = false;

unsigned long lastFlashMs = 0;
unsigned long lastReportMs = 0;

// Runs the hazard flasher. Nothing in here waits for anything, so the lamps
// respond on the very next pass of loop() after the switch is pressed.
void hazardUpdate() {
  bool pressed = (digitalRead(PIN_HAZARD_SW) == HIGH);

  if (pressed && !hazardOn) {
    // Light the lamps immediately on the press, then start the flash clock, so
    // the driver never has to wait for the middle of a flash cycle.
    hazardOn = true;
    flashOn = true;
    lastFlashMs = millis();
  } else if (!pressed && hazardOn) {
    hazardOn = false;
    flashOn = false;
  }

  if (hazardOn && millis() - lastFlashMs >= FLASH_HALF_PERIOD_MS) {
    lastFlashMs = millis();
    flashOn = !flashOn;
  }

  digitalWrite(PIN_LAMP_LEFT,  (hazardOn && flashOn) ? HIGH : LOW);
  digitalWrite(PIN_LAMP_RIGHT, (hazardOn && flashOn) ? HIGH : LOW);
}

void report() {
  if (millis() - lastReportMs < REPORT_PERIOD_MS) return;
  lastReportMs = millis();

  Serial.print(millis());
  Serial.print('\t'); Serial.print(hazardOn ? 1 : 0);
  Serial.print('\t'); Serial.println(flashOn ? 1 : 0);
}

void setup() {
  pinMode(PIN_HAZARD_SW, INPUT);

  pinMode(PIN_LAMP_LEFT, OUTPUT);
  pinMode(PIN_LAMP_RIGHT, OUTPUT);
  pinMode(PIN_HEARTBEAT, OUTPUT);

  Serial.begin(115200);
  Serial.println(F("# hazard warning ECU"));
  Serial.println(F("t_ms\thazard\tlamps"));
}

void loop() {
  digitalWrite(PIN_HEARTBEAT, !digitalRead(PIN_HEARTBEAT));
  hazardUpdate();
  report();
}

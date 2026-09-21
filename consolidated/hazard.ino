// Hazard warning, on the consolidated board.
//
// While the hazard switch is closed, both turn lamps flash together. From the
// press to the lamps coming on there is a budget of 100 ms
// (REQ-SAFE-100, HAZARD_RESPONSE_MAX_MS in tests/limits.env).
//
// Hazard beats the turn signals: it writes the lamps, and body.ino leaves them
// alone while hazardOn is true.

#include "ecu.h"

bool hazardOn = false;

void hazardUpdate() {
  bool pressed = (digitalRead(PIN_HAZARD_SW) == HIGH);

  if (pressed && !hazardOn) {
    // Light the lamps on the press itself and restart the flash clock, so the
    // driver never waits for the middle of a flash cycle.
    hazardOn = true;
    flashOn = true;
    lastFlashMs = millis();
  } else if (!pressed && hazardOn) {
    hazardOn = false;
  }

  if (hazardOn) {
    digitalWrite(PIN_LAMP_LEFT,  flashOn ? HIGH : LOW);
    digitalWrite(PIN_LAMP_RIGHT, flashOn ? HIGH : LOW);
  }
}

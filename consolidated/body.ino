// Body control, on the consolidated board.
//
// The headlamp switch turns the headlamps on and off. The turn-signal stalk
// makes the lamp on that side flash for as long as the stalk is held.
//
// The two turn lamps are shared with the hazard warning, so this file leaves
// them alone whenever the hazard warning is on.

#include "ecu.h"

bool headlampOn = false;
bool turnLeft = false;
bool turnRight = false;

void bodyUpdate() {
  headlampOn = (digitalRead(PIN_HEADLAMP_SW) == HIGH);
  turnLeft   = (digitalRead(PIN_TURN_L_SW) == HIGH);
  turnRight  = (digitalRead(PIN_TURN_R_SW) == HIGH);

  digitalWrite(PIN_HEADLAMP, headlampOn ? HIGH : LOW);

  if (!hazardOn) {
    digitalWrite(PIN_LAMP_LEFT,  (turnLeft  && flashOn) ? HIGH : LOW);
    digitalWrite(PIN_LAMP_RIGHT, (turnRight && flashOn) ? HIGH : LOW);
  }
}

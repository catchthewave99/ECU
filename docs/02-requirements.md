# 02 - Requirements

These are the statements the bench verifies. They were written and frozen
**before** the three functions were consolidated onto one board, so that
consolidation could not quietly redefine what "working" means.

Numeric limits live in `tests/limits.env`, never here in prose; this document
names the limit variable instead. Every requirement is traced to a test in
`docs/05-traceability.md`.

Convention for switches: a switch input reads HIGH when the switch is closed
(pressed). Every lamp output is active high.

## Interface requirements

| ID | Requirement | Limit |
| --- | --- | --- |
| REQ-IF-001 | The ECU shall read coolant temperature as a 0 - 5 V single-ended analog input on A0, 10-bit, AVcc reference, scaled 10 mV per degree C. | `TEMP_REPORT_TOL_C` |
| REQ-IF-002 | The ECU shall read the hazard switch on D2, the turn stalk on D4 (left) and D5 (right), and the headlamp switch on D7. | - |
| REQ-IF-003 | The ECU shall drive the left lamp on D8, the right lamp on D9, the headlamps on D10, the cooling fan on D11 and the over-temperature warning on D12, all active high. | - |
| REQ-IF-004 | No two signals shall share a pin. | - |
| REQ-IF-005 | The ECU shall flip D13 once per pass of `loop()`, so that the bench can measure how long a pass takes. | `LOOP_MAX_MS` |
| REQ-IF-006 | The ECU shall emit one telemetry record every 200 ms at 115200 8N1 in the documented column order. | - |
| REQ-IF-007 | No ECU output shall be relied on to source or sink more than 20 mA, and total port current shall stay below 200 mA. | device limit |

## Thermal control

| ID | Requirement | Limit |
| --- | --- | --- |
| REQ-THRM-010 | Reported coolant temperature shall equal the applied temperature within tolerance. | `TEMP_REPORT_TOL_C` |
| REQ-THRM-011 | The fan shall run when coolant temperature is at or above the fan-on temperature. | `FAN_ON_C` |
| REQ-THRM-012 | The fan shall stop when coolant temperature is at or below the fan-off temperature. | `FAN_OFF_C` |
| REQ-THRM-013 | Between those two temperatures the fan shall hold its current state, so that it does not chatter around the switching point. | `FAN_ON_C`, `FAN_OFF_C` |
| REQ-THRM-014 | The over-temperature warning shall light at or above the over-temperature limit and clear below it. | `OVERTEMP_C` |
| REQ-THRM-015 | Reading the temperature sensor shall not block; the sensor is sampled on a timer, not waited for. | `LOOP_MAX_MS` |

## Body control

| ID | Requirement | Limit |
| --- | --- | --- |
| REQ-BODY-020 | The headlamps shall be on while the headlamp switch is closed and off while it is open. | - |
| REQ-BODY-021 | The headlamps shall respond only to the headlamp switch. | - |
| REQ-BODY-022 | While the left stalk is held, the left lamp shall flash and the right lamp shall stay dark; and the mirror image for the right stalk. | - |
| REQ-BODY-023 | A flashing lamp shall be on for one half period and off for one half period. | `FLASH_HALF_MS_MIN`, `FLASH_HALF_MS_MAX` |

## Hazard warning (safety-critical)

| ID | Requirement | Limit |
| --- | --- | --- |
| REQ-SAFE-100 | From the hazard switch closing to the first hazard lamp turning on, no more than the maximum response time shall elapse - at any temperature, in any lighting state, on a build with all three functions enabled. | `HAZARD_RESPONSE_MAX_MS` |
| REQ-SAFE-101 | Every press shall be answered. A test shall press the switch repeatedly, because a job that blocks intermittently only delays some presses. | `HAZARD_PRESS_COUNT` |
| REQ-SAFE-102 | Both hazard lamps shall flash together, in step with each other. | `FLASH_HALF_MS_MIN/MAX` |
| REQ-SAFE-103 | While the hazard switch is closed, the turn stalk shall not take either hazard lamp over. | - |
| REQ-SAFE-104 | No single pass of `loop()` shall take longer than the loop limit. This is what makes REQ-SAFE-100 achievable rather than lucky. | `LOOP_MAX_MS` |

## Consolidation requirements

| ID | Requirement | Limit |
| --- | --- | --- |
| REQ-CONS-200 | The consolidated build shall satisfy every requirement above, tested with the same cases and the same limits used for the three functions on their own. | all |
| REQ-CONS-201 | The firmware pin map and the wiring chart shall agree, checked mechanically. | - |
| REQ-CONS-202 | The consolidated build shall contain no blocking wait. | `LOOP_MAX_MS` |

## Out of scope

Deliberately not required, to keep the bench readable: sensor fault detection
(open or shorted sensor), switch debouncing beyond what the loop rate provides,
lamp failure detection, and any persistence across reset. Each would be a
reasonable next requirement; none of them is needed to answer the question this
experiment asks.

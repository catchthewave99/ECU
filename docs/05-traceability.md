# 05 - Traceability

Requirement -> limit -> SIL case -> HIL case -> result. One page that answers
"is anything untested?" and, later, "do SIL and HIL still agree?".

SIL case names are the log names printed by `tests/sil/run_sil_suite.sh`; each
one writes `build/sil-logs/<case>.txt`. The `func-*` cases run against the three
standalone sketches, the `cons-*` cases against the consolidated build. HIL
cases are in `docs/04-hil-plan.md`; the HIL column is empty because the rig has
not been run yet.

## Verified requirements

| Requirement | Limit variable | SIL case | HIL case | SIL result | HIL result |
| --- | --- | --- | --- | --- | --- |
| REQ-IF-001 | `TEMP_REPORT_TOL_C` | `cons-thermal-cold` | TC-HIL-01 | pass | |
| REQ-IF-002 | - | `tests/check_pinmap.sh`, all `cons-*` | TC-HIL-01/02/03 | pass | |
| REQ-IF-003 | - | `tests/check_pinmap.sh`, all `cons-*` | TC-HIL-01/02/03 | pass | |
| REQ-IF-004 | - | `tests/check_pinmap.sh` | rig wiring check | pass | |
| REQ-IF-005 | `LOOP_MAX_MS` | `cons-hazard`, `cons-hazard-busy` | TC-HIL-04 | 1.016 ms worst pass | |
| REQ-IF-006 | - | `cons-thermal-cold` | TC-HIL-01 | pass | |
| REQ-IF-007 | device limit | n/a (electrical) | bench inspection | - | |
| REQ-THRM-010 | `TEMP_REPORT_TOL_C` | `func-thermal-cold`, `cons-thermal-cold` | TC-HIL-01 | pass | |
| REQ-THRM-011 | `FAN_ON_C` | `func-thermal-hot`, `cons-thermal-hot` | TC-HIL-01 | pass | |
| REQ-THRM-012 | `FAN_OFF_C` | `func-thermal-hyst-off`, `cons-thermal-hyst-off` | TC-HIL-01 | pass | |
| REQ-THRM-013 | `FAN_ON_C`, `FAN_OFF_C` | `func-thermal-hyst-hold`, `cons-thermal-hyst-hold` | TC-HIL-01 | pass | |
| REQ-THRM-014 | `OVERTEMP_C` | `func-thermal-overtemp`, `cons-thermal-overtemp` | TC-HIL-01 | pass | |
| REQ-THRM-015 | `LOOP_MAX_MS` | `cons-hazard-busy` | TC-HIL-04 | pass | |
| REQ-BODY-020 | - | `func-body-head-on/off`, `cons-body-head-on/off` | TC-HIL-02 | pass | |
| REQ-BODY-021 | - | `cons-no-crosstalk` | TC-HIL-05 | pass | |
| REQ-BODY-022 | - | `func-body-turn-left/right`, `cons-body-turn-left/right` | TC-HIL-02 | pass | |
| REQ-BODY-023 | `FLASH_HALF_MS_MIN/MAX` | `func-body-turn-left`, `cons-body-turn-left` | TC-HIL-02 | pass | |
| REQ-SAFE-100 | `HAZARD_RESPONSE_MAX_MS` | `cons-hazard`, `cons-hazard-busy` | TC-HIL-03, TC-HIL-04 | 0.055 ms worst of 8 | |
| REQ-SAFE-101 | `HAZARD_PRESS_COUNT` | `cons-hazard`, `cons-hazard-busy` | TC-HIL-03 | 8 of 8 answered | |
| REQ-SAFE-102 | `FLASH_HALF_MS_MIN/MAX` | `func-hazard`, `cons-hazard` | TC-HIL-05 | pass | |
| REQ-SAFE-103 | - | `cons-hazard-vs-stalk` | TC-HIL-05 | pass | |
| REQ-SAFE-104 | `LOOP_MAX_MS` | `cons-hazard`, `cons-hazard-busy` | TC-HIL-04 | 1.016 ms | |
| REQ-CONS-200 | all | every `cons-*` case | TC-HIL-01..05 | pass | |
| REQ-CONS-201 | - | `tests/check_pinmap.sh` | - | pass | |
| REQ-CONS-202 | `LOOP_MAX_MS` | `cons-hazard-busy` + `grep -rn "delay(" consolidated/` | TC-HIL-04 | pass | |

## The defective build, for contrast

`tests/sil/run_sil_suite.sh --defects` runs the same cases against
`DEFECT_SHARED_PIN=1 DEFECT_BLOCKING_SENSOR=1`. The requirements it breaks:

| Requirement | Defect | Measured |
| --- | --- | --- |
| REQ-BODY-020, REQ-BODY-021, REQ-IF-004 | DEF-101 | headlamps follow the hazard switch; `check_pinmap.sh` reports pin 2 twice |
| REQ-SAFE-100 | DEF-102 | 161.319 ms worst of 8 presses, against a 100 ms limit (the median, 96.392 ms, is inside it) |
| REQ-SAFE-104, REQ-THRM-015, REQ-CONS-202 | DEF-102 | 162.821 ms worst pass of `loop()`, against a 20 ms limit |
| REQ-BODY-023 | DEF-102 | 487.53 ms flash half period, against a 330-370 ms band: a lamp cannot change state while the loop is stopped |

Six checks out of 79 fail. Everything else still passes - thermal control,
both stalks, the hazard lamps flashing in step. That is the point of the
experiment: the consolidated firmware looks like it works.

## Gaps

* No requirement, and therefore no case, for sensor fault detection, switch
  debounce, lamp failure detection or behaviour across reset - see the
  out-of-scope section of `docs/02-requirements.md`.
* REQ-IF-007 is an electrical property; SIL cannot see it and the NI rig does
  not measure current. It is checked by inspection against the wiring chart.
* Every HIL result is empty. Until the rig runs, every number in this
  repository is a simulation of an AVR, not a measurement of a board.

# 06 - Traceability matrix

Requirement -> limit -> SIL case -> HIL case -> result. This is the single page
that shows whether the SIL and HIL sides of the bench still agree.

Fill the SIL column from `build/sil-results/`, the HIL column from the NI
sequence. `diff` is HIL minus SIL. Anything outside the section 6 uncertainty
budget of `docs/05-stage2-hil-ni-plan.md` gets a DEF row.

## Verified requirements

| Requirement | Limit variable | SIL case | HIL case | SIL result | HIL result | diff |
| --- | --- | --- | --- | --- | --- | --- |
| REQ-IF-001/002 | - | TC-SIL-04 | TC-HIL-04 | pass | | |
| REQ-IF-003 | - | TC-SIL-03 | TC-HIL-10 | pass | | |
| REQ-IF-004 | - | TC-SIL-07 | TC-HIL-07 | pass | | |
| REQ-IF-005 | - | TC-SIL-02, TC-SIL-06 | TC-HIL-02, TC-HIL-06 | pass | | |
| REQ-IF-006 | - | all | all | pass | | |
| REQ-IF-007 | - | n/a (electrical) | bench inspection | - | | |
| REQ-SENS-010 | - | logged | TC-HIL-10 | - | | MAT conversion is suspect, see wiring group B5 |
| REQ-SENS-011 | `PULSE_81C_*`, `PULSE_22C_*` | TC-SIL-03 | TC-HIL-03, TC-HIL-10 | 2804.50 / 3404.50 us | | |
| REQ-SENS-012 | - | logged | TC-HIL-11 | - | | |
| REQ-SENS-013 | `SPEED_SETTLE_CYCLES` | TC-SIL-04 | TC-HIL-04 | pass | | |
| REQ-SENS-014 | logged only | - | - | n/a | n/a | DEF-003, DEF-015 |
| REQ-SPD-020 | `ECU_SPEED_TOL_PCT` | TC-SIL-04 | TC-HIL-04 | 796 / 2000 / 4404 rpm | | |
| REQ-SPD-021 | `SPEED_SETTLE_CYCLES` | TC-SIL-04 | TC-HIL-04 | pass | | |
| REQ-SPD-022 | - | not covered | not covered | **gap** | | needs a >10000 rpm stimulus |
| REQ-SPD-023 | - | TC-SIL-01 | TC-HIL-01 | pass | | |
| REQ-FUEL-025 | `PULSE_COUNT_*` | TC-SIL-07 | TC-HIL-07 | 197 pulses in 6 s | | |
| REQ-FUEL-026 | `PULSE_81C_*` | TC-SIL-03 | TC-HIL-03 | 2804.50 us | | HIL is the first real check of this |
| REQ-FUEL-027 | `PULSE_81C_*`, `PULSE_22C_*` | TC-SIL-03 | TC-HIL-10 | pass | | |
| REQ-FUEL-028 | `PULSE_ENRICH_*` | TC-SIL-08 | TC-HIL-08 | 3506.00 us | | |
| REQ-FUEL-029 | `PULSE_ABS_MAX_US` | TC-SIL-07 | TC-HIL-07, TC-HIL-16 | max 3506.38 us | | |
| REQ-FUEL-030 | trim authority | TC-SIL-03 (implicit) | TC-HIL-11 | +4.5 us at 2295 mV | | |
| REQ-FUEL-031 | `PULSE_81C_*` | TC-SIL-03 | TC-HIL-15 | pass | | |
| REQ-FUEL-032 | `PHASE_2000RPM_*` | TC-SIL-05 | TC-HIL-05 | 2461.94 us | | expect HIL nearer 2500 us: no 125 us bench tick |
| REQ-STATE-040 | `STATE_SETTLE_MS` | TC-SIL-01 | TC-HIL-01 | pass | | |
| REQ-STATE-041 | `STATE_SETTLE_MS` | TC-SIL-08 | TC-HIL-02 | state 2 | | |
| REQ-STATE-042 | `STATE_SETTLE_MS` | TC-SIL-02 | TC-HIL-02 | state 1 | | |
| REQ-STATE-043 | - | not covered in SIL | TC-HIL-02 | **gap** | | D5 is not instrumented, O8 |
| REQ-SAFE-050 | boolean | TC-SIL-06 | TC-HIL-06 | pass | | |
| REQ-SAFE-051 | **open** | - | TC-HIL-06 (data only) | n/a | | O-REQ-1, DEF-018 |

Configuration for the SIL column: `uno_baseline` and `tests/limits.env` at the
commit that added this file, 20/20 checks passing.

## Requirement gaps with no test

| Gap | Requirement | Next step |
| --- | --- | --- |
| O-REQ-1 | REQ-SAFE-051 trip latency undefined | define it in time or crank events, then give TC-HIL-06 an acceptance band |
| O-REQ-2 | loss of one crank signal | TC-BENCH-08 and TC-HIL-12 record the behaviour; write the requirement from the result |
| O-REQ-3 | sensor out of range | TC-HIL-13 records the behaviour |
| O-REQ-4 | trim convergence | TC-HIL-11 records the behaviour |
| O-REQ-5 | injector duty limiting | TC-HIL-14 records the behaviour |
| - | REQ-SPD-022 (>10000 rpm rejection) | the crank simulator stops at 8000 rpm, so this needs an external stimulus or a unit-level test |

## Defect coverage

Every DEF row in `docs/07-defect-log.md` that the bench build changes should be
demonstrated by a test, so a regression cannot pass silently.

| DEF | Demonstrated by | Status |
| --- | --- | --- |
| DEF-001, DEF-006, DEF-014 | the firmware build step of the SIL suite | covered |
| DEF-004 | TC-SIL-07, TC-HIL-16 | covered in SIL |
| DEF-008 | TC-SIL-08 | covered |
| DEF-011 | TC-SIL-03: a truncated pulse falls out of the band | indirect |
| DEF-002 | TC-BENCH-07 | bench only |
| DEF-019 | TC-BENCH-06, TC-HIL-02 | bench only |
| DEF-005 | TC-SIL-04 timing stability | indirect |
| DEF-009, DEF-018 | not covered | needs a trip-latency requirement first |
| DEF-010, DEF-013 | not covered | dormant paths; covered once the trim is exercised by TC-HIL-11 |
| DEF-003, DEF-007, DEF-012, DEF-015, DEF-016, DEF-017 | not covered | no observable effect on the bench |

# 03 - Bench requirements

These are the statements the bench verifies. They are reverse-engineered from
the firmware, because the repository shipped without requirements - so each one
carries a **basis** column saying whether it is a design intent that the code
clearly implements, or an observation of what the code happens to do. Anything
marked `as-built` should be confirmed by the design owner before it is treated as
production-level.

Numeric limits live in `tests/limits.env`, never here in prose; this document
names the limit variable. Requirement IDs are referenced by
`docs/06-traceability-matrix.md`.

## Interface requirements

| ID | Requirement | Basis |
| --- | --- | --- |
| REQ-IF-001 | The ECU shall accept a TDC signal on D2 and take action on its falling edge, one pulse per revolution. | intent |
| REQ-IF-002 | The ECU shall accept a pre-TDC signal on D3 and take action on its falling edge, one pulse per revolution, occurring before TDC by the crank advance. | intent |
| REQ-IF-003 | The ECU shall read MAT, CHT, MAP, lambda, TPS and throttle demand as 0 - 5 V single-ended analog inputs on A0 - A5 respectively, 10-bit, AVcc reference. | intent |
| REQ-IF-004 | The ECU shall drive the injector command on D4, active high, logic level, on the bench. | intent |
| REQ-IF-005 | The ECU shall indicate fuel-pump demand on D5, engine running on D7, cranking on D8 and overspeed on D9, active high. | as-built (DEV-002, DEV-006) |
| REQ-IF-006 | The ECU shall emit one telemetry record per 100 ms at 115200 8N1 in the documented column order. | as-built (DEV-005) |
| REQ-IF-007 | No ECU output shall be relied on to source or sink more than 20 mA, and total port current shall stay below 200 mA. | device limit |

## Sensor conversion requirements

| ID | Requirement | Limit | Basis |
| --- | --- | --- | --- |
| REQ-SENS-010 | MAT shall be reported as `0.4956 * counts - 308.81` degC. | - | as-built |
| REQ-SENS-011 | CHT shall be reported as `0.4108 * counts - 255.04` degC, and this value shall select the base injection interval. | `PULSE_81C_*`, `PULSE_22C_*` | intent |
| REQ-SENS-012 | Lambda shall be reported as `10 + 10 * counts / 1023` AFR. | - | as-built |
| REQ-SENS-013 | Each analog channel shall be filtered by a 5 % single-pole exponential filter per sample. | `SPEED_SETTLE_CYCLES` | as-built |
| REQ-SENS-014 | MAP and the A4 TPS channel shall be sampled and reported, and shall not affect any output. | logged only | as-built (DEF-003, DEF-015) |

## Speed requirements

| ID | Requirement | Limit | Basis |
| --- | --- | --- | --- |
| REQ-SPD-020 | Engine speed shall be computed from consecutive TDC intervals as `60e6 / dt_us`. | `ECU_SPEED_TOL_PCT` | intent |
| REQ-SPD-021 | Reported speed shall be the 5 % filtered value and shall settle inside tolerance within `SPEED_SETTLE_CYCLES` TDC events of a step. | `SPEED_SETTLE_CYCLES` | as-built |
| REQ-SPD-022 | A computed speed above 10000 rpm shall be rejected and the previous value retained. | - | intent |
| REQ-SPD-023 | Reported speed shall be 0 when no TDC interval is available. | - | intent |

## Fuelling requirements

| ID | Requirement | Limit | Basis |
| --- | --- | --- | --- |
| REQ-FUEL-025 | One injection shall be scheduled per pre-TDC edge. | `PULSE_COUNT_MIN/MAX` | intent |
| REQ-FUEL-026 | Injector on-time shall equal the commanded duration to within one Timer 1 tick (0.5 us) plus interrupt latency. | `PULSE_81C_*` | intent |
| REQ-FUEL-027 | The base duration shall be the `baseInt` entry for the nearest CHT breakpoint. | `PULSE_81C_*`, `PULSE_22C_*` | intent |
| REQ-FUEL-028 | While the engine state is STARTING, the commanded duration shall be 1.25 x the base duration. | `PULSE_ENRICH_*` | intent (defeated in the reference build, DEF-008) |
| REQ-FUEL-029 | A duration outside 200 - 8000 us shall not be scheduled; the event shall be counted and reported as `inj_skipped`. | `PULSE_ABS_MAX_US` | bench-added (DEF-004) |
| REQ-FUEL-030 | Closed-loop trim authority shall be limited to +/-100 us and shall be applied only in the IDLE state. | `PULSE_*` bands | intent |
| REQ-FUEL-031 | With lambda held at a constant stimulus, the trim contribution shall be stable within the TC-03 acceptance band for the duration of a test case. | `PULSE_81C_*` | as-built, see G8 |
| REQ-FUEL-032 | Injector rise shall lead TDC by the crank advance, within the stimulus quantisation. | `PHASE_2000RPM_*` | intent |

## State and protection requirements

| ID | Requirement | Limit | Basis |
| --- | --- | --- | --- |
| REQ-STATE-040 | The engine state shall be STOP when no TDC event has been seen in the last 500 ms. | `STATE_SETTLE_MS` | intent |
| REQ-STATE-041 | The engine state shall be STARTING on the first 500 ms window in which rotation is detected. | `STATE_SETTLE_MS` | intent |
| REQ-STATE-042 | The engine state shall become IDLE after three consecutive 500 ms windows with rotation. | `STATE_SETTLE_MS` | intent |
| REQ-STATE-043 | Fuel-pump demand shall be asserted in STARTING and IDLE and deasserted in STOP. | as-built | DEV-006 |
| REQ-SAFE-050 | The overspeed output shall assert while reported speed exceeds 4000 rpm and deassert below it. | boolean | intent |
| REQ-SAFE-051 | Overspeed trip latency shall be specified in time or crank events. | **open** | gap: the implementation counts 20 loop passes (DEF-018) |

## Open requirement gaps

| ID | Gap |
| --- | --- |
| O-REQ-1 | REQ-SAFE-051 has no numeric limit; the design does not define a trip latency. It cannot be verified as-is. |
| O-REQ-2 | No requirement covers behaviour on loss of the pre-TDC signal while TDC continues, or vice versa. The bench can produce this case; the design does not say what should happen. |
| O-REQ-3 | No requirement bounds sensor out-of-range behaviour (open circuit, short to 5 V, short to ground). The look-up table simply saturates at its end breakpoints. |
| O-REQ-4 | No requirement defines the convergence time or steady-state error of the closed-loop trim. |
| O-REQ-5 | No requirement defines injector duty-cycle limiting at high speed; at 8000 rpm a 3600 us pulse is 48 % of the cycle and nothing prevents overlap. |

These five are the honest answer to "does the Arduino behave the way the
software design says?" - for these, the design does not say. They are the first
items to close before calling anything production-level.

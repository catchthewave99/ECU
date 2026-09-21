# 06 - Defect log

Every defect found while consolidating the three functions onto one Uno, with
the measurement that found it and the measurement that closed it.

Evidence: `SIL` = measured by `tests/sil/run_sil_suite.sh` in simavr against the
real compiled firmware; `HIL` = measured on the NI rig; `review` = code
inspection only.

The engine firmware this repository started as has its own register, kept for
provenance in `reference/docs/07-defect-log-engine.md`.

## Consolidation defects

| ID | Severity | Finding | Evidence | Status |
| --- | --- | --- | --- | --- |
| DEF-101 | high | Both the hazard switch and the headlamp switch were assigned to D2. Each function was correct on its own board; neither noticed the other when they were merged. Pressing hazard turns the headlamps on, and the headlamp switch does nothing. | SIL | fixed: headlamp switch moved to D7, and `tests/check_pinmap.sh` now fails on any pin claimed twice |
| DEF-102 | **blocker (safety)** | Thermal control averaged 16 sensor readings 10 ms apart. The average is a fine idea; `delay(10)` between readings is not. Every other job - including the hazard lamps - stops for about 160 ms. | SIL | fixed: single non-blocking read on a 50 ms timer |

### DEF-101, measured

Hazard switch pressed 4 times, headlamp switch never touched
(`build/sil-logs/defect-no-crosstalk.txt`):

```
headlamp          level=0 rises=4 falls=4      <- should be rises=0
```

and with the headlamp switch closed (`build/sil-logs/defect-body-head-on.txt`)
the headlamps stayed off, because D2 was low.

After the fix, the same two cases give `rises=0` and `level=1`.

The static check catches the same fault without running anything:

```
FAIL  pinmap: PIN_HEADLAMP_SW is D2 in firmware, not documented as
      "headlamp switch" on D2 in docs/07-wiring-chart.md section 7
FAIL  pinmap: pin(s) used by more than one signal: 2
```

### DEF-102, measured

Eight hazard presses 500 ms apart, `HAZARD_RESPONSE_MAX_MS=100`
(`build/sil-logs/defect-hazard.txt`):

```
hazard_response_ms n=8    min=11.827 mean=88.924 median=96.392 max=161.319
loop_ms            edges=795                                   max=162.821
```

After the fix (`build/sil-logs/cons-hazard.txt`):

```
hazard_response_ms n=8    min=0.010 mean=0.028 median=0.025 max=0.055
loop_ms            edges=143765                               max=1.016
```

Two things are worth noting about the failing numbers. The **median** is 96 ms,
inside the limit - so a suite that reported an average, or that pressed the
switch once, could have passed this build. And the slowest pass of `loop()`,
162.8 ms, is almost exactly the response time, which is what pointed at the
blocking sensor read rather than at the hazard code itself.

## Why the fix is what it is

The sensor average was not removed because averaging is wrong; it was removed
because waiting is. The thermal function now reads the sensor once every 50 ms
and returns immediately, so the slowest pass of `loop()` is about 1 ms and the
hazard lamps are limited by the loop rate, not by another function's patience.

A reviewer should check exactly one thing in the fix: that no code path in
`consolidated/` can wait. `grep -rn "delay(" consolidated/` is the whole review.

## How this register is maintained

A HIL measurement that disagrees with SIL becomes a new row here, with the
evidence column set to `HIL`, and either a firmware change or a limit change in
`tests/limits.env`. No test tolerance moves without a row in this table - see
`docs/08-change-control.md`.

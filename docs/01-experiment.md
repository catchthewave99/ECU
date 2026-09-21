# 01 - The experiment

Question being tested: **can asynchronous software agents take the toil out of
consolidating several ECU functions onto one controller, without lowering the
safety standard?**

The toil is not typing code. It is tracing dependencies, updating pin maps and
tests, resolving integration conflicts, running regressions, diagnosing a
failure, fixing it, re-running, and writing down the evidence. This repository
is a bench small enough that all of that is visible in an afternoon.

Bench: an **Arduino Uno R3** is the consolidated ECU. An **NI cDAQ-9173** with
an **NI-9401** (digital) and an **NI-9263** (analog out) is the HIL simulator.
Before any hardware exists, the same tests run in SIL against the real compiled
firmware inside simavr.

## The three functions

Each one is first proven on its own board, then all three move onto one Uno.

| Function | What it does | Why it is in the experiment |
| --- | --- | --- |
| Thermal control | Reads a temperature sensor, runs a cooling fan, lights an over-temperature lamp | continuous control, analog input, the function that likes to take its time |
| Body control | Headlamp switch, and a turn-signal stalk that flashes one lamp | discrete I/O and timing; the "boring" function that generates the conflicts |
| Hazard warning | Hazard switch makes both turn lamps flash, **within 100 ms** | the safety-critical requirement that must never regress |

Body control and hazard warning share the two turn lamps, which is why
consolidating them is not just copying two files into one folder.

## The two defects that consolidation shipped with

Both are real integration mistakes, kept in the tree behind build flags so the
failing build can be rebuilt on demand as evidence (`consolidated/ecu.h`):

| Flag | Defect | Effect |
| --- | --- | --- |
| `DEFECT_SHARED_PIN` | The headlamp switch kept its old pin, D2, which the hazard switch also uses (DEF-101) | pressing hazard turns the headlamps on; the headlamp switch does nothing |
| `DEFECT_BLOCKING_SENSOR` | Thermal control still averages 16 sensor readings 10 ms apart (DEF-102) | every job stops for about 160 ms, so the hazard lamps miss their 100 ms deadline |

The important part: **the consolidated firmware still works.** Lamps light,
fan runs, telemetry streams. Only the safety test notices DEF-102, and it
notices because the number it checks against was frozen before consolidation
started.

## Repository shape

```
functions/thermal/        one sketch, thermal control only
functions/body/           one sketch, body control only
functions/hazard/         one sketch, hazard warning only
consolidated/             all three on one Uno - the configuration under test
tests/limits.env          every acceptance number, including HAZARD_RESPONSE_MAX_MS=100
tests/sil/                simavr harness and suite: per-function and consolidated
tests/hil/ni_sequence.py  the same cases against the NI rig, same limits file
tests/check_pinmap.sh     the wiring chart and the firmware pin map must agree
docs/                     requirements, wiring, traceability, defect log
reference/                the engine firmware this repo started as, kept for provenance
```

## Firmware rules that keep it readable

* One sketch per function, one `setup()` and one `loop()`, around 150 lines.
* Every pin is a named constant at the top of the file, and
  `tests/check_pinmap.sh` compares those constants with the wiring chart.
* No blocking calls. Each function is an `xxxUpdate()` that returns quickly;
  `loop()` calls the update functions in turn and nothing else.
* Time is `millis()` arithmetic, never `delay()`.
* Integers only - no floating point, no look-up tables, no learning algorithms.
* No interrupts. The hazard requirement is met by keeping every job short, not
  by making one job special; that is easier to read and easier to test.
* Comments say what the code is for in plain English, not what the C does.

## The safety requirement, stated so a test can fail on it

> **REQ-SAFE-100**: from the hazard switch closing to the first hazard lamp
> turning on, no more than 100 ms shall elapse, at any temperature, in any
> lighting state, on a build with all three functions enabled.

Measured in SIL from the emulator's pin trace and in HIL from an NI-9401 line
driving the switch and reading the lamp. One number,
`HAZARD_RESPONSE_MAX_MS=100` in `tests/limits.env`, for both.

A single press is not enough evidence: a job that blocks for 160 ms only delays
the presses that land while it is busy. The suite presses the switch
`HAZARD_PRESS_COUNT` times and every press must be answered in time.

## What was done, in order

1. Froze `tests/limits.env` and the requirements, including REQ-SAFE-100.
2. Built and tested the three functions separately: all pass.
3. Consolidated, carrying the pin conflict and the blocking routine across.
4. Ran the **unchanged** suite: DEF-101 and DEF-102 both fail it.
5. Remediated both, re-ran the unchanged suite: 100 % pass.
6. Collected the evidence: suite logs in `build/sil-logs/`,
   `docs/05-traceability.md`, `docs/06-defect-log.md`.

An engineer still reviews and approves every safety-relevant change; the agent's
job is steps 3 to 6, not the decision in step 1.

# ECU consolidation bench (Arduino Uno R3)

Three car functions - **thermal control**, **body control** and a
safety-critical **hazard warning** - first as three separate controllers, then
consolidated onto one Arduino Uno R3, with a test suite that decides whether the
consolidation was allowed.

The interesting part is the failure. Consolidating the three functions
introduces two realistic integration defects, and one of them is a **safety
regression**: the firmware still looks like it works, but the hazard lamps
answer the switch in 161 ms against a 100 ms requirement. The suite catches it,
the fix is in the firmware rather than in the limit, and the same unchanged test
decides when it is fixed.

Why it exists: `docs/01-experiment.md`.

## Layout

| Path | What it is |
| --- | --- |
| `functions/thermal/` | Standalone controller: fan, over-temperature warning. |
| `functions/body/` | Standalone controller: headlamps, turn signals. |
| `functions/hazard/` | Standalone controller: hazard lamps. Safety-critical. |
| `consolidated/` | **Configuration under test.** All three on one Uno. Two defects behind compile flags. |
| `tests/limits.env` | Every acceptance limit, once. SIL and HIL both read it. |
| `tests/sil/` | simavr harness and the regression suite. Runs on a laptop, no hardware. |
| `tests/hil/` | NI CompactDAQ sequence (cDAQ-9173, NI-9401, NI-9263). Not yet run. |
| `tests/check_pinmap.sh` | Fails if the firmware pin constants and the wiring chart disagree. |
| `docs/` | Experiment, requirements, test plans, wiring, traceability, defects, change control. |
| `reference/` | The single-cylinder engine ECU this repository started as, kept for provenance. Not built, not tested. |

## Read in this order

1. `docs/01-experiment.md` - what the experiment is and how the firmware is organised
2. `docs/02-requirements.md` - what "correct" means
3. `docs/03-test-plan.md` - the SIL suite and how each number is measured
4. `docs/04-hil-plan.md` - NI channel list and the hardware-timed safety measurement
5. `docs/05-traceability.md` - requirement to limit to case to result
6. `docs/06-defect-log.md` - the two consolidation defects, measured before and after
7. `docs/07-wiring-chart.md` - **the breadboard wiring chart**
8. `docs/08-change-control.md` - the rules that stop a limit being widened to pass

## Build and test

```bash
# toolchain
arduino-cli core install arduino:avr
sudo apt install simavr libsimavr-dev libelf-dev     # SIL only

# the gate: builds all four sketches, runs them in simavr, checks every limit
tests/sil/run_sil_suite.sh

# the demonstration: also build the defective firmware and show what it fails
tests/sil/run_sil_suite.sh --defects
```

Logs land in `build/sil-logs/`, one file per case.

```bash
# flash and monitor (needs the board)
arduino-cli board list
arduino-cli upload --fqbn arduino:avr:uno -p /dev/ttyACM0 consolidated
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

Minimum hardware for the safety case: one push button and two LEDs with 220 Ohm
resistors - wiring group A in `docs/07-wiring-chart.md`.

## Bench interface

Serial, 115200 8N1, one telemetry record every 200 ms:

```
t_ms  coolantC  fan  overtemp  head  turnL  turnR  hazard  loop_max_ms
```

`loop_max_ms` is the slowest pass of `loop()` since reset - the number that
makes the hazard deadline achievable rather than lucky.

## The defective build

```bash
arduino-cli compile --fqbn arduino:avr:uno consolidated \
  --build-property "compiler.cpp.extra_flags=-DDEFECT_SHARED_PIN=1 -DDEFECT_BLOCKING_SENSOR=1"
```

* `DEFECT_SHARED_PIN` puts the headlamp switch back on D2, where the hazard
  switch already lives.
* `DEFECT_BLOCKING_SENSOR` restores the thermal function's averaging loop, which
  waits 10 ms between sixteen readings and stops everything else for 160 ms.

Both are the kind of thing consolidation produces and neither shows up in a
build log. See `docs/06-defect-log.md`.

## Status

* SIL: 53 checks, passing, against the corrected consolidated build.
* Defective build: 6 of 79 checks fail, including REQ-SAFE-100.
* Bench: nothing has been flashed or wired yet.
* HIL: written against the lab rig, never executed - the DAQmx kernel modules
  do not load on a cloud VM and the chassis is USB-attached to the bench
  machine (`docs/04-hil-plan.md` section 5).

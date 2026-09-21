# 03 - Test plan (SIL)

Everything in this document runs on a laptop with no hardware attached. It
compiles the real firmware with `arduino-cli` and runs the resulting AVR binary
inside simavr, driving the same pins the NI rig will drive later.

```bash
tests/sil/run_sil_suite.sh              # the gate: must be 0 failures
tests/sil/run_sil_suite.sh --defects    # also rebuild the defective firmware
                                        # and show which checks it fails
```

The whole suite takes well under a minute. Per-case logs land in
`build/sil-logs/`, one file per case, and every FAIL line names its log.

## What the suite does, in order

**Stage 1 - each function on its own board.** `functions/thermal`,
`functions/body` and `functions/hazard` are built and tested separately. This is
the known-good baseline: the behaviour the consolidation is not allowed to
change.

**Stage 2 - all three on one board.** `consolidated/` is built and put through
*the same test functions with the same limits*, plus two cases that only exist
once the functions share a board:

* the hazard switch must not touch the headlamps (DEF-101 territory), and
* the hazard deadline must hold while the fan is running and the stalk is on
  (DEF-102 territory).

Then `tests/check_pinmap.sh` compares the firmware's pin constants with the
wiring chart, and fails if any pin is claimed by two signals.

**Stage 3, optional - the defective build.** `--defects` rebuilds
`consolidated/` with `DEFECT_SHARED_PIN=1 DEFECT_BLOCKING_SENSOR=1` and runs the
same cases again. Its failures are evidence for `docs/06-defect-log.md`, not a
gate, so they are reported and then excluded from the suite result.

## How the measurements are made

The harness (`tests/sil/sil_runner.c`) sits on simavr's pin IRQs, so every
number comes from the firmware's own I/O, not from instrumentation added to the
firmware:

| Measurement | How |
| --- | --- |
| lamp / fan / warning state | level of the AVR port bit at the end of the run |
| flash period | gaps between rising edges of a lamp |
| hazard response | time from the harness driving D2 high to the first lamp edge |
| slowest pass of `loop()` | largest gap between two D13 heartbeat edges |
| reported temperature | column 2 of the telemetry line on the UART |

The temperature stimulus is applied as millivolts into the ADC, exactly as the
NI-9263 will apply it.

Two details worth knowing before reading a result:

* **Presses, plural.** The hazard case presses the switch `HAZARD_PRESS_COUNT`
  times. A blocking job only delays the presses that land while it is busy, so
  one press can pass a firmware a driver would fail. Every press is checked,
  and a press that never lit a lamp is scored 9999 ms rather than dropped.
* **Stimulus resolution.** The ADC quantises to about half a degree, so a case
  that tests a switching point drives `TEMP_STIM_MARGIN_C` past it. That is the
  bench's resolution, not slack in the requirement.

## Limits

No number appears in the suite. It sources `tests/limits.env`, which the NI
sequence also reads. Changing a limit is a one-file diff, reviewed under
`docs/08-change-control.md`.

## Before the hardware arrives, and after

SIL proves logic and ordering. It does not prove wiring, levels, ADC scaling
against a real sensor, or supply behaviour. When the board is on the bench:

1. Build the wiring in `docs/07-wiring-chart.md`, groups A then B then C.
2. `arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:uno consolidated`
3. `arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200` and check the
   telemetry header appears.
4. Record the measured 5 V rail voltage and ambient temperature in the test
   record - the ADC is ratiometric to that rail.
5. Press each switch once and watch the LEDs before running anything automated.
6. Then connect the NI rig and run `docs/04-hil-plan.md`.

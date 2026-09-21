# ECU

Single-cylinder, port-injected engine controller firmware, and a SIL-to-HIL test
bench built around it.

The repository holds two firmware configurations and the documentation that ties
them to a test plan:

| Path | What it is |
| --- | --- |
| `ECU_*.ino` | **Reference firmware**, as received. Targets an Arduino Mega 2560. Not modified; findings about it are logged, not fixed here. |
| `uno_baseline/` | **Configuration under test.** Arduino Uno R3 port with an on-board crank simulator, so the bench can stimulate itself. |
| `tests/limits.env` | Every acceptance limit, once. Both stages read it. |
| `tests/sil/` | simavr harness and the Stage 1 SIL regression suite. |
| `tests/hil/` | NI CompactDAQ sequence skeleton for Stage 2 (not yet run). |
| `tests/check_pinmap.sh` | Fails if the wiring chart and the firmware pin constants disagree. |
| `docs/` | Assessment, plan, requirements, test plans, wiring chart, defect log, change control. |

## Read in this order

1. `docs/00-repo-assessment.md` - what the firmware does and whether it suits the bench
2. `docs/01-plan-gaps-and-pitfalls.md` - gaps, pitfalls, and the 10-step build order
3. `docs/02-uno-port-design.md` - why the Mega sketch cannot be flashed to an Uno, and what changed
4. `docs/03-requirements.md` - what "correct" means, numerically
5. `docs/04-stage1-vv-plan.md` - SIL and bench test cases
6. `docs/05-stage2-hil-ni-plan.md` - NI channel list, measurement methods, uncertainty budget
7. `docs/06-traceability-matrix.md` - requirement to limit to case to result
8. `docs/07-defect-log.md` - findings in the reference firmware and port deviations
9. `docs/08-wiring-chart.md` - **the breadboard wiring chart**
10. `docs/09-change-control.md` - the rules that stop SIL/HIL drift

## Build, test, flash

```bash
# toolchain
arduino-cli core install arduino:avr
arduino-cli lib install MegunoLink
sudo apt install simavr libsimavr-dev libelf-dev     # SIL only

# build
arduino-cli compile --fqbn arduino:avr:uno uno_baseline

# Stage 1 SIL suite: builds the firmware, runs it in simavr, checks the limits
tests/sil/run_sil_suite.sh                            # logs in build/sil-results/

# flash and monitor (needs the board)
arduino-cli board list
arduino-cli upload --fqbn arduino:avr:uno -p /dev/ttyACM0 uno_baseline
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

Minimum hardware to run the whole control path: two jumper wires, one 10 kOhm
pot, one LED and a 220 Ohm resistor - wiring group A in
`docs/08-wiring-chart.md`.

## Bench interface

Serial, 115200 8N1. Commands: `R<rpm>` crank speed, `A<deg>` injection advance,
`?` status. Telemetry, one record per 100 ms:

```
t_ms  MAT  CHT  MAP  AFR  TPS  RPM  pulse_us  correction  state
```

`state`: 0 = STOP, 1 = IDLE, 2 = STARTING.

## Status

* Stage 1 SIL: TC-SIL-00 to TC-SIL-08, 20 checks, passing.
* Stage 1 bench: not run - no hardware has been flashed or wired yet.
* Stage 2 HIL: planned against the lab rig (cDAQ-9173, NI-9401, NI-9263); the
  sequence in `tests/hil/` has never been executed against hardware.

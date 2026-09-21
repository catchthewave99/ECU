# 04 - HIL plan (NI cDAQ-9173)

The HIL stage runs the same cases as `docs/03-test-plan.md` against the real
Uno, with the NI rig playing the part of the car. Same firmware, same
`tests/limits.env`, different measurement path - which is exactly what makes a
disagreement between SIL and HIL interesting rather than confusing.

Implementation: `tests/hil/ni_sequence.py`.

## 1. Rig

| Slot | Module | Use |
| --- | --- | --- |
| 1 | NI-9401 | 8 lines, 5 V TTL. Lines 0-3 drive the four switches; lines 4-7 read four outputs. |
| 2 | NI-9263 | `ao0` supplies the temperature sensor voltage on A0. |
| - | cDAQ-9173 | USB chassis; provides the sample clock and start trigger the timing case needs. |

The NI-9401 sets direction **per nibble**, not per line. That constraint chose
the pin map: four switches in the low nibble, four observed outputs in the high
nibble.

| NI line | Direction | Uno pin | Signal |
| --- | --- | --- | --- |
| line0 | out | D2 | hazard switch |
| line1 | out | D4 | turn stalk left |
| line2 | out | D5 | turn stalk right |
| line3 | out | D7 | headlamp switch |
| line4 | in | D8 | left lamp |
| line5 | in | D9 | right lamp |
| line6 | in | D11 | cooling fan |
| line7 | in | D12 | over-temperature warning |
| `ao0` | out | A0 | coolant temperature, 10 mV per degree C |

Two ECU outputs do not fit: the headlamps (D10) and the heartbeat (D13). They
are observed instead in the telemetry line over USB serial, which is also how
`loop_max_ms` is read. Timing-critical signals are never taken from telemetry.

## 2. How the safety case is measured

REQ-SAFE-100 allows 100 ms. A software-timed loop on a desktop OS can miss by
tens of milliseconds, which would make the measurement a property of the PC.
So the hazard case is hardware-timed:

* the switch waveform is written to a buffered DO task at 10 kHz;
* the DI task is clocked from the same `/cDAQ1/do/SampleClock` and started on
  `/cDAQ1/do/StartTrigger`;
* the press and the lamp that answers it are therefore in one sample index
  space, and the latency is a sample count: 0.1 ms resolution, no OS in the
  path.

The switch is pressed `HAZARD_PRESS_COUNT` times, and every press is checked -
see `docs/03-test-plan.md` for why one press is not evidence.

The other cases are on-demand reads: the fan, the warning lamp and the flasher
have no requirement finer than tens of milliseconds.

## 3. Cases

| Case | Verifies | SIL twin |
| --- | --- | --- |
| TC-HIL-01 | thermal: fan on/off points, warning lamp, hysteresis, reported temperature | `cons-thermal-*` |
| TC-HIL-02 | body: headlamps follow their own switch, stalk flashes one lamp, flash period | `cons-body-*` |
| TC-HIL-03 | **REQ-SAFE-100**: hazard response over repeated presses | `cons-hazard` |
| TC-HIL-04 | the same deadline with the fan running and the stalk on | `cons-hazard-busy` |
| TC-HIL-05 | integration: one switch drives one function; hazard beats the stalk | `cons-no-crosstalk`, `cons-hazard-vs-stalk` |

## 4. Bring-up order

1. `python3 tests/hil/ni_sequence.py --check-limits` - no hardware needed.
2. `python3 tests/hil/ni_sequence.py --list` - confirm device names and slots
   match `DIO`, `AO` and `CHASSIS` in the script; rename in NI MAX if not.
3. Set the NI-9401 nibble directions **before** connecting anything. An NI line
   configured as an output against an Uno output is a contention fault.
4. Fit the clamp of note N3 in `docs/07-wiring-chart.md` before the NI-9263 is
   connected to A0. The module can output 10 V; the pin is rated to 5.5 V.
5. Run TC-HIL-01 first - it is the case that fails harmlessly if a channel is
   miswired. Then TC-HIL-03.
6. Record the measured 5 V rail voltage with the run.

## 5. Why this has not been run yet

The chassis is USB-attached to the bench machine, and the NI-DAQmx Linux driver
cannot be brought up on a cloud VM: DKMS builds `nipalk.ko` against headers
that do not match the running kernel, so `nipal` never starts and even NI
simulated devices fail. `tests/hil/ni_sequence.py` has therefore been checked
for syntax and limit parsing only.

Two ways to run it:

* on the bench machine directly, or
* from elsewhere against an **NI gRPC Device Server** on the bench machine, via
  `nidaqmx.Task(grpc_options=...)`.

Expect the first bring-up to correct at least the device names, and possibly
the sample-clock and start-trigger terminal strings in section 2 - those are
the lines to check first if a task fails to start.

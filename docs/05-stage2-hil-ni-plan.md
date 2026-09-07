# 05 - Stage 2 HIL plan (NI CompactDAQ)

Stage 2 replaces software stimulus with NI analog and digital output, and
software observation with NI measurement, so the claim "the Arduino behaves
correctly in the physical world" rests on instrumentation rather than on the
Arduino reporting on itself.

The firmware does not change between stages. Only the two crank jumpers and the
sensor pots being replaced are unplugged (`docs/08-wiring-chart.md` section 5).

## 1. Rig

| Item | Part | Capability used here |
| --- | --- | --- |
| Chassis | NI cDAQ-9173 | 4 slots, USB; provides the chassis counter/timer resources used for injector timing |
| Digital | NI-9401 | 8 lines, 5 V TTL, bidirectional **per nibble** (ch0-3 and ch4-7 share a direction) |
| Terminal block | NI TB-9924 | 25-pin D-SUB breakout for the NI-9401 |
| Analog out | NI-9263 | 4 channels, +/-10 V |
| Software | Python + `nidaqmx` | driver must be on the machine physically connected to the chassis |

Channels are addressed as `cDAQ1Mod<slot>/...`, e.g. `cDAQ1Mod1/port0/line0`
and `cDAQ1Mod2/ao0`. Enumerate with
`nidaqmx.system.System.local().devices` rather than assuming; rename in NI MAX
if needed.

### What this rig can and cannot do

| Consequence | Detail |
| --- | --- |
| 4 AO channels, 6 analog ECU inputs | Two channels stay on breadboard pots. Priority: CHT and lambda (they drive fuelling), then MAT and throttle demand. MAP and the A4 TPS channel are logged-only anyway (REQ-SENS-014), so they keep their pots/dividers. |
| NI-9263 is +/-10 V | An Uno analog pin is rated to Vcc + 0.5 V. Commanding 10 V destroys it. Clamp per note N3 **and** limit in software (section 5). This is the single biggest hazard in the rig. |
| NI-9401 direction is per nibble | Two crank outputs must share a nibble with two spares; that leaves only 4 input lines for 6 ECU outputs. See section 2. |
| **No AI module in the rig** | The 5 V ADC reference, the AO read-back and any injector-current shunt cannot be measured with NI. Use a DMM and record the value manually (open item O5), or add a C Series AI module. |
| Counters live in the chassis | The NI-9401 is the interface to them, so injector pulse width can be measured by two-edge separation at the chassis timebase rather than by sampling. Verify slot support before wiring: on cDAQ, counter and correlated-DIO tasks are only available for modules in slots that support them, so if a counter task refuses to start, move the NI-9401 to slot 1 (O2). |

## 2. Channel assignment

Fixed by the per-nibble direction constraint of the NI-9401:

| NI channel | Direction | Uno pin | Signal | Interface |
| --- | --- | --- | --- | --- |
| `Mod1/port0/line0` | out | D2 | crank TDC stimulus | 1 kOhm series; replaces the D13 jumper |
| `Mod1/port0/line1` | out | D3 | crank pre-TDC stimulus | 1 kOhm series; replaces the D12 jumper |
| `Mod1/port0/line2` | out | - | spare (reset or fault injection) | |
| `Mod1/port0/line3` | out | - | spare | |
| `Mod1/port0/line4` | in | D4 | **injector command** | counter input, primary measurement |
| `Mod1/port0/line5` | in | D9 | overspeed | DI |
| `Mod1/port0/line6` | in | D7 | engine running | DI |
| `Mod1/port0/line7` | in | D8 | cranking | DI |
| `Mod2/ao0` | out | A1 | CHT | 0 - 5 V only, clamp per N3 |
| `Mod2/ao1` | out | A3 | lambda | 0 - 5 V only, clamp per N3 |
| `Mod2/ao2` | out | A0 | MAT | 0 - 5 V only, clamp per N3 |
| `Mod2/ao3` | out | A5 | throttle demand | 0 - 5 V only, clamp per N3 |

Not connected to NI, and how they are covered instead:

| Uno pin | Signal | Covered by |
| --- | --- | --- |
| A2 | MAP | breadboard pot, telemetry only (REQ-SENS-014) |
| A4 | TPS channel | fixed divider, telemetry only (REQ-SENS-014) |
| D5 | fuel-pump demand | LED2 plus telemetry `state`; D5 is redundant with D7/D8 (REQ-STATE-043) |
| D6 | throttle PWM | LED3 plus telemetry `TPS` |
| Uno 5V | ADC reference | DMM, recorded per test case (note N2) |
| group D shunt | injector current | DMM or scope, optional |

Keep the LEDs fitted when NI is connected; they are the local check that the NI
channel and the pin agree.

## 3. Measurement methods

| Quantity | Method | Expected uncertainty |
| --- | --- | --- |
| Injector pulse width | counter, two-edge separation, rising to falling on line4 | chassis timebase quantisation, tens of nanoseconds - negligible against 2800 us |
| Injector period | counter, period measurement, rising to rising | as above |
| Injector phase | correlate the DO edge that generates TDC with the counter timestamp of the D4 rising edge, both on the chassis timebase | limited by DO update timing, target < 20 us |
| Crank stimulus period | generated from the chassis clock by a hardware-timed DO task, so it is the reference | device timebase spec |
| Discrete state timing | DI change detection, or DI polled at >= 1 kHz against a 500 ms task | < 5 ms |
| Analog stimulus level | commanded on the NI-9263; **verified with a DMM at the Uno pin**, no AI available | DMM accuracy plus the divider/clamp loading |
| ADC reference | DMM on the Uno 5V pin, once per case | +/-10 mV |

Use a **hardware-timed** DO task for the crank waveform. A software-timed loop
gives millisecond-scale jitter, which at 2000 rpm (30 ms/rev) is percent-level
speed noise and will show up as ECU speed error that is really stimulus error.

## 4. HIL test cases

Each is the twin of a SIL case, reads the same limit variables, and produces a
number for the same row of `docs/06-traceability-matrix.md`.

| Case | Twin of | Stimulus (NI) | Measurement | Acceptance |
| --- | --- | --- | --- | --- |
| TC-HIL-01 | TC-SIL-01 | no crank; AO at defaults | counter on line4, DI line5-7 | no injector edges in 3 s; D7/D8 low |
| TC-HIL-02 | TC-SIL-02 | crank 1200 rpm | DI change detection | cranking high within one 500 ms window, running high within `STATE_SETTLE_MS` |
| TC-HIL-03 | TC-SIL-03 | crank 2000 rpm; ao0 4.000 V then 3.300 V | counter pulse width, n >= 30 | mean in the `PULSE_81C_*` / `PULSE_22C_*` bands; report the standard deviation |
| TC-HIL-04 | TC-SIL-04 | crank 800, 2000, 4400 rpm | counter period; telemetry `RPM` | measured period within `STIMULUS_SPEED_TOL_PCT` of commanded; reported rpm within `ECU_SPEED_TOL_PCT` |
| TC-HIL-05 | TC-SIL-05 | crank 2000 rpm, 30 deg advance | DO edge to counter timestamp | in `PHASE_2000RPM_*`, and expected to be tighter than SIL: the NI stimulus is not quantised to the 125 us bench tick |
| TC-HIL-06 | TC-SIL-06 | crank 6000 rpm | DI line5 timestamped | asserts; record the latency as data against O-REQ-1 rather than pass/fail |
| TC-HIL-07 | TC-SIL-07 | crank 2000 rpm, 6 s | counter, all pulses | max <= `PULSE_ABS_MAX_US`; count in `PULSE_COUNT_*` |
| TC-HIL-08 | TC-SIL-08 | crank 2000 rpm, first 1.2 s | counter pulse width | mean in `PULSE_ENRICH_*` |
| TC-HIL-10 | new | ao0 sweep 0.5 - 4.5 V in 0.1 V steps, crank 2000 rpm | counter pulse width per step | the whole `baseInt` table is reproduced; step boundaries within 1 ADC count of the breakpoints |
| TC-HIL-11 | new | ao1 step 2.295 -> 2.500 V, crank 2000 rpm, IDLE | counter pulse width vs time | trim moves <= 100 us and settles; feeds O-REQ-4 |
| TC-HIL-12 | new | crank 2000 rpm, then stop the pre-TDC line only | counter, DI | records actual behaviour; feeds O-REQ-2 |
| TC-HIL-13 | new | ao0 to 0 V and to 5 V | counter pulse width | records look-up saturation; feeds O-REQ-3 |
| TC-HIL-14 | new | crank 8000 rpm, ao0 3.3 V | counter pulse width and period | reports injector duty; feeds O-REQ-5 |
| TC-HIL-15 | new | repeat TC-HIL-03, n = 30, power-cycling between runs | counter pulse width | mean, standard deviation, range; no drift across power cycles |
| TC-HIL-16 | new | line2 spare driving the Uno RESET through 1 kOhm | counter on line4 | characterises the first-pulse transient of DEF-004 on real hardware |

TC-HIL-10 to TC-HIL-16 only make sense with real instrumentation, and they are
the cases most likely to change the requirements. That is the intended outcome.

## 5. Sequencer

`tests/hil/ni_sequence.py` is a skeleton: it reads `tests/limits.env`, clamps
every AO command to 0 - 5 V, drives the crank waveform from a hardware-timed DO
task, and measures the injector with a counter task. It has **not been executed**
- there is no NI-DAQmx driver on a cloud VM, so it must be dry-run on the rig
machine (Step 7 of `docs/01-plan-gaps-and-pitfalls.md`) before it is trusted.

**Where it has to run.** The DAQmx Linux driver installs on a cloud VM but
cannot be brought up: DKMS builds `nipalk.ko` against the installed headers, the
VM runs a kernel with no matching headers, so `nipal.service` fails and every
API call returns `-200090`. NI simulated devices need `nipal` too, so they are no
help. The chassis is USB-attached to the bench in any case. That leaves two
options: run the sequence on the bench machine, or run the **NI gRPC Device
Server** there and point remote Python at it with
`nidaqmx.Task(grpc_options=...)`. Pick one before Step 7 - it decides whether
this file stays a local script or gains a connection argument.

Two rules for whatever sequencer you end up using:

* **Limits are read, never copied.** `tests/limits.env` is plain shell
  assignments so any language can parse it; the Python reader is in the
  skeleton, and LabVIEW/TestStand can read the same file and split on `=`.
* **AO is clamped in software as well as in hardware.** The NI-9263 can output
  10 V. `clamp_volts()` exists so a units bug in a test cannot reach the pin.

## 6. Measurement uncertainty budget

Fill this in during Step 7; it is what turns a measurement into a certifiable
one.

| Source | Typical magnitude | Applies to | Handling |
| --- | --- | --- | --- |
| Uno clock error | Uno R3 clocks the 328P from a ceramic resonator, order +/-0.5 % | every ECU-generated interval | measure once against the chassis timebase at a known crank period; record as a scale factor and either correct or widen the band explicitly |
| Uno clock drift with temperature | manufacturer spec | long runs | record ambient per case |
| Chassis timebase | device spec, ppm | all NI timing | negligible against the above; state it anyway |
| Counter edge placement | one timebase period | pulse width, phase | quantify from the device spec |
| DO update timing | hardware-timed task: sample clock period | crank stimulus, phase reference | keep the DO sample clock at least 10x the required edge resolution |
| ECU interrupt latency | microseconds, plus ISR contention | pulse start, phase | bound it by the SIL-to-HIL difference, since SIL has an exact clock and no analog noise |
| ADC reference (5 V rail) | +/-2 - 3 % on USB power | every analog conversion | DMM per case; it is a scale error, not noise |
| NI-9263 accuracy and settling | device spec | analog stimulus | no AI to read back: verify with a DMM at the pin during setup |
| Clamp and filter loading | 1 kOhm + 100 nF is a 100 us time constant; clamp diodes add leakage | fast AO steps, DC level | wait >= 1 ms after a step; check the DC level at the pin with the clamp fitted |
| Bench tick quantisation | 125 us at 8 kHz | SIL only | absent in HIL, hence TC-HIL-05 should be tighter than TC-SIL-05 |

## 7. Reconciliation report

Per case, record the SIL value, the HIL value, the difference and the difference
allowed by section 6. Classify every out-of-budget difference:

1. **Firmware defect** - raise a DEF row, fix, re-run both stages.
2. **Unbudgeted uncertainty** - add the source to section 6, keep the limit.
3. **Wrong limit** - change `tests/limits.env`, with a DEF row explaining why
   (`docs/09-change-control.md`).
4. **Requirement gap** - close an O-REQ item in `docs/03-requirements.md`, then
   add the case to both suites.

The bench exists so that these four are distinguishable and the third cannot
happen quietly.

## 8. Open items

| ID | Item | Status |
| --- | --- | --- |
| O2 | Confirm the NI-9401 slot supports counter / correlated-DIO tasks on the cDAQ-9173 | verify on the rig; move to slot 1 if a counter task fails to start |
| O4 | Confirm NI-9263 behaviour at power-up, task stop and USB disconnect | if any of those can leave a channel at a non-zero level, the note N3 clamp is load-bearing, not belt-and-braces |
| O5 | No AI module: decide DMM-and-record versus adding a C Series AI module | affects whether the 5 V reference and injector current are in the certifiable record or a manual note |
| O7 | Isolation and grounding: cDAQ module COM to Arduino GND, bonded at the single point of note N1 only | check for a second path through the two USB grounds if the chassis and the Uno share a host PC |
| O8 | D5 and D6 are not instrumented (only 4 input lines) | accept, or move a spare output line to input by reassigning the nibble |

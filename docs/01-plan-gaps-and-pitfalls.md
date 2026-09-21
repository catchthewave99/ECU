# 01 - Gaps, pitfalls, and the implementation plan

This reviews the Stage 1 / Stage 2 plan as proposed, lists what will bite, and
gives the build order actually followed in this repository.

## 1. What the plan gets right

* The item under test is a real controller whose every output is timed off a
  crank edge, so it can be stimulated and measured purely electrically.
* Splitting stimulus (SIL, software-generated) from stimulus (HIL, NI-generated)
  while keeping the *same* firmware and the *same* acceptance limits is exactly
  the comparison that exposes SIL/HIL drift.
* Starting with a few signals is the right instinct. The bench needs four inputs
  and two outputs to prove the whole control path (see `docs/00-repo-assessment.md`
  section 2); the rest is telemetry and can be added later.

## 2. Gaps in the plan as stated

| # | Gap | Why it matters | Handled by |
| --- | --- | --- | --- |
| G1 | "Flash this firmware to the Uno" is not possible. The sketch targets a Mega, uses Timer 4 and pin D38, and does not compile as published. | Without a compiling baseline there is nothing to test. | `uno_baseline/`, `docs/02-uno-port-design.md` |
| G2 | No stimulus source. Stage 1 needs a crank signal at a known speed; the starter kit has no signal generator, and no engine is present. | Without a repeatable crank stimulus, nothing about fuelling or phasing can be measured. | on-board crank simulator, DEV-004 |
| G3 | No definition of "behaving the way the software design says". The repository has no requirements, so "verified" is unfalsifiable. | Verification needs numeric pass/fail criteria agreed before testing. | `docs/03-requirements.md` |
| G4 | No shared acceptance criteria between stages. If the SIL suite and the NI sequence hard-code their own numbers, they will diverge on the first HIL finding. | This is the documented failure mode the bench is meant to demonstrate solving. | `tests/limits.env`, `docs/09-change-control.md` |
| G5 | The Uno's own timebase is the measurement reference in SIL, but the thing under test in HIL. | The 328P is clocked from a ceramic resonator on the Uno R3, typically about +/-0.5 %, plus temperature drift. Every HIL timing measurement carries that bias, and it looks like a firmware error if it is not budgeted. | `docs/05-stage2-hil-ni-plan.md` section 6 |
| G6 | NI hardware is unspecified. Sample rate, DO logic family, counter/timer availability and isolation decide whether the plan is feasible at all. | A 1 kS/s multifunction DAQ cannot resolve a 2800 us pulse to 1 %; a counter input can resolve it to 12.5 ns. | `docs/05-stage2-hil-ni-plan.md` section 2, open item O1 |
| G7 | No electrical interface design. The ECU's outputs are named "injector" and "fuel pump relay". | Wiring an Uno pin to anything inductive or above 20 mA destroys the pin. | `docs/08-wiring-chart.md` sections 5 and 7 |
| G8 | The adaptive network is treated as settled ECU behaviour. | Its output depends on the history of past cycles, so an absolute pulse-width assertion is only repeatable if lambda is held constant and the trim authority is inside the acceptance band. Trim authority is +/-100 us; the TC-03 band is +/-25 us around the table value. | authority and test conditions pinned in `docs/03-requirements.md` REQ-FUEL-030 and REQ-FUEL-031 |
| G9 | No configuration identity. "The firmware" is not a testable noun once there are two builds and a set of limits. | Test records must name the artefact they were produced from. | `docs/09-change-control.md` section 2 |

## 3. Pitfalls, in the order they will hit you

**Bench and firmware**

1. **Timer ownership.** `Servo` and hardware-timed injection cannot coexist on an
   Uno; both want Timer 1 (DEF-001). Symptom is a link error, not a runtime bug,
   so it is cheap - but the fix costs you the servo (DEV-003).
2. **A dropped stimulus edge looks like an engine stall.** One missed 500 ms
   window sends the state machine to STOP and cancels enrichment. Use a clean
   edge with a series resistor and keep the crank leads short; do not run the
   crank lines next to the injector LED lead.
3. **Interrupt-driven, so scope traces lie about cause.** The injector rises on
   the *pre-TDC* edge and falls on a timer compare. If you probe only TDC you
   will mis-attribute phase errors.
4. **First-cycle transient.** The very first pulse after reset is scheduled
   before any speed is known (DEF-004). Always discard the first cycle, or you
   will chase a 32.7 ms outlier that is a genuine firmware defect but not the one
   you are measuring.
5. **Serial telemetry perturbs what it reports.** At 115200 baud the original
   prints about 8 ms per loop pass (DEF-005). Rate-limit it, and never leave a
   plotter attached while taking timing data.
6. **Floating analog inputs.** An unconnected A-channel reads noise that walks
   through the CHT look-up table and moves the pulse width. Tie every unused
   analog pin to a rail (`docs/08-wiring-chart.md` note N4).
7. **Ceramic-resonator clock.** See G5. Measure the actual TDC-to-TDC period
   with the NI counter at a known crank period and record the ratio once; it is a
   fixed scale factor, not noise.

**Electrical**

8. **Pin current.** 20 mA per pin, 200 mA total on the 328P. LEDs need series
   resistors; motors, relays and injectors need a driver stage.
9. **Inductive kick.** Any motor or relay coil needs a flyback diode across it.
   Without one the switching transient will reach the MCU through the shared
   ground.
10. **Ground loops and shared references.** NI analog out, NI digital in, the
    Arduino and the PC's USB ground must meet at one point. Two ground paths
    around a loop turn into a millivolt-scale error on a 4.9 mV/count ADC.
11. **NI logic levels.** Uno VIH is 0.6 x Vcc, i.e. about 3.0 V. A 3.3 V DO works
    but with almost no margin; a 5 V TTL DO is preferable. Do not drive an Uno
    input above 5.5 V.
12. **NI AO overrange.** A +/-10 V AO card can put 10 V on an ADC pin rated to
    Vcc + 0.5 V. Programme the range *and* fit the clamp in
    `docs/08-wiring-chart.md` note N3.

**Process**

13. **Silent limit edits.** The temptation after a failing HIL run is to widen a
    number in the test. That is precisely the drift this bench exists to catch,
    hence the rule in `docs/09-change-control.md`.
14. **Two sources of truth for pin assignment.** A wiring change that is not in
    the firmware header (or vice versa) is the most common bench fault. Pin
    tables in `docs/08-wiring-chart.md` and the constants in
    `uno_baseline/uno_baseline.ino` are checked against each other by
    `tests/check_pinmap.sh`.

## 4. Implementation plan

Each step has an exit criterion. Do not proceed past a failing one.

### Step 1 - reproducible toolchain (done)

`arduino-cli` with `arduino:avr`, plus `simavr` and `libelf` for SIL.
Exit: `arduino-cli compile -b arduino:avr:uno uno_baseline` succeeds.

### Step 2 - Uno bench build of the firmware (done)

Port the Mega firmware to the 328P without changing control behaviour, recording
every difference as a DEV row. Add the crank simulator so the sketch can
stimulate itself.
Exit: firmware builds and fits (currently 46 % flash, 35 % SRAM).

### Step 3 - requirements and shared limits (done)

Write down what the software design promises, as numbers with tolerances.
Exit: every requirement in `docs/03-requirements.md` has a limit in
`tests/limits.env` or an explicit "logged only" note.

### Step 4 - SIL regression suite (done)

Run the *same binary* that will be flashed, inside simavr, driving ADC values and
crank edges and measuring the injector pin.
Exit: `tests/sil/run_sil_suite.sh` passes TC-SIL-01 to TC-SIL-08.

### Step 5 - flash and minimum bench, 3 signals (next, needs hardware)

Wiring group A in `docs/08-wiring-chart.md`: two crank jumpers, one pot on CHT,
one LED on the injector output.
Exit: TC-BENCH-01 to TC-BENCH-04 pass with LED/serial observation only.

### Step 6 - full analog and discrete set

Wiring groups B and C. All six analog channels on pots or fixed dividers, all
five discrete outputs on LEDs.
Exit: TC-BENCH-05 to TC-BENCH-08 pass.

### Step 7 - NI channel definition and dry-run

Fill in the channel table in `docs/05-stage2-hil-ni-plan.md` with the actual
device, then check continuity and levels with nothing else connected.
Exit: NI DO toggles a scope/LED at the Uno input pin at correct levels; NI AO
commands are read back within 1 % on the NI AI loopback, before either is
connected to the Uno.

### Step 8 - HIL equivalents of the SIL cases

Run TC-HIL-01 to TC-HIL-08 against the same limits file. Record the SIL value,
the HIL value and the difference for each.
Exit: a completed `docs/06-traceability-matrix.md` results column.

### Step 9 - reconcile

For every SIL/HIL difference outside the band: decide whether it is a firmware
defect, a measurement-uncertainty budget item, or a wrong limit. Raise a DEF row
and change exactly one of firmware, limits, or the uncertainty budget.
Exit: no unexplained difference, and every change traced to a DEF row.

### Step 10 - promote to production-level tests

Add sequences R&D testing does not need: power-on/reset behaviour, stimulus loss
mid-cycle, out-of-range sensor voltages, supply sag, and a repeatability run (n
>= 30) that produces a mean and standard deviation per measurement rather than a
single value.

## 5. Deliberate simplifications in the baseline

| Simplification | Restore when |
| --- | --- |
| Injector is observed as a logic-level output only. | A driver stage and current measurement are added (`docs/08-wiring-chart.md` group D). |
| Adaptive fuel trim runs as shipped, but every pulse-width test holds lambda at a fixed stimulus so its contribution is a small constant (+4.5 us at 2295 mV, measured in TC-SIL-03). | Trim convergence gets its own requirement, a lambda-step test, and a repeatability run. |
| Throttle servo replaced by a PWM LED. | The bench moves to a Mega, or the servo is driven from a free timer. |
| MAP and A4-TPS are logged, not verified. | A requirement exists that makes them affect an output. |
| Single crank pulse per revolution, no cam. | Multi-cylinder sequencing is in scope. |

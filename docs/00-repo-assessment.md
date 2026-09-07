# 00 - Assessment of the existing ECU firmware

Status: baseline established 2026-09-07. Applies to the root sketch
(`ECU_*.ino`) as committed.

## 1. Is this repository the right base for the test bench?

Yes, with qualifications. What is in the repository is a real, single-cylinder,
port-injected, two-stroke engine controller with the structure a bench needs:

| Function | Present | Notes |
| --- | --- | --- |
| Crank position acquisition | yes | two hall inputs, `hallTDC` (D2) and `hallPreTDC` (D3), falling edge |
| Speed calculation | yes | 1 pulse/revolution, `rpm = 60e6 / dt_us`, 5 % exponential filter |
| Fuel delivery | yes | one injector output, duration set by a hardware timer, 0.5 us resolution |
| Fuel map | yes | base duration vs cylinder head temperature (17 breakpoints) |
| Closed-loop trim | yes | equivalence-ratio error driven through a 3-node adaptive network |
| Engine state machine | yes | STOP / STARTING / IDLE on a 500 ms task |
| Overspeed protection | yes | `safetyPin` after 20 consecutive loop passes above 4000 rpm |
| Analog sensing | yes | MAT, CHT, MAP, lambda, TPS, throttle demand pot |
| Telemetry | yes | tab-separated serial record, MegunoLink compatible |

That is a good match for a SIL-to-HIL demonstration: every control output is
timed off a crank event, so it can be stimulated and measured electrically
without an engine.

Qualifications you should know before building anything:

1. **The target is an Arduino Mega 2560, not an Uno.** The firmware writes
   Timer 4 registers and drives `safetyPin = 38`; neither exists on an
   ATmega328P. See `docs/02-uno-port-design.md`.
2. **The published sketch does not build, on either board.** Three independent
   blockers, all reproduced with `arduino-cli 1.5.1` / `arduino:avr 1.8.8`
   (DEF-001, DEF-006, DEF-014 in `docs/07-defect-log.md`). This is worth
   knowing early: "flash the repo to the board" is not a one-command step.
3. **It is a two-stroke, one-injector controller.** There is no ignition output,
   no cam signal, no multi-cylinder sequencing. Do not expect a general ECU.
4. **Several inputs have no functional effect.** MAP and the dedicated TPS
   channel (A4) are sampled, converted and logged, but nothing downstream reads
   them. Fuel quantity depends only on CHT, the AFR/lambda trim, and the state
   machine. You cannot verify what does not affect an output, so those signals
   are logged-only in the test plan.

## 2. Control-path summary (what the bench must be able to prove)

```
D3 pre-TDC falling edge --> injectionTask()
                              OCR1A = (controlSignal_us * 2) - 1
                              injector D4 HIGH, Timer1 /8 start
                            TIMER1_COMPA --> injector D4 LOW

D2 TDC falling edge -----> checkTDC(): timestamp, isTDC = 1
                            main loop, idleCondition():
                              rpm      = 60e6 / (t[n] - t[n-1]), 5 % filter
                              interval = baseInt[nearest CHT breakpoint]
                              (STARTING: interval x 1.25)
                              correction = f(14.7/AFR - 1) via adaptive net,
                                           clamped +/- 100 us, IDLE only
                              controlSignal = interval + correction
```

So the observable transfer function of the whole ECU is:

**(crank period, CHT voltage, lambda voltage, engine state) -> (injector pulse
width, injector phase relative to TDC)**

Those four inputs and two outputs are what Stage 1 measures on the bench and
Stage 2 measures with NI hardware. Everything else is telemetry.

## 3. Timing budget on the original firmware

| Item | Value | Consequence |
| --- | --- | --- |
| Injection timer tick | 0.5 us (16 MHz / 8) | pulse-width resolution 0.5 us, max 32.7 ms |
| Base pulse range | 2500 - 3600 us | plus start enrichment x1.25 and trim +/-100 us |
| Cycle period | 30 ms at 2000 rpm, 7.5 ms at 8000 rpm | at 8000 rpm a 4500 us pulse is 60 % duty |
| Telemetry | approx 90 characters every loop pass at 115200 baud | approx 8 ms of blocking per pass (DEF-005) |
| Engine-state task | 500 ms, Timer 4 compare | 3 windows to reach IDLE, i.e. 1.5 s |

## 4. Verdict

Use this firmware as the item under test, but treat the root sketch as a
**reference** and the Uno bench build (`uno_baseline/`) as the **configuration
under test**. Every difference between the two is recorded with an identifier in
`docs/07-defect-log.md` and traced to a test case in
`docs/06-traceability-matrix.md`. That separation is what keeps the
documentation maintainable when HIL results start changing things.

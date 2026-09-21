# 07 - Defect log and port deviations

Two registers:

* **DEF-xxx** - findings in the root (Mega 2560) firmware. Each says whether the
  Uno bench build changes the behaviour.
* **DEV-xxx** - intentional deviations in `uno_baseline/` that exist only because
  the bench runs on different silicon or needs stimulus.

Evidence column: `build` = reproduced with `arduino-cli 1.5.1`, core
`arduino:avr 1.8.8`; `SIL` = measured in simavr with `tests/sil/sil_runner`;
`review` = code inspection only, not yet exercised.

## DEF - findings in the shipped firmware

| ID | Severity | Finding | Evidence | Bench build |
| --- | --- | --- | --- | --- |
| DEF-001 | blocker | The `Servo` library defines `TIMER1_COMPA_vect` (and `TIMER4_COMPA_vect` on Mega), which collide with the ECU's own injection and engine-state ISRs. Link fails with `multiple definition of __vector_17` / `__vector_42`. The firmware cannot use `Servo` and hardware-timed injection at the same time. | build | fixed by DEV-003 |
| DEF-006 | blocker | The global `volatile double time` collides with `time_t time(time_t*)` reachable from `<math.h>` in current avr-libc; `time = millis()` no longer compiles. | build | renamed `t_ms` |
| DEF-014 | blocker | The repository has no `ECU.ino`, so the folder is not a valid sketch. Also, `.ino` files are concatenated with the folder-named file first and the rest alphabetically, so the globals in `ECU_GLOBAL.ino` must come first or every module fails to resolve them. | build | bench sketch keeps globals in the folder-named file |
| DEF-004 | high | `controlSignal` is 0 until the first TDC event, but the first pre-TDC edge already calls `injectionTask()`. `(0 * 2) - 1` loads `0xFFFF` into OCR1A, holding the injector open for 32.7 ms - about 10x the largest legitimate pulse. | SIL | duration guarded to 200 - 8000 us, seeded at reset |
| DEF-008 | high | Start enrichment never reaches the injector. `loop()` calls `displayEngineState()` (which does `interval *= 1.25`) *before* `sensorSamplingTask()`, which overwrites `interval` from the look-up table. | SIL | order swapped; TC-SIL-08 measures 3506 us while STARTING against a 2804 us base |
| DEF-019 | high | `fuelPumpRelay`, `engineIndicator` and `starterIndicator` are configured as outputs but never driven (the relay is written LOW once in `setup()`). The engine state is only ever reported over serial. | review | driven from the state machine |
| DEF-002 | high | `f_TPS` is derived from the servo command word, not from throttle position: `valPot` is remapped to 125..143 for the servo, then `f_TPS = map(valPot, 143, 0, 0, 100)` pins throttle at about 12 % for the entire pot travel, so the AFR table row never changes. | review | `f_TPS` taken from the raw pot reading |
| DEF-010 | medium | `targetAFR = pgm_read_word(&AFR[i][j])` reads 2 bytes of a 4-byte `float` from PROGMEM, so the value is meaningless. Masked today because `targetEqRat` is hard-coded to 1 and `targetAFR` is never used. | review | `pgm_read_float` |
| DEF-009 | medium | The overspeed path calls `delay(5000)` inside `loop()`. Sensor sampling, telemetry and the state machine stop for 5 s while injection keeps running from interrupts. | review | latching output, no blocking delay |
| DEF-018 | medium | Overspeed is detected after 20 *loop passes* above 4000 rpm, not after a defined time or number of crank events, so trip latency is a function of telemetry load and cannot be specified. | SIL | unchanged; recorded as a requirement gap in `docs/04-stage1-vv-plan.md` |
| DEF-011 | medium | `TIFR1` is written with all-zero bits, which cannot clear a flag (AVR flags clear on write-1). A compare match pending from the previous cycle fires the end-of-injection ISR immediately, truncating that pulse. | review | `TIFR1 = (1 << OCF1A)` before start |
| DEF-005 | medium | Telemetry writes about 90 characters every loop pass at 115200 baud, roughly 8 ms of blocking, which dominates loop jitter and therefore the RPM/telemetry latency the bench is trying to characterise. | SIL | rate limited to 10 Hz |
| DEF-013 | medium | In `find_dj_dwij()` the delay array is indexed `ryev_del[0][l]` and `ryev_del[1][l-1]`, i.e. sample and signal subscripts swapped versus every other use, and `l-1` is -1 on the first sample. Out-of-bounds read on a 2x5 array. | review | indices made consistent |
| DEF-007 | low | `digitalWrite(adcLambda, HIGH)` in `setup()` enables the pull-up on A3, loading the lambda source and biasing the reading. | review | removed |
| DEF-015 | low | MAP scaling is for a 0 - 700 kPa sensor (152 kPa/V). An engine manifold range of 0 - 105 kPa occupies 0.03 - 0.16 V, i.e. about 30 ADC counts of the 1023 available. MAP is also unused downstream. | review | unchanged, logged only |
| DEF-003 | low | A4 (`adcTPS`) is sampled into `valTPS`, which nothing reads. | review | unchanged, logged only |
| DEF-017 | low | The AFR table's row axis is commented "TPS (in bit)" but `axisTPS` holds 5..100 and is searched with a percentage. Documentation/implementation mismatch. | review | unchanged, table left as shipped |
| DEF-016 | low | `throtPotPin = 9` is declared and never used; the servo is attached to a literal `9`. | review | not applicable |
| DEF-012 | info | The end-of-injection ISR comments "Disable Interrupt" but writes `OCIE1A = 1`. Harmless because the timer is stopped, but misleading. | review | interrupt actually disabled |

## DEV - deviations in the Uno bench build

| ID | Deviation | Reason |
| --- | --- | --- |
| DEV-001 | The 500 ms engine-state task moved from `ISR(TIMER4_COMPA_vect)` to a software divider on an 8 kHz Timer 2 tick. | ATmega328P has no Timer 4. Period is identical (4000 ticks). |
| DEV-002 | `safetyPin` moved from D38 to D9. | D38 does not exist on an Uno. |
| DEV-003 | The throttle servo is removed. Throttle plate position is rendered as PWM brightness on D6, and the servo command word is still computed and logged. | `Servo` owns Timer 1 on the Uno, which the injector pulse needs (DEF-001). A real servo can come back on a Mega, or via a separate timer, once the baseline is signed off. |
| DEV-004 | An on-board crank simulator drives D13 (TDC) and D12 (pre-TDC), jumpered to D2/D3, with `R<rpm>` and `A<advance_deg>` serial commands. | Stage 1 needs a repeatable stimulus; the starter kit contains no signal source. In Stage 2 the jumpers are removed and NI drives the same two lines, with no firmware change. |
| DEV-005 | Telemetry is rate limited to 10 Hz and reports commanded pulse width instead of `ccm`/`totalCons`. | DEF-005; pulse width is the quantity the HIL side measures. |
| DEV-006 | Pump and indicator outputs are driven from the engine state. | DEF-019; the bench needs observable state. |

## How this register is maintained

A HIL measurement that disagrees with SIL becomes a new row here, with the
evidence column set to `HIL`, and either a firmware change or a limit change in
`tests/limits.env`. The rule in `docs/09-change-control.md` is that no test
tolerance moves without a row in this table.

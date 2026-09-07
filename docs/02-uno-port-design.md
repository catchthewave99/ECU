# 02 - Uno R3 bench build design

`uno_baseline/` is the configuration under test. The root `ECU_*.ino` sketch is
kept unchanged as the reference. This document says what had to change and why.

## 1. Why the root sketch cannot be flashed to an Uno

| Root firmware uses | ATmega328P (Uno R3) | Consequence |
| --- | --- | --- |
| `TCCR4A/B`, `TIMSK4`, `ISR(TIMER4_COMPA_vect)` for the 500 ms engine-state task | no Timer 4 | does not compile |
| `safetyPin = 38` | 20 usable I/O, no D38 | no such pin |
| `Servo` plus its own `TIMER1_COMPA_vect` | Timer 1 is the only 16-bit timer | link error (DEF-001) |
| 8 KB SRAM, 256 KB flash | 2 KB SRAM, 32 KB flash | the 16 x 16 `float` AFR table is 1 KB and must stay in PROGMEM |
| `volatile double time` | avr-libc declares `time()` | does not compile (DEF-006) |

The port therefore is not cosmetic: it changes the timer architecture. That is
recorded as DEV-001 through DEV-006 in `docs/07-defect-log.md` so a HIL result
can always be attributed to either the ECU logic or the port.

## 2. Resource allocation on the 328P

| Resource | Owner | Configuration | Notes |
| --- | --- | --- | --- |
| Timer 0 | Arduino core | as core | `millis()`, `micros()`, `analogWrite` on D5/D6 |
| Timer 1 | injection | CTC, prescaler /8, 0.5 us tick | `OCR1A = 2 * pulse_us - 1`, max 32.7 ms |
| Timer 2 | bench | CTC, /8, `OCR2A = 249` -> 8 kHz | crank simulator and 500 ms task (DEV-001, DEV-004) |
| INT0 | D2 | falling edge | TDC |
| INT1 | D3 | falling edge | pre-TDC |
| ADC | free running via `analogRead` | 10 bit, AVcc reference | 4.888 mV/count |
| USART | telemetry and bench commands | 115200 8N1 | 10 Hz records (DEV-005) |
| Flash / SRAM | measured at build | 46 % / 35 % | headroom for HIL instrumentation |

The 8 kHz bench tick is the one number that constrains stimulus fidelity: crank
edges are quantised to 125 us. At 2000 rpm (30 ms/rev) that is 1.5 degrees of
crank angle, and it is why the TC-05 phase band is +/-80 us rather than tighter.
When NI drives D2/D3 in Stage 2 this quantisation disappears, which is itself a
useful SIL-versus-HIL observation.

## 3. Module map, reference to bench build

| Reference module | Bench module | Change |
| --- | --- | --- |
| `ECU_GLOBAL.ino` | `uno_baseline.ino` (globals + `setup()` + `loop()` + `idleCondition()`) | pins, `t_ms` rename, injection guard limits, bench state; must be the folder-named file so it compiles first (DEF-014) |
| `ECU_SETUP.ino` | `uno_baseline.ino` `setup()` | Timer 2 instead of Timer 4, no A3 pull-up, seeded `controlSignal` |
| `ECU_LOOP.ino` | `uno_baseline.ino` `loop()`, `idleCondition()` | sampling before state display (DEF-008), no blocking `delay()` on overspeed (DEF-009), `pgm_read_float` (DEF-010) |
| `ECU_Sensor.ino` | `Sensor.ino` | `f_TPS` from the raw pot (DEF-002) |
| `ECU_Inject.ino` | `Inject.ino` | duration guard, `TIFR1` cleared correctly (DEF-011), interrupt actually disabled (DEF-012) |
| `ECU_EngineState.ino` | `EngineState.ino` | ISR becomes a task called from the 8 kHz tick |
| `ECU_UsefulVoids.ino` | `UsefulVoids.ino` | PWM throttle surrogate, discrete outputs driven (DEF-019), rate-limited telemetry |
| `ECU_Functions.ino` | `Functions.ino` | unchanged apart from `const` correctness |
| `ECU_NeuralNet.ino` | `NeuralNet.ino` | consistent `ryev_del` indexing (DEF-013) |
| - | `Bench.ino` | new: crank simulator and serial command handler (DEV-004) |

## 4. Bench command interface

Sent over the same USB serial link as telemetry, one command per line:

| Command | Effect |
| --- | --- |
| `R<rpm>` | crank simulator speed; `R0` stops it and releases D12/D13 high |
| `A<deg>` | pre-TDC advance in crank degrees, default 30 |
| `?` | print setpoints, tick rate and injection counters |

Telemetry is one tab-separated record every 100 ms:

```
t_ms  MAT  CHT  MAP  AFR  TPS  RPM  pulse_us  correction  state
```

`state`: 0 = STOP, 1 = IDLE, 2 = STARTING.

## 5. Injection path, as built

```
pre-TDC falling edge -> checkPreTDC() -> injectionTask()
    us = controlSignal
    reject if us < 200 or us > 8000  (DEF-004, counted in injectionSkipped)
    OCR1A  = (unsigned)(us * 2) - 1
    TIFR1  = (1 << OCF1A)            (DEF-011)
    TCNT1  = 0
    injector HIGH
    TCCR1B = (1 << WGM12) | (1 << CS11)   /8, running
TIMER1_COMPA -> injector LOW, TCCR1B stops the timer, OCIE1A cleared
```

Rejected schedules are counted rather than clamped, so a limit violation is
visible in telemetry instead of being hidden by saturation.

## 6. Verification of the port itself

The port is verified by TC-SIL-01..08 in `docs/04-stage1-vv-plan.md`, which run
against the *same ELF* that gets flashed. Two properties are specifically
checked because they are port-sensitive:

* the 500 ms task period, indirectly, through the STARTING -> IDLE transition
  time (TC-SIL-02, must be inside `STATE_SETTLE_MS`);
* the Timer 1 tick, through absolute pulse width against the look-up table
  (TC-SIL-03), which fails if the prescaler or tick maths is wrong.

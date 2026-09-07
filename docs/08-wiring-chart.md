# 08 - Wiring chart: Arduino Uno R3 + starter-kit breadboard

Build this in four groups (A, B, C, D). Each group has its own verification step
in `docs/04-stage1-vv-plan.md`. Group A alone is enough to run the whole fuel
control path, and it uses four wires, one pot, one LED and one resistor.

Board: **Arduino Uno R3**. Breadboard: the 400-point board in the Arduino
Starter Kit (K000007). Rows are numbered 1 - 30, columns `a`-`e` (left half) and
`f`-`j` (right half), split by the centre channel; the two edge rails are used as
**+5 V (red)** and **GND (blue)**. Any equivalent breadboard works - the row
numbers below are a suggested, non-overlapping layout, not a requirement.

## 0. Power rails (do this first, once)

| From | To | Wire | Purpose |
| --- | --- | --- | --- |
| Uno `5V` | breadboard top red rail | red | 5 V supply for the sensor dividers |
| Uno `GND` | breadboard top blue rail | black | single-point ground, see note N1 |
| top red rail | bottom red rail | red | bridge the two halves |
| top blue rail | bottom blue rail | black | bridge the two halves |

Power the Uno from USB only. Do not fit the 9 V battery: the on-board regulator
gets hot and the 5 V rail sags, which moves every ADC reading (note N2).

## 1. Group A - minimum bench (4 signals, run this first)

Proves: crank acquisition, speed measurement, CHT look-up, injection pulse,
state machine, telemetry. This is the whole control path in
`docs/00-repo-assessment.md` section 2.

| # | Uno pin | Direction | Goes to | Component / value | Signal | Expected level | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A1 | D13 | out | D2 | jumper wire, board-to-board | crank TDC stimulus | 5 V idle, 250 us low pulse per rev | remove in Stage 2 (NI drives D2) |
| A2 | D12 | out | D3 | jumper wire, board-to-board | crank pre-TDC stimulus | 5 V idle, 250 us low pulse per rev | remove in Stage 2 (NI drives D3) |
| A3 | A1 | in | breadboard `e10` | 10 kOhm pot P2 wiper | CHT | 0 - 5 V, 3.30 V = 22 degC, 4.00 V = 81 degC | fit C1, see A6 |
| A4 | +5 V rail | - | pot P2 pin 1 | - | pot supply | 5.00 V | |
| A5 | GND rail | - | pot P2 pin 3 | - | pot return | 0 V | |
| A6 | `e10` | - | GND rail | 100 nF ceramic C1 | ADC anti-alias | - | keeps the wiper quiet; also the NI injection point in Stage 2 |
| A7 | D4 | out | breadboard `b20` | 220 Ohm R1, then LED1 (red) anode | injector command | 5 V for 2.5 - 3.6 ms per rev | LED cathode to GND rail |
| A8 | A3 | in | breadboard `e14` | 10 kOhm pot P1 wiper (+5 V / GND like P2) | lambda | 0 - 5 V, 2.295 V = 14.6 AFR | with C2 100 nF to GND |
| A9 | A0, A2, A4, A5 | in | GND rail | 10 kOhm R2 - R5, one per pin | unused analog | 0 V | note N4, mandatory |

LED1 blinks too fast to see individual pulses - it sits at a dim, constant
brightness proportional to duty (about 9 % at 2000 rpm). That is the intended
Group A observation; exact widths come from SIL and from Stage 2.

### Group A bring-up

1. Wire, then check with the Uno unpowered: continuity 5 V rail to GND must be
   open, and each pot wiper must read 0 - 5 V as you turn it.
2. Confirm the port with `arduino-cli board list` (usually `/dev/ttyACM0` on
   Linux, `COMn` on Windows), then
   `arduino-cli upload --fqbn arduino:avr:uno -p /dev/ttyACM0 uno_baseline`.
3. `arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200`. You should see the banner and 10 records/s with
   `RPM 0`, `state 0`.
4. Send `R1200`. Within 1.5 s `state` goes 2 then 1, `RPM` settles near 1200,
   LED1 lights dimly.
5. Turn P2 from 3.30 V to 4.00 V: `pulse_us` moves from about 3400 to about 2800
   in look-up-table steps.

## 2. Group B - remaining analog channels

Replace the R2 - R5 pull-downs from A9 one at a time. The kit has five 10 kOhm
pots; use fixed dividers where you run out.

| # | Uno pin | Direction | Source | Component | Signal | Expected level |
| --- | --- | --- | --- | --- | --- | --- |
| B1 | A0 | in | pot P3 wiper, `e4` | 10 kOhm pot + 100 nF | MAT | 0 - 5 V, 2.50 V = 45 degC |
| B2 | A2 | in | pot P4 wiper, `e24` | 10 kOhm pot + 100 nF | MAP | 0 - 5 V, logged only (DEF-015) |
| B3 | A5 | in | pot P5 wiper, `e28` | 10 kOhm pot + 100 nF | throttle demand | 0 - 5 V = 0 - 100 % TPS |
| B4 | A4 | in | divider `e26` | 10 kOhm + 10 kOhm to 5 V/GND | TPS channel | fixed 2.5 V, logged only (DEF-003) |
| B5 | A0 | in | *alternative*: TMP36 pin 2 | TMP36 (+5 V, GND) | real MAT | 10 mV/degC, about 0.75 V at 25 degC |

B5 is worth doing once: a TMP36 at room temperature reads about 0.75 V, which
the MAT conversion in REQ-SENS-010 turns into roughly -232 degC. That is not a
firmware bug - it shows the conversion assumes a different sensor, and it is
exactly the kind of finding that HIL testing is supposed to surface early.

## 3. Group C - discrete outputs

All five are logic-level indications. One 220 Ohm resistor per LED, cathodes to
the GND rail.

| # | Uno pin | LED | Resistor | Meaning | Active |
| --- | --- | --- | --- | --- | --- |
| C1 | D5 | LED2 green | 220 Ohm | fuel-pump demand | STARTING or IDLE |
| C2 | D6 | LED3 yellow | 220 Ohm | throttle plate position (PWM brightness) | proportional to demand |
| C3 | D7 | LED4 green | 220 Ohm | engine running | IDLE |
| C4 | D8 | LED5 yellow | 220 Ohm | cranking | STARTING |
| C5 | D9 | LED6 red | 220 Ohm | overspeed | reported rpm > 4000 |

Total LED current with all six on: about 6 x 15 mA = 90 mA, inside the 200 mA
port limit (REQ-IF-007), but do not add a seventh load without recounting.

## 4. Group D - optional injector power stage

Only build this when the logic-level bench is signed off. It converts D4 into a
real current-switching event so a clamp-on or shunt measurement means something.

| # | Node | Component | Detail |
| --- | --- | --- | --- |
| D1 | D4 -> `b25` | 220 Ohm | gate series resistor for Q1 |
| D2 | `b25` -> GND | 10 kOhm | gate pull-down, keeps Q1 off during reset |
| D3 | Q1 | IRF520 MOSFET | gate `b25`, source to GND rail, drain to load |
| D4 | load | kit DC motor **or** a 100 Ohm 1 W resistor | the resistor is the better bench load: purely resistive, no back-EMF, 90 mA at 9 V |
| D5 | across load | 1N4007 diode | cathode to +V, anode to drain - mandatory if the load is the motor (pitfall 9) |
| D6 | load +V | separate 9 V supply | its ground bonds to the Uno GND rail at the single point only (note N1) |
| D7 | shunt | 1 Ohm in the source leg | measure across it for injector current; 90 mA gives 90 mV |

The IRF520's gate threshold is specified up to 4 V, so 5 V drive is marginal for
full enhancement. At 90 mA it will switch fine, but do not scale this stage up
without a logic-level MOSFET. Note that Group D changes the electrical loading of
D4, so re-run the Group A pulse-width check after fitting it.

## 5. Stage 2 - what NI replaces

Same firmware, same pins. Remove the two crank jumpers and the pot on whichever
channel NI is driving. Rig: cDAQ-9173 + NI-9401 (8 lines, 5 V TTL, direction per
nibble) + NI-9263 (4 x +/-10 V AO). Channel list and rationale in
`docs/05-stage2-hil-ni-plan.md` section 2.

| Bench item | Stage 2 replacement | Interface |
| --- | --- | --- |
| A1 jumper D13 -> D2 | NI-9401 `line0` -> D2 | 1 kOhm series, note N5 |
| A2 jumper D12 -> D3 | NI-9401 `line1` -> D3 | 1 kOhm series, note N5 |
| A3 pot P2 on A1 (CHT) | NI-9263 `ao0` -> A1 at node `e10` | 1 kOhm series + existing 100 nF, clamp per note N3 - **mandatory**, the module can output 10 V |
| A8 pot P1 on A3 (lambda) | NI-9263 `ao1` -> A3 | as above |
| B1 pot P3 on A0 (MAT) | NI-9263 `ao2` -> A0 | as above |
| B3 pot P5 on A5 (throttle) | NI-9263 `ao3` -> A5 | as above |
| B2 pot P4 on A2 (MAP), B4 divider on A4 | stay on the breadboard | only 4 AO channels; both are logged-only signals |
| LED1 observation on D4 | NI-9401 `line4` as a counter input, LED left fitted | note N6 |
| LED4, LED5, LED6 on D7, D8, D9 | NI-9401 `line6`, `line7`, `line5` as DI | 5 V TTL in |
| LED2, LED3 on D5, D6 | stay LED-and-telemetry only | the input nibble has 4 lines and 5 candidates |
| Uno `5V` rail, group D shunt | DMM | the rig has no AI module |

Keep the LEDs fitted when NI is connected: they are the local sanity check that
the NI channel and the pin agree.

## 6. Notes

* **N1 single-point ground.** The Uno GND rail, the Group D supply ground and the
  NI-9401 COM / NI-9263 AO COM must meet at exactly one point, on the breadboard GND rail
  next to the Uno GND wire. Do not also bond them at the NI chassis (pitfall 10).
* **N2 supply.** USB only. Note the actual 5 V rail voltage in the test record:
  the ADC is ratiometric to it, so 4.85 V instead of 5.00 V is a 3 % scale error
  on every analog reading.
* **N3 ADC input protection.** Uno analog pins tolerate -0.5 V to Vcc + 0.5 V.
  The NI-9263 in this rig is a +/-10 V module, so this is not optional: fit a
  1 kOhm series resistor at the pin plus a pair of small-signal Schottky diodes
  from the pin to 5 V and to GND (or a 5.1 V zener). Also clamp the commanded
  voltage in the test software (`tests/hil/ni_sequence.py`, `clamp_volts()`). Do
  both, not either (pitfall 12).
* **N4 no floating analog inputs.** Every unused A-pin gets a 10 kOhm pull-down.
  A floating pin reads drifting noise, and on A1 that walks the CHT look-up and
  moves the injector pulse width by hundreds of microseconds (pitfall 6).
* **N5 digital input levels.** Uno VIH is about 3.0 V, VIL about 1.5 V; the
  NI-9401 is 5 V TTL, so levels are compatible in both directions. Fit a 1 kOhm
  series resistor on each NI-driven line to limit fault current, and never
  exceed 5.5 V on a digital pin (pitfall 11). Set the direction of the whole
  nibble before connecting: two NI-9401 lines configured as outputs against Uno
  outputs is a contention fault.
* **N6 measuring the injector.** Resolve the pulse to better than 1 % of 2800 us,
  i.e. better than 28 us. A counter/timer edge-separation measurement does this
  trivially; a software-timed DI loop does not. See
  `docs/05-stage2-hil-ni-plan.md` section 2.
* **N7 D13 has the on-board LED and its resistor.** That is fine as a crank
  output into D2 (a high-impedance input), and it gives you a free crank-activity
  indication. Do not also load D13 with an LED of your own.
* **N8 D0/D1 are the USB serial pins.** Leave them unconnected; anything on them
  breaks telemetry and uploading.
* **N9 reset behaviour.** Opening the serial port resets the Uno, so the crank
  setpoint reverts to 0 and you must resend `R<rpm>`. Expect it, and put the
  command in your test script rather than typing it.

## 7. Bill of materials, from the starter kit

| Qty | Item | Kit part | Group |
| --- | --- | --- | --- |
| 1 | Arduino Uno R3 + USB cable | included | - |
| 1 | 400-point breadboard | included | - |
| ~20 | jumper wires | included | all |
| 5 | 10 kOhm potentiometer | included | A, B |
| 6 | LED (2 red, 2 green, 2 yellow) | included | A, C |
| 6 | 220 Ohm resistor | included | A, C, D |
| 6 | 10 kOhm resistor | included | A, B, D |
| 5 | 100 nF ceramic capacitor | included | A, B |
| 1 | TMP36 temperature sensor | included | B5, optional |
| 1 | IRF520 MOSFET | included | D, optional |
| 1 | 1N4007 diode | included | D, optional |
| 1 | DC motor, or a 100 Ohm 1 W resistor | motor included | D, optional |

Not in the kit, needed only for Stage 2: the NI rig, a 9 V bench supply for
Group D, eight Schottky diodes or four 5.1 V zeners plus four 1 kOhm resistors
for the four AO clamps (note N3), six 1 kOhm resistors for the NI digital lines,
a 1 Ohm shunt for D7, and a DMM - the rig has no AI module, so the 5 V reference
and any current measurement are manual readings. A scope or logic analyser is
not required, but it will save you an afternoon on the phase measurement.

## 8. Pin map, single page

```
                 Arduino Uno R3
   D0  RX   -- USB serial, leave alone
   D1  TX   -- USB serial, leave alone
   D2  IN   <- TDC          (jumper from D13, later NI DO)
   D3  IN   <- pre-TDC      (jumper from D12, later NI DO)
   D4  OUT  -> injector     (220R + LED1, later NI counter in / Q1 gate)
   D5  OUT  -> fuel pump    (220R + LED2, later NI DI)
   D6  OUT  -> throttle PWM (220R + LED3, later NI DI/AI)
   D7  OUT  -> engine run   (220R + LED4, later NI DI)
   D8  OUT  -> cranking     (220R + LED5, later NI DI)
   D9  OUT  -> overspeed    (220R + LED6, later NI DI)
   D10 D11  -- spare
   D12 OUT  -> crank pre-TDC stimulus (bench only)
   D13 OUT  -> crank TDC stimulus     (bench only, on-board LED)
   A0  IN   <- MAT     pot P3 / TMP36 / NI AO
   A1  IN   <- CHT     pot P2 / NI AO      <- controls fuelling
   A2  IN   <- MAP     pot P4 / NI AO      (logged only)
   A3  IN   <- lambda  pot P1 / NI AO      <- closed-loop trim
   A4  IN   <- TPS     divider / NI AO     (logged only)
   A5  IN   <- throttle demand pot P5 / NI AO
   5V  ---> breadboard red rail
   GND ---> breadboard blue rail (single-point ground)
```

`tests/check_pinmap.sh` compares this section against the pin constants in
`uno_baseline/uno_baseline.ino`, so the chart cannot drift from the firmware
(pitfall 14).

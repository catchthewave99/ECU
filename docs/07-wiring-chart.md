# 08 - Wiring chart: Arduino Uno R3 bench

One Arduino Uno R3 is the ECU. Everything it controls is an LED, and everything
it reads is a switch or a potentiometer, so the whole bench fits on the
400-point breadboard from the Arduino Starter Kit (K000007).

Build it in three groups. Group A is enough to run the hazard safety test,
which is the test that matters most; B and C add the rest.

Board: **Arduino Uno R3**, powered from USB only. Do not fit the 9 V battery -
the regulator gets warm and the 5 V rail sags, which moves every temperature
reading (note N2).

## 0. Power rails (do this first, once)

| From | To | Wire | Purpose |
| --- | --- | --- | --- |
| Uno `5V` | breadboard red rail | red | 5 V for the pot and the switches |
| Uno `GND` | breadboard blue rail | black | single-point ground, see note N1 |

## 1. Group A - hazard warning (the safety function)

Proves: the hazard switch lights both hazard lamps within 100 ms
(REQ-SAFE-100), and they then flash together.

| # | Uno pin | Direction | Goes to | Component | Signal | Idle level |
| --- | --- | --- | --- | --- | --- | --- |
| A1 | D2 | in | pushbutton to +5 V rail | button + 10 kOhm to GND | hazard switch | 0 V, 5 V when pressed |
| A2 | D8 | out | 220 Ohm, then LED1 (yellow) anode | 220 Ohm | left lamp | 0 V |
| A3 | D9 | out | 220 Ohm, then LED2 (yellow) anode | 220 Ohm | right lamp | 0 V |
| A4 | D13 | out | nothing (on-board LED) | - | heartbeat | flickers |

Every switch input uses the same pattern: button between the pin and +5 V, and
a 10 kOhm resistor from the pin to GND so the pin is never left floating
(note N4). LED cathodes all go to the GND rail.

D13 flips on every pass of `loop()`. It looks like a dim flicker, and the test
bench measures it to see how long a pass takes.

## 2. Group B - body control

| # | Uno pin | Direction | Goes to | Component | Signal |
| --- | --- | --- | --- | --- | --- |
| B1 | D7 | in | pushbutton to +5 V rail | button + 10 kOhm to GND | headlamp switch |
| B2 | D4 | in | pushbutton to +5 V rail | button + 10 kOhm to GND | turn stalk left |
| B3 | D5 | in | pushbutton to +5 V rail | button + 10 kOhm to GND | turn stalk right |
| B4 | D10 | out | 220 Ohm, then LED3 (white or green) | 220 Ohm | headlamps |

The turn signals share LED1 and LED2 with the hazard warning, exactly as a car
does. Hazard wins: while the hazard switch is closed, the stalk does nothing to
those two lamps.

## 3. Group C - thermal control

| # | Uno pin | Direction | Goes to | Component | Signal | Scale |
| --- | --- | --- | --- | --- | --- | --- |
| C1 | A0 | in | 10 kOhm pot P1 wiper | pot across +5 V / GND, 100 nF to GND | coolant temperature | 10 mV per degree C, so 0.95 V = 95 degC |
| C2 | D11 | out | 220 Ohm, then LED4 (green) | 220 Ohm | cooling fan relay |
| C3 | D12 | out | 220 Ohm, then LED5 (red) | 220 Ohm | over-temperature warning |
| C4 | A1 - A5 | in | GND rail | 10 kOhm each | unused analog pins | note N4 |

A TMP36 from the kit can replace the pot on A0. It reads about 0.75 V at room
temperature, which this firmware calls 75 degC - the scale is the same
10 mV/degC, so the pot and the sensor are interchangeable for the bench.

## 4. Stage 2 - what the NI rig replaces

Same firmware, same pins. Pull the buttons and the pot; the rig drives those
nodes instead. Rig: cDAQ-9173 + NI-9401 (8 lines, 5 V TTL, direction set per
nibble) + NI-9263 (4 channels, +/-10 V AO).

| Bench item | Stage 2 replacement | Interface |
| --- | --- | --- |
| A1 hazard button on D2 | NI-9401 `line0` -> D2 | 1 kOhm series, note N5 |
| B2 turn-left button on D4 | NI-9401 `line1` -> D4 | as above |
| B3 turn-right button on D5 | NI-9401 `line2` -> D5 | as above |
| B1 headlamp button on D7 | NI-9401 `line3` -> D7 | as above |
| A2 left lamp on D8 | NI-9401 `line4` as DI | 5 V TTL in |
| A3 right lamp on D9 | NI-9401 `line5` as DI | 5 V TTL in |
| C2 fan on D11 | NI-9401 `line6` as DI | 5 V TTL in |
| C3 over-temperature lamp on D12 | NI-9401 `line7` as DI | 5 V TTL in |
| C1 pot P1 on A0 | NI-9263 `ao0` -> A0 | 1 kOhm series + the 100 nF, clamp per note N3 - **mandatory**, the module can output 10 V |
| B4 headlamp on D10, heartbeat on D13 | stay LED-and-telemetry only | the NI-9401 has 8 lines and 10 candidates |

Note the direction rule: the NI-9401 sets direction per nibble, so lines 0-3
are the four switch outputs and lines 4-7 are the four lamp inputs. That is why
the switches are grouped on D2/D4/D5/D7 and the observed outputs on
D8/D9/D11/D12.

Leave the LEDs fitted when the rig is connected. They are the local sanity
check that the NI channel and the pin agree.

## 5. Notes

* **N1 single-point ground.** The Uno GND rail and the NI-9401 COM / NI-9263 AO
  COM must meet at exactly one point, on the breadboard GND rail next to the Uno
  GND wire. Do not also bond them at the NI chassis.
* **N2 supply.** USB only. Write the measured 5 V rail voltage in the test
  record: the ADC is ratiometric to it, so 4.85 V instead of 5.00 V is a 3 %
  error on every temperature reading.
* **N3 ADC input protection.** Uno analog pins tolerate -0.5 V to Vcc + 0.5 V.
  The NI-9263 is a +/-10 V module, so this is not optional: fit a 1 kOhm series
  resistor at the pin plus small-signal Schottky diodes from the pin to 5 V and
  to GND (or a 5.1 V zener), **and** clamp the commanded voltage in software
  (`tests/hil/ni_sequence.py`, `clamp_volts()`). Do both, not either.
* **N4 no floating pins.** Every switch input gets a 10 kOhm pull-down and every
  unused analog pin gets one too. A floating pin reads drifting noise, and on a
  switch input that looks like somebody pressing the button.
* **N5 digital levels.** Uno VIH is about 3.0 V, VIL about 1.5 V; the NI-9401 is
  5 V TTL, so both directions are compatible. Fit a 1 kOhm series resistor on
  each NI-driven line, never exceed 5.5 V on a digital pin, and set the
  direction of each nibble before connecting - an NI line driving an Uno output
  is a contention fault.
* **N6 D0/D1 are the USB serial pins.** Leave them unconnected; anything on them
  breaks telemetry and uploading.
* **N7 reset on connect.** Opening the serial port resets the Uno. Expect the
  first telemetry line to be the banner.

## 6. Bill of materials, from the starter kit

| Qty | Item | Kit part | Group |
| --- | --- | --- | --- |
| 1 | Arduino Uno R3 + USB cable | included | - |
| 1 | 400-point breadboard | included | - |
| ~20 | jumper wires | included | all |
| 4 | pushbutton | included | A, B |
| 1 | 10 kOhm potentiometer | included | C |
| 5 | LED (2 yellow, 1 green, 1 red, 1 white/green) | included | A, B, C |
| 5 | 220 Ohm resistor | included | A, B, C |
| 9 | 10 kOhm resistor | included | A, B, C |
| 1 | 100 nF ceramic capacitor | included | C |
| 1 | TMP36 temperature sensor | included | C, optional |

Not in the kit, needed only for Stage 2: the NI rig, four 1 kOhm resistors and
clamp diodes for the analog channel (note N3), and eight 1 kOhm resistors for
the digital lines.

## 7. Pin map, single page

```
                 Arduino Uno R3
   D0  RX   -- USB serial, leave alone
   D1  TX   -- USB serial, leave alone
   D2  IN   <- hazard switch      (button, later NI DO line0)
   D3       -- spare
   D4  IN   <- turn stalk left    (button, later NI DO line1)
   D5  IN   <- turn stalk right   (button, later NI DO line2)
   D6       -- spare
   D7  IN   <- headlamp switch    (button, later NI DO line3)
   D8  OUT  -> left lamp          (220R + LED1, turn signal and hazard)
   D9  OUT  -> right lamp         (220R + LED2, turn signal and hazard)
   D10 OUT  -> headlamps          (220R + LED3)
   D11 OUT  -> cooling fan relay  (220R + LED4)
   D12 OUT  -> overtemp warning   (220R + LED5)
   D13 OUT  -> heartbeat          (on-board LED, one flip per loop())
   A0  IN   <- coolant temperature (pot P1 / TMP36 / NI AO ao0)
   A1 - A5  -- unused, 10 kOhm to GND each
   5V  ---> breadboard red rail
   GND ---> breadboard blue rail (single-point ground)
```

`tests/check_pinmap.sh` compares this section against the pin constants in
`consolidated/ecu.h`, so the chart cannot drift away from the firmware. It is
the check that would have caught DEF-101 before the board was even built.

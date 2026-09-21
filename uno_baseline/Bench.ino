/*=========================== Bench crank simulator (DEV-004) =============================
  Stage 1 stimulus generator. An 8 kHz Timer 2 tick synthesises the two crank
  signals the ECU expects and drives them out of D13 (TDC) and D12 (pre-TDC),
  which are jumpered back to the ECU inputs D2 and D3.

  Both edges are active-low, matching the FALLING-edge interrupts in setup().
  In Stage 2 the jumpers are removed and the same two signals are driven by the
  NI hardware, so the ECU code under test does not change between stages.

  Serial commands (115200 8N1, newline terminated):
    R<rpm>   crank speed setpoint, 0 stops cranking, 200..8000 otherwise
    A<deg>   injection trigger advance before TDC, 0..180 degrees
    ?        print bench status
=========================================================================================*/

void setCrankRPM(unsigned int rpmSetpoint) {
  if (rpmSetpoint == 0) {
    crankRPMSetpoint = 0;
    noInterrupts();
    crankTicksPerRev = 0;
    crankPhase = 0;
    interrupts();
    PORTB |= (1 << PB5) | (1 << PB4);  // idle both crank lines high
    return;
  }

  rpmSetpoint = constrain(rpmSetpoint, 200, 8000);
  crankRPMSetpoint = rpmSetpoint;

  unsigned int ticksPerRev = (unsigned int)(((unsigned long)BENCH_TICK_HZ * 60UL) / rpmSetpoint);
  unsigned int preTick = ticksPerRev - (unsigned int)(((unsigned long)ticksPerRev * crankAdvanceDeg) / 360UL);
  if (preTick >= ticksPerRev) preTick = ticksPerRev - 1;

  noInterrupts();
  crankTicksPerRev = ticksPerRev;
  crankPreTDCTick = preTick;
  crankPhase = 0;
  interrupts();
}

void setCrankAdvance(unsigned int deg) {
  crankAdvanceDeg = constrain(deg, 0, 180);
  setCrankRPM(crankRPMSetpoint);
}

void serviceBenchCommands() {
  static char buf[12];
  static byte n = 0;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf[n] = '\0';
      if (n > 0) {
        if (buf[0] == 'R' || buf[0] == 'r') {
          setCrankRPM((unsigned int)atoi(&buf[1]));
          printBenchStatus();
        } else if (buf[0] == 'A' || buf[0] == 'a') {
          setCrankAdvance((unsigned int)atoi(&buf[1]));
          printBenchStatus();
        } else if (buf[0] == '?') {
          printBenchStatus();
        }
      }
      n = 0;
    } else if (n < sizeof(buf) - 1) {
      buf[n++] = c;
    }
  }
}

void printBenchStatus() {
  Serial.print(F("# rpm_sp=")); Serial.print(crankRPMSetpoint);
  Serial.print(F(" adv_deg=")); Serial.print(crankAdvanceDeg);
  Serial.print(F(" ticks_rev=")); Serial.print(crankTicksPerRev);
  Serial.print(F(" inj=")); Serial.print(injectionCount);
  Serial.print(F(" inj_skipped=")); Serial.println(injectionSkipped);
}

// 8 kHz bench tick: crank waveform synthesis plus the 500 ms engine-state task
// that ran on Timer 4 of the Mega 2560.
ISR(TIMER2_COMPA_vect) {
  if (crankTicksPerRev != 0) {
    unsigned int phase = crankPhase;

    if (phase == 0) {
      PORTB &= ~(1 << PB5);            // D13 low: TDC edge
    } else if (phase == crankPulseTicks) {
      PORTB |= (1 << PB5);
    }

    if (phase == crankPreTDCTick) {
      PORTB &= ~(1 << PB4);            // D12 low: pre-TDC edge
    } else if (phase == (unsigned int)(crankPreTDCTick + crankPulseTicks)) {
      PORTB |= (1 << PB4);
    }

    phase++;
    if (phase >= crankTicksPerRev) phase = 0;
    crankPhase = phase;
  }

  benchTicks500ms++;
  if (benchTicks500ms >= (BENCH_TICK_HZ / 2)) {
    benchTicks500ms = 0;
    engineStateTask();
  }
}

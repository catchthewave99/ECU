// Timer1 output compare A interrupt service routine: end of injection pulse.
ISR(TIMER1_COMPA_vect)
{
  digitalWrite(injectorPin, LOW);

  TIMSK1 = 0;                    // disable compare interrupt
  TCCR1B = (1 << WGM12);         // stop Timer 1, stay in CTC mode
}

// Called from the pre-TDC interrupt. Timer 1 runs at 16 MHz / 8 = 2 MHz, so one
// count is 0.5 us and the register value is (duration_us * 2) - 1.
void injectionTask() {
  // DEF-004: reject an out-of-range duration instead of wrapping the 16-bit
  // register. controlSignal == 0 previously produced 0xFFFF (32.7 ms of open
  // injector) on the first pre-TDC edge after reset.
  if (controlSignal < INJ_MIN_US || controlSignal > INJ_MAX_US) {
    injectionSkipped++;
    return;
  }

  durationRegister = (unsigned int)((controlSignal * 2.0) - 1.0);
  OCR1AH = (durationRegister & 0xFF00U) >> 8U;
  OCR1AL = (durationRegister & 0x00FFU);

  // Reset Timer 1
  TCNT1H = 0x00;
  TCNT1L = 0x00;

  digitalWrite(injectorPin, HIGH);

  TIMSK1 = (1 << OCIE1A);
  TIFR1  = (1 << OCF1A);                        // clear pending compare flag
  TCCR1B = (1 << WGM12) | (1 << CS11);          // start Timer 1, /8

  injectionCount++;
}

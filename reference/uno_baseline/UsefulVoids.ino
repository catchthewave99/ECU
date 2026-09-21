void checkTDC() {
  temp2 = temp1;
  temp1 = micros();
  isTDC = 1;
}

void checkPreTDC() {
  isPreTDC = 1;
  injectionTask();
}

// DEV-003: the Mega firmware drove a throttle servo from D9 with the Servo
// library. On the Uno that library owns Timer 1, which the injector pulse needs,
// so the bench renders throttle plate position as LED brightness instead. The
// servo command word is still computed and logged so the transfer function can
// be verified against the original.
void runThrottleOutput() {
  valPotRaw = analogRead(adcPot);
  valPot = map(valPotRaw, 0, 1023, 125, 143);
  TPSFilter.Filter(valPot);
  valPot = constrain((int)TPSFilter.Current(), 125, 143);
  analogWrite(throttlePlatePin, map(valPot, 125, 143, 0, 255));
}

void displayEngineState() {
  if (isEngineIdle == 1) {
    printEngine = 1;
  }

  if (isStarting == 1) {
    printEngine = 2;
    interval *= 1.25;   // start enrichment
  }

  if (isEngineIdle == 0 && isStarting == 0) {
    printEngine = 0;
    rpm = 0;
  }

  digitalWrite(engineIndicator, isEngineIdle == 1 ? HIGH : LOW);
  digitalWrite(starterIndicator, isStarting == 1 ? HIGH : LOW);
  digitalWrite(fuelPumpRelay, (isEngineIdle == 1 || isStarting == 1) ? HIGH : LOW);
}

// DEF-005: rate limited. The Mega firmware emitted ~90 characters every loop
// pass, which at 115200 baud blocks the main loop for about 8 ms and dominates
// the sampling jitter the bench is meant to measure.
void printToHMI() {
  if (millis() - lastHMI < HMI_PERIOD_MS) {
    return;
  }
  lastHMI = millis();

  Serial.print((unsigned long)t_ms); Serial.print("\t");
  Serial.print(f_MAT, 1); Serial.print("\t");
  Serial.print(f_CHT, 1); Serial.print("\t");
  Serial.print(f_MAP, 2); Serial.print("\t");
  Serial.print(f_Lambda, 2); Serial.print("\t");
  Serial.print(f_TPS, 0); Serial.print("\t");
  Serial.print(rpm, 0); Serial.print("\t");
  Serial.print(controlSignal, 0); Serial.print("\t");
  Serial.print(correction, 1); Serial.print("\t");
  Serial.println(printEngine);
}

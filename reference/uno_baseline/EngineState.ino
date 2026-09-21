// 500 ms engine-state task. On the Mega this was ISR(TIMER4_COMPA_vect); on the
// Uno it is called from the 8 kHz Timer 2 tick (DEV-001). The state logic is
// unchanged: three consecutive 500 ms windows with crank rotation promote the
// engine from STARTING to IDLE, and one window without rotation drops it to STOP.
void engineStateTask() {
  // No rotation in the last 500 ms
  if (lastTemp == temp1) {
    isEngineIdle = 0;
    isStarting = 0;
    startingCount = 0;
  }
  // Engine is rotating
  else {
    if (isEngineIdle == 0) {
      isStarting = 1;
      startingCount++;

      // If engine is rotating for more than 3 * 500 ms, update engine status
      if (startingCount >= 3) {
        isEngineIdle = 1;
        isStarting = 0;
      }
    }
  }

  // Update time stamp
  lastTemp = temp1;
}

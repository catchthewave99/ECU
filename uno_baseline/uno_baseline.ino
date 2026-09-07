// Sketch        : uno_baseline
// Target board  : Arduino Uno R3 (ATmega328P, 16 MHz)
// Purpose       : SIL/HIL test-bench baseline port of the Mega 2560 ECU firmware
//                 in the repository root. Behaviour is traceable to the original
//                 module by module; every intentional difference is recorded in
//                 docs/07-defect-log.md with a DEF-xxx / DEV-xxx identifier.
//
// Build         : arduino-cli compile -b arduino:avr:uno uno_baseline
// Dependencies  : MegunoLink (provides Filter.h / ExponentialFilter)
//
// Timer budget  : Timer0 -> Arduino millis()/micros(), analogWrite on D5/D6
//                 Timer1 -> injector pulse width (CTC, /8, 0.5 us tick)
//                 Timer2 -> 8 kHz bench tick: crank simulator + 500 ms engine state
//
// The Servo library is deliberately NOT used: on both Uno and Mega it defines
// TIMER1_COMPA_vect (and TIMER4_COMPA_vect on Mega), which collides with the
// ECU's own injection and engine-state ISRs (DEF-001).

#include <Filter.h>
#include <math.h>

/*=============================================Look Up Table====================================================*/

const float baseInt[] = {
  //Temperature      20    25    30    35    40    45    50    55    60    65    70    80    90    100   110   120  130
  /*Base Interval*/ 3600, 3600, 3500, 3500, 3400, 3400, 3300, 3300, 3200, 3200, 3200, 3000, 2900, 2800, 2700,  2600, 2500
};

const PROGMEM float AFR[][16] = {
  /* RPM         0    400     800     1200    1600    2000    2400    2800    3200    3600    4000    4400    4800    6000    7000    8000 */
  /*  5 */  { 13  , 13  , 13  , 13  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 10 */  { 13  , 13  , 14  , 14  , 15.7  , 15.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 15 */  { 13  , 13  , 15  , 15  , 16.7  , 16.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 20 */  { 13  , 13  , 16  , 16  , 17.7  , 17.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 25 */  { 13  , 13  , 17  , 17  , 18.7  , 18.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 30 */  { 13  , 13  , 18  , 18  , 19.7  , 19.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 35 */  { 13  , 13  , 19  , 19  , 20.7  , 20.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 40 */  { 13  , 13  , 20  , 20  , 21.7  , 21.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 45 */  { 13  , 13  , 21  , 21  , 22.7  , 22.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 50 */  { 13  , 13  , 22  , 22  , 23.7  , 23.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 55 */  { 13  , 13  , 23  , 23  , 24.7  , 24.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 60 */  { 13  , 13  , 24  , 24  , 25.7  , 25.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 70 */  { 13  , 13  , 25  , 25  , 26.7  , 26.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 80 */  { 13  , 13  , 26  , 26  , 27.7  , 27.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 90 */  { 13  , 13  , 27  , 27  , 28.7  , 28.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  },
  /* 100 */ { 13  , 13  , 28  , 28  , 29.7  , 29.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  , 14.7  }
};

const int axisRPM[]  = {0, 400, 800, 1200, 1600, 2000, 2400, 2800, 3200, 3600, 4000, 4400, 4800, 6000, 7000, 8000};
const int axisTPS[]   = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80, 90, 100};
const int axisTemp[] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 110};

int idxRPM;
int idxTPS;
int idxTemp;

ExponentialFilter<float> MATFilter(5, 0);
ExponentialFilter<float> CHTFilter(5, 0);
ExponentialFilter<float> MAPFilter(5, 0);
ExponentialFilter<float> AFRFilter(5, 0);
ExponentialFilter<float> TPSFilter(5, 0);
ExponentialFilter<float> RPMFilter(5, 0);

/*============================================Sensor-Actuator====================================================*/
// Actuator pin assignment (Uno R3)
const int injectorPin      = 4;   // was 4 on Mega
const int fuelPumpRelay    = 5;   // was 5 on Mega
const int throttlePlatePin  = 6;  // DEV-003: PWM LED replaces the throttle servo
const int engineIndicator  = 7;   // was 7 on Mega
const int starterIndicator = 8;   // was 8 on Mega
const int safetyPin        = 9;   // DEV-002: was Mega pin 38, which does not exist on Uno

// Bench crank simulator outputs (DEV-004). Jumper to the ECU inputs:
//   D13 -> D2 (TDC), D12 -> D3 (pre-TDC)
const int crankTDCOut    = 13;
const int crankPreTDCOut = 12;

// Sensor pin assignment
const int hallTDC    = 2;  // INT0
const int hallPreTDC = 3;  // INT1
const int adcMAT     = A0;
const int adcCHT     = A1;
const int adcMAP     = A2;
const int adcLambda  = A3;
const int adcTPS     = A4;  // DEF-003: sampled but unused by the control path
const int adcPot     = A5;

// ADC variables
int valMAT = 0;
int valCHT = 0;
int valMAP = 0;
int valLambda = 0;
int valTPS = 0;
int valPot = 0;
int valPotRaw = 0;

// Real value variables
float f_MAT = 0;
float f_CHT = 0;
float f_MAP = 0;
float f_Lambda = 0;
float f_TPS = 0;

// Temporary variables for sensor measurement
volatile double t_ms, temp1, temp2;  // DEF-006: `time` shadows time() in avr-libc
double rpm;
float voltPress;

/* RPM Check variables */
volatile int isPreTDC = 0;
volatile int isTDC = 0;

/* Injector variables */
float interval;
unsigned int durationRegister = 0;
float ccm;
volatile unsigned long injectionCount = 0;
volatile unsigned long injectionSkipped = 0;

int count = 0;

/* AFR feedback variables */
float targetAFR;
float correction;
float eqRat;
float errorEqRat;
float controlSignal;
float targetEqRat;

/* Neural Network Variables */
float KP, KI, KD;
int samp = 1;
float ryev_del[2][5];
int k;

float w11, w12, w13, w21, w22, w23, w1, w2, w3;

float dj_dw1, dj_dw2, dj_dw3;
float dj_dw11, dj_dw12, dj_dw13, dj_dw21, dj_dw22, dj_dw23;
float learningRate = 0.001;
float iDelay = 0;
float ud3Delay = 0;
float lim = 1;
int l;
int tes1, tes2, tes3, tes4;

/* Engine status variables */
volatile double lastTemp;
volatile int startingCount = 0;
volatile int isEngineIdle = 0;
volatile int isStarting = 0;
int printEngine = 0;

float totalCons;
float totalTime;
float tempRPM;

/* Bench tick / crank simulator state (DEV-004) */
const unsigned int BENCH_TICK_HZ = 8000;
volatile unsigned int benchTicks500ms = 0;
volatile unsigned int crankTicksPerRev = 0;   // 0 disables the simulator
volatile unsigned int crankPreTDCTick = 0;
volatile unsigned int crankPhase = 0;
volatile unsigned int crankPulseTicks = 2;    // 250 us low pulse
unsigned int crankRPMSetpoint = 0;
unsigned int crankAdvanceDeg = 30;

/* Injection guard limits (DEF-004) */
const float INJ_MIN_US = 200.0;
const float INJ_MAX_US = 8000.0;

/* HMI transmit rate limit (DEF-005) */
const unsigned long HMI_PERIOD_MS = 100;
unsigned long lastHMI = 0;

/*================================= Functions and Voids Declarations ================================*/
int indexSearch(int value, const int arr[], int arrLength);
float ud1(int h);
float ud2(int h);
float ud3(int h);
float xd1(int h);
float xd2(int h);
float xd3(int h);
float satlin(float a);
float findDif(float a, float b);
void varStorage();
void find_dj_dwi();
void find_dj_dwij();
void updateWeight();

void idleCondition();
void sensorSamplingTask();
void injectionTask();
void checkTDC();
void checkPreTDC();
void runThrottleOutput();
void displayEngineState();
void printToHMI();
void setCrankRPM(unsigned int rpmSetpoint);
void setCrankAdvance(unsigned int deg);
void serviceBenchCommands();
void printBenchStatus();

/*================================SETUP=========================================*/
void setup() {
  t_ms = millis();

  pinMode(hallTDC, INPUT);
  pinMode(hallPreTDC, INPUT);

  pinMode(injectorPin, OUTPUT);
  pinMode(engineIndicator, OUTPUT);
  pinMode(starterIndicator, OUTPUT);
  pinMode(fuelPumpRelay, OUTPUT);
  pinMode(safetyPin, OUTPUT);
  pinMode(throttlePlatePin, OUTPUT);

  pinMode(crankTDCOut, OUTPUT);
  pinMode(crankPreTDCOut, OUTPUT);
  digitalWrite(crankTDCOut, HIGH);
  digitalWrite(crankPreTDCOut, HIGH);

  // DEF-007: the original enabled the A3 pull-up, which loads the lambda source.

  attachInterrupt(digitalPinToInterrupt(hallTDC), checkTDC, FALLING);
  attachInterrupt(digitalPinToInterrupt(hallPreTDC), checkPreTDC, FALLING);

  temp1 = 0;
  temp2 = 0;

  Serial.begin(115200);

  //Initialize Neural Network
  k = 0;

  w1 = 1;
  w2 = 0;
  w3 = 0;
  KP = w1;
  KI = w2;
  KD = w3;

  w11 = 1;
  w12 = 1;
  w13 = 1;

  w21 = -1;
  w22 = -1;
  w23 = -1;

  dj_dw1 = 0;
  dj_dw2 = 0;
  dj_dw3 = 0;

  dj_dw11 = 0;
  dj_dw12 = 0;
  dj_dw13 = 0;
  dj_dw21 = 0;
  dj_dw22 = 0;
  dj_dw23 = 0;

  targetEqRat = 0;
  eqRat = 0;
  errorEqRat = 0;
  correction = 0;

  for (int b = 0; b <= 4; b++) {
    for (int a = 0; a < 2; a++) {
      ryev_del[a][b] = 0;
    }
  }

  digitalWrite(fuelPumpRelay, LOW);

  // Prime pulse, identical to the Mega firmware
  digitalWrite(injectorPin, HIGH);
  delay(300);
  digitalWrite(injectorPin, LOW);
  totalTime = 500;

  // DEF-004: seed the injection duration so the first pre-TDC edge cannot load
  // a negative duration register.
  interval = baseInt[0];
  controlSignal = interval;

  // Timer 1: injector duration, CTC, stopped until injectionTask() starts it
  TCCR1A = 0;
  TCCR1B = (1 << WGM12);

  // Timer 2: 8 kHz bench tick (crank simulator + 500 ms engine-state task).
  // The Mega used Timer 4 for the 500 ms task; Timer 4 does not exist on Uno.
  TCCR2A = (1 << WGM21);                 // CTC
  TCCR2B = (1 << CS21);                  // /8 -> 2 MHz
  OCR2A  = 249;                          // 2 MHz / 250 = 8 kHz
  TCNT2  = 0;
  TIMSK2 = (1 << OCIE2A);

  Serial.println(F("# ECU uno_baseline"));
  Serial.println(F("# commands: R<rpm> A<advance_deg> ?"));
  Serial.print(F("t_ms")); Serial.print("\t");
  Serial.print(F("MAT")); Serial.print("\t");
  Serial.print(F("CHT")); Serial.print("\t");
  Serial.print(F("MAP")); Serial.print("\t");
  Serial.print(F("AFR")); Serial.print("\t");
  Serial.print(F("TPS")); Serial.print("\t");
  Serial.print(F("RPM")); Serial.print("\t");
  Serial.print(F("pulse_us")); Serial.print("\t");
  Serial.print(F("correction")); Serial.print("\t");
  Serial.println(F("state"));
}

/*================================MAIN LOOP=========================================*/
void loop() {
  serviceBenchCommands();
  runThrottleOutput();
  sensorSamplingTask();
  displayEngineState();   // DEF-008: after sampling, so start enrichment survives
  printToHMI();

  idleCondition();

  if (rpm > 4000) {
    count += 1;
    if (count == 20) {
      digitalWrite(safetyPin, HIGH);
      // DEF-009: the original blocked here for 5 s with delay()
      count = 0;
    }
  } else {
    count = 0;
    digitalWrite(safetyPin, LOW);
  }
}

void idleCondition() {
  /* Crank at TDC */
  if (isTDC == 1) {
    /* Calculate RPM */
    if (temp1 == temp2) {
      rpm = 0;
    } else {
      rpm = (60.0) * 1000000.0 / (temp1 - temp2);
    }

    if (rpm > 10000) {
      rpm = tempRPM;
    }
    RPMFilter.Filter(rpm);
    rpm = RPMFilter.Current();
    tempRPM = rpm;

    /* Look for Injector Duration */
    idxRPM = indexSearch(rpm, axisRPM, 16);
    idxTPS = indexSearch(f_TPS, axisTPS, 16);

    targetAFR = pgm_read_float(&(AFR[idxTPS][idxRPM]));  // DEF-010
    targetEqRat = 1;
    eqRat = 14.7 / f_Lambda;
    errorEqRat = targetEqRat - eqRat;

    k += 1;
    varStorage();

    find_dj_dwi();
    find_dj_dwij();

    if (k == samp) {
      updateWeight();
      if (isEngineIdle != 1) {
        correction = 0;
      } else {
        correction = constrain(200.0 * (w1 * xd1(k) + w2 * xd2(k) + w3 * xd3(k)), -100, 100);
      }
      ryev_del[0][1] = ryev_del[k][1];
      ryev_del[0][3] = ryev_del[k][3];
      ryev_del[k][3] = correction;
      k = 0;
    }

    controlSignal = interval + correction;

    totalTime += controlSignal / 1000;
    ccm = (rpm * controlSignal / 1000000.0) * (268.0 / 60.0);
    totalCons = (268.0 / 60.0) * totalTime / 1000;
    isTDC = 0;
  }

  if (isPreTDC == 1) {
    isPreTDC = 0;
  }
}

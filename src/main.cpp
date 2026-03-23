#include <Arduino.h>

#include <Wire.h>

// ── Pin Definitions ─────────────────────────────────────────────
#define SDA_PIN  25
#define SCL_PIN  26
#define BUZ      15
#define VIB      27
#define POT      32
#define CUR      34
#define TEM      35
#define LED_GR   2
#define LED_RE   4
#define LED0     16
#define LED1     17
#define LED2     18
#define LED3     19
#define LED4     21

// ── Limits per motor index (0–4) ────────────────────────────────
float currentLimit[5] = {0.5, 0.8, 1.2, 1.5, 2.0};
float tempLimit[5]    = {40,  50,  60,  70,  80};
float vibLimit[5]     = {1.0, 2.0, 3.0, 4.0, 5.0};

int motorLEDs[5] = {LED0, LED1, LED2, LED3, LED4};

// ── Global state ─────────────────────────────────────────────────
int   motorIdx  = 0;
float current   = 0;
float vibration = 0;
float temp      = 0;

// ════════════════════════════════════════════════════════════════
//  SENSOR READING FUNCTIONS
// ════════════════════════════════════════════════════════════════

int motorIndex() {
  int raw = analogRead(POT);
  return map(raw, 0, 4095, 0, 4);
}

// ACS712-5A: 185 mV/A sensitivity, ~1.65V at 0A on 3.3V rail
float getCurrent() {
  long sum = 0;
  for (int i = 0; i < 10; i++) { sum += analogRead(CUR); delay(1); }
  float voltage = (sum / 10.0) * (3.3 / 4095.0);
  float amps    = (voltage - 1.65) / 0.185;
  return abs(amps);
}

// SW-420: counts rising edges over 250ms window → scaled 0.0–5.0
float getVibration() {
  int  pulseCount = 0;
  bool lastState  = LOW;
  unsigned long startTime = millis();

  while (millis() - startTime < 250) {
    bool state = digitalRead(VIB);
    if (state == HIGH && lastState == LOW) {  // rising edge only
      pulseCount++;
    }
    lastState = state;
    delay(5);  // 5ms debounce → max ~50 edges detectable
  }

  const int maxPulses = 20;  // tune this to your motor's vibration level
  return constrain((pulseCount / (float)maxPulses) * 5.0, 0.0, 5.0);
}

// LM35: 10 mV/°C
float getTemp() {
  long sum = 0;
  for (int i = 0; i < 10; i++) { sum += analogRead(TEM); delay(1); }
  float voltage = (sum / 10.0) * (3.3 / 4095.0);
  return voltage * 100.0;
}

// ════════════════════════════════════════════════════════════════
//  FAULT CHECK
//  0=OK | 1=Overcurrent | 2=Overvibration | 3=Overtemp | 4=Multiple
// ════════════════════════════════════════════════════════════════
int checkFault(float cur, float vib, float tmp) {
  bool fCur = cur > currentLimit[motorIdx];
  bool fVib = vib > vibLimit[motorIdx];
  bool fTmp = tmp > tempLimit[motorIdx];

  int count = (int)fCur + (int)fVib + (int)fTmp;
  if (count > 1) return 4;
  if (fCur)      return 1;
  if (fVib)      return 2;
  if (fTmp)      return 3;
  return 0;
}

// ════════════════════════════════════════════════════════════════
//  ALARM PATTERN ENGINE
// ════════════════════════════════════════════════════════════════
void blinkBeep(int onMs, int offMs) {
  digitalWrite(LED_RE, HIGH);
  digitalWrite(BUZ,    HIGH);
  delay(onMs);
  digitalWrite(LED_RE, LOW);
  digitalWrite(BUZ,    LOW);
  delay(offMs);
}

// Case 1 – Overcurrent: ● ● ● pause  (3 short rapid pulses)
void patternOvercurrent() {
  for (int i = 0; i < 3; i++) blinkBeep(100, 100);
  delay(600);
}

// Case 2 – Overvibration: ━━  ━━  pause  (2 slow long blinks)
void patternOvervibration() {
  for (int i = 0; i < 2; i++) blinkBeep(500, 300);
  delay(700);
}

// Case 3 – Overtemperature: ━━━━━ ● pause  (1 long + 1 short)
void patternOvertemp() {
  blinkBeep(800, 200);
  blinkBeep(100, 100);
  delay(800);
}

// Case 4 – Multiple faults: ● ● ● ● ● pause  (5 frantic rapid pulses)
void patternMultiFault() {
  for (int i = 0; i < 5; i++) blinkBeep(80, 80);
  delay(300);
}

// ════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════
void setMotorLEDs(int index) {
  for (int i = 0; i < 5; i++)
    digitalWrite(motorLEDs[i], i == index ? HIGH : LOW);
}

void clearAlarm() {
  digitalWrite(LED_RE, LOW);
  digitalWrite(LED_GR, HIGH);
  digitalWrite(BUZ,    LOW);
}

// ════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════
void setup() {
  // Suppress GPIO15 boot pull-up glitch — must be before pinMode
  digitalWrite(BUZ, LOW);

  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  // Set ADC range to 0–3.3V for all analog pins (default is only 0–1.1V)
  analogSetAttenuation(ADC_11db);

  pinMode(LED_GR, OUTPUT);
  pinMode(LED_RE, OUTPUT);
  pinMode(BUZ,    OUTPUT);
  pinMode(VIB,    INPUT);
  for (int i = 0; i < 5; i++) pinMode(motorLEDs[i], OUTPUT);

  clearAlarm();
  Serial.println("=== Motor Protection System Ready ===");
}

// ════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ════════════════════════════════════════════════════════════════
void loop() {
  motorIdx  = motorIndex();
  current   = getCurrent();
  vibration = getVibration();
  temp      = getTemp();

  setMotorLEDs(motorIdx);

  int error = checkFault(current, vibration, temp);

  switch (error) {

    case 0:
      clearAlarm();
      Serial.printf("[Motor %d] OK | I=%.2fA  Vib=%.2f  T=%.1f°C\n",
                    motorIdx, current, vibration, temp);
      delay(700);  // ~270ms already spent reading sensors → ~1s total
      break;

    case 1:
      digitalWrite(LED_GR, LOW);
      Serial.printf("[Motor %d] ⚠ OVERCURRENT! I=%.2fA > Limit %.2fA\n",
                    motorIdx, current, currentLimit[motorIdx]);
      patternOvercurrent();
      break;

    case 2:
      digitalWrite(LED_GR, LOW);
      Serial.printf("[Motor %d] ⚠ OVERVIBRATION! Vib=%.2f > Limit %.2f\n",
                    motorIdx, vibration, vibLimit[motorIdx]);
      patternOvervibration();
      break;

    case 3:
      digitalWrite(LED_GR, LOW);
      Serial.printf("[Motor %d] ⚠ OVERTEMPERATURE! T=%.1f°C > Limit %.1f°C\n",
                    motorIdx, temp, tempLimit[motorIdx]);
      patternOvertemp();
      break;

    default:
      digitalWrite(LED_GR, LOW);
      Serial.printf("[Motor %d] ⛔ MULTIPLE FAULTS! I=%.2fA  Vib=%.2f  T=%.1f°C\n",
                    motorIdx, current, vibration, temp);
      patternMultiFault();
      break;
  }
}
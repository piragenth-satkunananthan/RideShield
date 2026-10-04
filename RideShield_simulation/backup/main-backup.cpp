#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <math.h>

// ============================================================
// RideShield - Semi-Final Core Safety Proof-of-Concept
// Wokwi / ESP32-S3
//
// What is REAL in the simulation:
//   - ESP32-S3 firmware/state machine
//   - MPU6050 I2C readings
//   - helmet-worn two-input logic
//   - crash verification logic
//   - countdown + cancel path
//   - event severity index
//   - buzzer/LED outputs
//   - microSD event + black-box logging
//
// What is intentionally SIMULATED as test inputs:
//   - GPS availability/location/speed
//   - LTE network availability / SMS delivery
//
// This avoids pretending that Wokwi validates GNSS/cellular RF hardware.
// ============================================================

// ---------------- Pins ----------------
static const uint8_t PIN_HEAD_PRESENT = 4;
static const uint8_t PIN_STRAP        = 5;
static const uint8_t PIN_CANCEL       = 6;
static const uint8_t PIN_BUZZER       = 7;

static const uint8_t PIN_SDA          = 8;
static const uint8_t PIN_SCL          = 9;

static const uint8_t PIN_SD_CS        = 10;
static const uint8_t PIN_SD_MOSI      = 11;
static const uint8_t PIN_SD_SCK       = 12;
static const uint8_t PIN_SD_MISO      = 13;

static const uint8_t PIN_CRASH_TEST   = 14;
static const uint8_t PIN_LED_WORN     = 15;
static const uint8_t PIN_LED_WARNING  = 16;
static const uint8_t PIN_LED_ALERT    = 17;

static const uint8_t PIN_GPS_FIX      = 18;
static const uint8_t PIN_LTE_NETWORK  = 21;
static const uint8_t PIN_SPEED_POT    = 1;
static const uint8_t PIN_RESET_DEMO   = 40;

// ---------------- MPU6050 ----------------
static const uint8_t MPU_ADDR = 0x68;

struct ImuData {
  float ax, ay, az;       // g
  float gx, gy, gz;       // deg/s
  float accelMag;         // g
  float gyroMag;          // deg/s
  bool valid;
};

ImuData imu = {0};

// ---------------- GPS simulation ----------------
// Fixed demo coordinate. In the real prototype these values come from GNSS.
static const double DEMO_LAT = 6.927079;
static const double DEMO_LON = 79.861244;

bool gpsHasEverFixed = false;
double lastLat = 0.0;
double lastLon = 0.0;
float lastGpsSpeed = 0.0f;
unsigned long lastGpsFixMs = 0;

// ---------------- Black-box ring buffer ----------------
struct Sample {
  uint32_t ms;
  float ax, ay, az;
  float gx, gy, gz;
  float speedKph;
  uint8_t worn;
};

static const int BLACKBOX_SAMPLES = 100; // 5 seconds @ 20 Hz
Sample blackbox[BLACKBOX_SAMPLES];
int bbHead = 0;
int bbCount = 0;

// Frozen snapshot captured immediately when a crash candidate starts.
// This prevents the 10-second rider countdown from overwriting pre-event data.
Sample eventBlackbox[BLACKBOX_SAMPLES];
int eventBlackboxCount = 0;

// ---------------- State machine ----------------
enum SystemState {
  STATE_NOT_WORN,
  STATE_READY,
  STATE_VERIFYING,
  STATE_COUNTDOWN,
  STATE_EMERGENCY
};

SystemState state = STATE_NOT_WORN;

const char* stateName(SystemState s) {
  switch (s) {
    case STATE_NOT_WORN:  return "NOT_WORN";
    case STATE_READY:     return "READY";
    case STATE_VERIFYING: return "VERIFYING";
    case STATE_COUNTDOWN: return "COUNTDOWN";
    case STATE_EMERGENCY: return "EMERGENCY";
  }
  return "UNKNOWN";
}

// ---------------- Event data ----------------
uint32_t eventId = 0;
bool injectedCrash = false;
unsigned long verifyStartMs = 0;
unsigned long countdownStartMs = 0;
int lastCountdownNumber = -1;

float peakAccel = 0.0f;
float peakGyro = 0.0f;
float eventPreSpeed = 0.0f;
bool eventStillness = false;

bool pendingAlert = false;
uint32_t pendingAlertEvent = 0;
int pendingAlertScore = 0;
String pendingAlertLevel = "";

// ---------------- Timing ----------------
unsigned long lastSampleMs = 0;
unsigned long lastStatusMs = 0;
unsigned long lastGpsUpdateMs = 0;

static const uint32_t SAMPLE_INTERVAL_MS = 50;   // 20 Hz black-box capture
static const uint32_t STATUS_INTERVAL_MS = 1000;
static const uint32_t VERIFY_WINDOW_MS = 1200;
static const uint32_t COUNTDOWN_MS = 10000;

// Crash thresholds for POC only. These require real-world validation.
static const float ACCEL_CANDIDATE_G = 2.5f;
static const float GYRO_CANDIDATE_DPS = 250.0f;

// ---------------- SD ----------------
bool sdReady = false;

// ---------------- Button edge states ----------------
bool lastCrashPressed = false;
bool lastCancelPressed = false;
bool lastResetPressed = false;

// ============================================================
// Helpers
// ============================================================

bool headPresent() {
  return digitalRead(PIN_HEAD_PRESENT) == HIGH;
}

bool strapFastened() {
  return digitalRead(PIN_STRAP) == HIGH;
}

bool helmetWorn() {
  return headPresent() && strapFastened();
}

bool gpsFixAvailable() {
  return digitalRead(PIN_GPS_FIX) == HIGH;
}

bool lteAvailable() {
  return digitalRead(PIN_LTE_NETWORK) == HIGH;
}

float readSpeedPotKph() {
  int raw = analogRead(PIN_SPEED_POT);
  return (raw / 4095.0f) * 120.0f;
}

void setOutputs() {
  digitalWrite(PIN_LED_WORN, helmetWorn() ? HIGH : LOW);
  digitalWrite(PIN_LED_WARNING, state == STATE_VERIFYING || state == STATE_COUNTDOWN ? HIGH : LOW);
  digitalWrite(PIN_LED_ALERT, state == STATE_EMERGENCY ? HIGH : LOW);
}

void beep(uint16_t freq, uint16_t durationMs) {
  tone(PIN_BUZZER, freq, durationMs);
}

void stopBuzzer() {
  noTone(PIN_BUZZER);
}

void mpuWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

bool initMPU6050() {
  Wire.begin(PIN_SDA, PIN_SCL);
  delay(50);

  // Wake device
  mpuWrite(0x6B, 0x00);
  delay(20);

  // Accelerometer range ±16 g
  mpuWrite(0x1C, 0x18);

  // Gyroscope range ±2000 deg/s
  mpuWrite(0x1B, 0x18);

  // Low-pass filter: moderate filtering for noisy motion
  mpuWrite(0x1A, 0x03);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75); // WHO_AM_I
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU_ADDR, 1, true) != 1) return false;
  uint8_t who = Wire.read();
  return (who == 0x68 || who == 0x69);
}

bool readMPU6050(ImuData &d) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) {
    d.valid = false;
    return false;
  }

  int count = Wire.requestFrom((int)MPU_ADDR, 14, true);
  if (count != 14) {
    d.valid = false;
    return false;
  }

  int16_t rawAx = (Wire.read() << 8) | Wire.read();
  int16_t rawAy = (Wire.read() << 8) | Wire.read();
  int16_t rawAz = (Wire.read() << 8) | Wire.read();

  // temperature bytes
  Wire.read();
  Wire.read();

  int16_t rawGx = (Wire.read() << 8) | Wire.read();
  int16_t rawGy = (Wire.read() << 8) | Wire.read();
  int16_t rawGz = (Wire.read() << 8) | Wire.read();

  // Sensitivity for selected ranges:
  // ±16g => 2048 LSB/g
  // ±2000dps => 16.4 LSB/(deg/s)
  d.ax = rawAx / 2048.0f;
  d.ay = rawAy / 2048.0f;
  d.az = rawAz / 2048.0f;

  d.gx = rawGx / 16.4f;
  d.gy = rawGy / 16.4f;
  d.gz = rawGz / 16.4f;

  d.accelMag = sqrtf(d.ax*d.ax + d.ay*d.ay + d.az*d.az);
  d.gyroMag = sqrtf(d.gx*d.gx + d.gy*d.gy + d.gz*d.gz);
  d.valid = true;
  return true;
}

bool isStill(const ImuData &d) {
  if (!d.valid) return false;
  bool accelNearGravity = d.accelMag > 0.75f && d.accelMag < 1.30f;
  bool lowRotation = d.gyroMag < 40.0f;
  return accelNearGravity && lowRotation;
}

void pushBlackboxSample() {
  Sample &s = blackbox[bbHead];
  s.ms = millis();
  s.ax = imu.ax; s.ay = imu.ay; s.az = imu.az;
  s.gx = imu.gx; s.gy = imu.gy; s.gz = imu.gz;
  s.speedKph = gpsHasEverFixed ? lastGpsSpeed : 0.0f;
  s.worn = helmetWorn() ? 1 : 0;

  bbHead = (bbHead + 1) % BLACKBOX_SAMPLES;
  if (bbCount < BLACKBOX_SAMPLES) bbCount++;
}

void freezeBlackboxSnapshot() {
  eventBlackboxCount = bbCount;
  int start = (bbHead - bbCount + BLACKBOX_SAMPLES) % BLACKBOX_SAMPLES;

  for (int i = 0; i < eventBlackboxCount; i++) {
    int idx = (start + i) % BLACKBOX_SAMPLES;
    eventBlackbox[i] = blackbox[idx];
  }

  Serial.print("[BLACKBOX] Frozen pre-event snapshot: ");
  Serial.print(eventBlackboxCount);
  Serial.println(" samples");
}

void ensureCsvHeaders() {
  if (!sdReady) return;

  if (!SD.exists("/events.csv")) {
    File f = SD.open("/events.csv", FILE_WRITE);
    if (f) {
      f.println("event_id,time_ms,outcome,severity_score,severity_level,peak_accel_g,peak_gyro_dps,pre_speed_kph,stillness,gps_valid,latitude,longitude,lte_available");
      f.close();
    }
  }

  if (!SD.exists("/blackbox.csv")) {
    File f = SD.open("/blackbox.csv", FILE_WRITE);
    if (f) {
      f.println("event_id,sample_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,speed_kph,helmet_worn");
      f.close();
    }
  }
}

void logEventSummary(const char* outcome, int score, const String &level) {
  if (!sdReady) {
    Serial.println("[SD] Not available - event summary not written");
    return;
  }

  File f = SD.open("/events.csv", FILE_APPEND);
  if (!f) {
    Serial.println("[SD] Failed to open events.csv");
    return;
  }

  bool fixNow = gpsFixAvailable();
  double lat = fixNow ? DEMO_LAT : (gpsHasEverFixed ? lastLat : 0.0);
  double lon = fixNow ? DEMO_LON : (gpsHasEverFixed ? lastLon : 0.0);
  bool hasLoc = fixNow || gpsHasEverFixed;

  f.print(eventId); f.print(',');
  f.print(millis()); f.print(',');
  f.print(outcome); f.print(',');
  f.print(score); f.print(',');
  f.print(level); f.print(',');
  f.print(peakAccel, 2); f.print(',');
  f.print(peakGyro, 1); f.print(',');
  f.print(eventPreSpeed, 1); f.print(',');
  f.print(eventStillness ? 1 : 0); f.print(',');
  f.print(hasLoc ? 1 : 0); f.print(',');
  f.print(lat, 6); f.print(',');
  f.print(lon, 6); f.print(',');
  f.println(lteAvailable() ? 1 : 0);

  f.close();
  Serial.println("[SD] Event summary written to /events.csv");
}

void saveBlackbox() {
  if (!sdReady) {
    Serial.println("[SD] Not available - black-box buffer not written");
    return;
  }

  File f = SD.open("/blackbox.csv", FILE_APPEND);
  if (!f) {
    Serial.println("[SD] Failed to open blackbox.csv");
    return;
  }

  for (int i = 0; i < eventBlackboxCount; i++) {
    const Sample &s = eventBlackbox[i];

    f.print(eventId); f.print(',');
    f.print(s.ms); f.print(',');
    f.print(s.ax, 3); f.print(',');
    f.print(s.ay, 3); f.print(',');
    f.print(s.az, 3); f.print(',');
    f.print(s.gx, 2); f.print(',');
    f.print(s.gy, 2); f.print(',');
    f.print(s.gz, 2); f.print(',');
    f.print(s.speedKph, 1); f.print(',');
    f.println(s.worn);
  }

  f.close();
  Serial.print("[SD] Saved ");
  Serial.print(eventBlackboxCount);
  Serial.println(" frozen pre-event black-box samples");
}

void printFile(const char* path) {
  if (!sdReady) {
    Serial.println("[SD] Card unavailable");
    return;
  }

  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.print("[SD] Cannot open ");
    Serial.println(path);
    return;
  }

  Serial.print("\n===== ");
  Serial.print(path);
  Serial.println(" =====");
  while (f.available()) {
    Serial.write(f.read());
  }
  Serial.println("===== END =====\n");
  f.close();
}

void updateGpsSimulation() {
  if (millis() - lastGpsUpdateMs < 250) return;
  lastGpsUpdateMs = millis();

  if (gpsFixAvailable()) {
    gpsHasEverFixed = true;
    lastLat = DEMO_LAT;
    lastLon = DEMO_LON;
    lastGpsSpeed = readSpeedPotKph();
    lastGpsFixMs = millis();
  }
}

String severityLevel(int score) {
  if (score < 35) return "LOW";
  if (score < 70) return "MODERATE";
  return "HIGH";
}

int calculateSeverityScore(bool noRiderResponse) {
  // All terms normalized 0..1.
  // Peak acceleration is capped at MPU6050's measurable range.
  float accelNorm = constrain((peakAccel - 2.5f) / (16.0f - 2.5f), 0.0f, 1.0f);
  float gyroNorm  = constrain(peakGyro / 1200.0f, 0.0f, 1.0f);
  float speedNorm = constrain(eventPreSpeed / 100.0f, 0.0f, 1.0f);
  float stillNorm = eventStillness ? 1.0f : 0.0f;
  float responseNorm = noRiderResponse ? 1.0f : 0.0f;

  float score =
      0.35f * accelNorm +
      0.20f * gyroNorm +
      0.20f * speedNorm +
      0.15f * stillNorm +
      0.10f * responseNorm;

  return (int)roundf(constrain(score * 100.0f, 0.0f, 100.0f));
}

void printLocationForAlert() {
  if (gpsFixAvailable()) {
    Serial.print("  GPS: CURRENT FIX  ");
    Serial.print(DEMO_LAT, 6);
    Serial.print(", ");
    Serial.println(DEMO_LON, 6);
  } else if (gpsHasEverFixed) {
    Serial.print("  GPS: LAST KNOWN   ");
    Serial.print(lastLat, 6);
    Serial.print(", ");
    Serial.print(lastLon, 6);
    Serial.print("  age=");
    Serial.print((millis() - lastGpsFixMs) / 1000.0f, 1);
    Serial.println("s");
  } else {
    Serial.println("  GPS: LOCATION UNAVAILABLE");
  }
}

void transmitEmergencyAlert(uint32_t id, int score, const String &level) {
  Serial.println("\n============================================");
  Serial.println("       RIDESHIELD EMERGENCY ALERT");
  Serial.println("============================================");
  Serial.print("  Event ID: ");
  Serial.println(id);
  Serial.print("  Event severity: ");
  Serial.print(level);
  Serial.print(" (");
  Serial.print(score);
  Serial.println("/100)");
  Serial.print("  Pre-event speed: ");
  Serial.print(eventPreSpeed, 1);
  Serial.println(" km/h");
  printLocationForAlert();

  if (lteAvailable()) {
    Serial.println("  LTE: NETWORK AVAILABLE");
    Serial.println("  SMS: SENT SUCCESSFULLY (SIMULATED MODEM)");
    pendingAlert = false;
    beep(2400, 600);
  } else {
    Serial.println("  LTE: NO NETWORK");
    Serial.println("  SMS: QUEUED FOR RETRY");
    pendingAlert = true;
    pendingAlertEvent = id;
    pendingAlertScore = score;
    pendingAlertLevel = level;
  }
  Serial.println("============================================\n");
}

void startCrashCandidate(bool injected) {
  if (!helmetWorn()) {
    Serial.println("[CRASH] Event ignored: helmet is not verified as worn");
    beep(500, 150);
    return;
  }

  eventId++;
  injectedCrash = injected;
  verifyStartMs = millis();

  // Freeze the ring buffer immediately, before verification/countdown.
  freezeBlackboxSnapshot();

  eventPreSpeed = gpsHasEverFixed ? lastGpsSpeed : 0.0f;
  peakAccel = imu.valid ? imu.accelMag : 0.0f;
  peakGyro = imu.valid ? imu.gyroMag : 0.0f;
  eventStillness = false;

  // Deterministic test injection for judges/demo.
  // It enters the same verification/countdown path as a sensor event.
  if (injectedCrash) {
    peakAccel = max(peakAccel, 7.5f);
    peakGyro  = max(peakGyro, 650.0f);
  }

  state = STATE_VERIFYING;
  setOutputs();

  Serial.println("\n[CRASH] Candidate detected");
  Serial.print("[CRASH] Source: ");
  Serial.println(injectedCrash ? "DEMO INJECTION BUTTON" : "MPU6050");
  Serial.print("[CRASH] Pre-event speed: ");
  Serial.print(eventPreSpeed, 1);
  Serial.println(" km/h");
  beep(1500, 200);
}

void beginCountdown() {
  state = STATE_COUNTDOWN;
  countdownStartMs = millis();
  lastCountdownNumber = -1;
  setOutputs();

  Serial.println("[CRASH] Candidate CONFIRMED by multi-factor check");
  Serial.println("[COUNTDOWN] 10 seconds to cancel emergency alert");
  beep(1800, 250);
}

void cancelEvent() {
  stopBuzzer();
  int score = calculateSeverityScore(false);
  String level = severityLevel(score);

  Serial.println("\n[CANCEL] Rider cancelled the emergency alert");
  Serial.print("[CANCEL] Event score for record: ");
  Serial.print(score);
  Serial.print("/100 ");
  Serial.println(level);

  logEventSummary("CANCELLED", score, level);
  saveBlackbox();

  state = helmetWorn() ? STATE_READY : STATE_NOT_WORN;
  setOutputs();
}

void confirmEmergency() {
  stopBuzzer();

  int score = calculateSeverityScore(true);
  String level = severityLevel(score);

  state = STATE_EMERGENCY;
  setOutputs();

  Serial.println("\n[EMERGENCY] Crash event CONFIRMED");
  Serial.print("[EMERGENCY] Crash Event Severity Index: ");
  Serial.print(score);
  Serial.print("/100 -> ");
  Serial.print(level);
  Serial.println("-ENERGY EVENT");
  Serial.println("[EMERGENCY] This is NOT a medical injury assessment.");

  logEventSummary("CONFIRMED", score, level);
  saveBlackbox();
  transmitEmergencyAlert(eventId, score, level);
}

void resetDemo() {
  stopBuzzer();
  pendingAlert = false;
  state = helmetWorn() ? STATE_READY : STATE_NOT_WORN;
  setOutputs();

  Serial.println("\n[RESET] Demo state reset");
  Serial.print("[RESET] New state: ");
  Serial.println(stateName(state));
}

void processSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'L' || c == 'l') {
      printFile("/events.csv");
    } else if (c == 'B' || c == 'b') {
      printFile("/blackbox.csv");
    } else if (c == 'R' || c == 'r') {
      resetDemo();
    } else if (c == 'H' || c == 'h' || c == '?') {
      Serial.println("\nCommands: L=event log, B=black-box log, R=reset demo, H=help\n");
    }
  }
}

void printStatus() {
  Serial.print("[STATUS] state=");
  Serial.print(stateName(state));
  Serial.print(" worn=");
  Serial.print(helmetWorn() ? "YES" : "NO");
  Serial.print(" head=");
  Serial.print(headPresent() ? "YES" : "NO");
  Serial.print(" strap=");
  Serial.print(strapFastened() ? "YES" : "NO");

  Serial.print(" | MPU a=");
  if (imu.valid) {
    Serial.print(imu.accelMag, 2);
    Serial.print("g gyro=");
    Serial.print(imu.gyroMag, 0);
    Serial.print("dps");
  } else {
    Serial.print("INVALID");
  }

  Serial.print(" | GPS=");
  Serial.print(gpsFixAvailable() ? "FIX" : "NO_FIX");
  Serial.print(" speed=");
  Serial.print(gpsHasEverFixed ? lastGpsSpeed : 0.0f, 1);
  Serial.print("km/h");

  Serial.print(" | LTE=");
  Serial.print(lteAvailable() ? "ONLINE" : "OFFLINE");

  Serial.print(" | SD=");
  Serial.println(sdReady ? "OK" : "FAIL");
}

// ============================================================
// Arduino setup
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_HEAD_PRESENT, INPUT);
  pinMode(PIN_STRAP, INPUT);
  pinMode(PIN_GPS_FIX, INPUT);
  pinMode(PIN_LTE_NETWORK, INPUT);

  pinMode(PIN_CANCEL, INPUT_PULLUP);
  pinMode(PIN_CRASH_TEST, INPUT_PULLUP);
  pinMode(PIN_RESET_DEMO, INPUT_PULLUP);

  pinMode(PIN_LED_WORN, OUTPUT);
  pinMode(PIN_LED_WARNING, OUTPUT);
  pinMode(PIN_LED_ALERT, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  analogReadResolution(12);

  Serial.println();
  Serial.println("================================================");
  Serial.println("     RideShield Core Safety POC - Wokwi");
  Serial.println("================================================");

  bool mpuOk = initMPU6050();
  Serial.print("[BOOT] MPU6050: ");
  Serial.println(mpuOk ? "OK (±16g, ±2000dps)" : "FAILED");

  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sdReady = SD.begin(PIN_SD_CS, SPI);
  Serial.print("[BOOT] microSD: ");
  Serial.println(sdReady ? "OK" : "FAILED");
  ensureCsvHeaders();

  state = helmetWorn() ? STATE_READY : STATE_NOT_WORN;
  setOutputs();

  Serial.println("[BOOT] GPS and LTE are interactive SIMULATION inputs");
  Serial.println("[BOOT] X=Crash Test button, C=Cancel, R=Reset Demo");
  Serial.println("[BOOT] Serial commands: L=events, B=black-box, R=reset, H=help");
  Serial.println();
}

// ============================================================
// Arduino loop
// ============================================================

void loop() {
  processSerialCommands();
  updateGpsSimulation();

  // Button edges
  bool crashPressedNow = (digitalRead(PIN_CRASH_TEST) == LOW);
  bool cancelPressedNow = (digitalRead(PIN_CANCEL) == LOW);
  bool resetPressedNow = (digitalRead(PIN_RESET_DEMO) == LOW);

  bool crashPressedEdge = crashPressedNow && !lastCrashPressed;
  bool cancelPressedEdge = cancelPressedNow && !lastCancelPressed;
  bool resetPressedEdge = resetPressedNow && !lastResetPressed;

  lastCrashPressed = crashPressedNow;
  lastCancelPressed = cancelPressedNow;
  lastResetPressed = resetPressedNow;

  if (resetPressedEdge) {
    resetDemo();
  }

  // Sample MPU and maintain black-box history
  if (millis() - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = millis();

    readMPU6050(imu);
    pushBlackboxSample();

    // Detect a physical/simulated MPU event only in normal ready state.
    if (state == STATE_READY && imu.valid) {
      if (imu.accelMag >= ACCEL_CANDIDATE_G || imu.gyroMag >= GYRO_CANDIDATE_DPS) {
        startCrashCandidate(false);
      }
    }

    // Continue collecting event peaks during verification.
    if (state == STATE_VERIFYING && imu.valid) {
      peakAccel = max(peakAccel, imu.accelMag);
      peakGyro = max(peakGyro, imu.gyroMag);
    }
  }

  // Keep READY / NOT_WORN state aligned with helmet inputs when idle
  if (state == STATE_READY || state == STATE_NOT_WORN) {
    SystemState desired = helmetWorn() ? STATE_READY : STATE_NOT_WORN;
    if (desired != state) {
      state = desired;
      setOutputs();
      Serial.print("[HELMET] State changed -> ");
      Serial.println(stateName(state));
      beep(helmetWorn() ? 1200 : 600, 120);
    }
  }

  // Deterministic demo crash button
  if (crashPressedEdge && (state == STATE_READY || state == STATE_NOT_WORN)) {
    startCrashCandidate(true);
  }

  // Verification window
  if (state == STATE_VERIFYING && millis() - verifyStartMs >= VERIFY_WINDOW_MS) {
    eventStillness = isStill(imu);

    int evidence = 0;
    if (peakAccel >= ACCEL_CANDIDATE_G) evidence++;
    if (peakGyro >= GYRO_CANDIDATE_DPS) evidence++;
    if (eventPreSpeed >= 8.0f) evidence++;
    if (eventStillness) evidence++;

    Serial.print("[VERIFY] peakAccel=");
    Serial.print(peakAccel, 2);
    Serial.print("g peakGyro=");
    Serial.print(peakGyro, 0);
    Serial.print("dps speed=");
    Serial.print(eventPreSpeed, 1);
    Serial.print(" still=");
    Serial.print(eventStillness ? "YES" : "NO");
    Serial.print(" evidence=");
    Serial.println(evidence);

    // Injected event is guaranteed to proceed for a reliable live demo.
    // Real MPU event requires acceleration evidence + at least 3 total factors.
    bool confirmedCandidate =
      injectedCrash ||
      (peakAccel >= ACCEL_CANDIDATE_G && evidence >= 3);

    if (confirmedCandidate) {
      beginCountdown();
    } else {
      Serial.println("[VERIFY] Candidate rejected as insufficient evidence");
      logEventSummary("REJECTED", 0, "NONE");
      state = helmetWorn() ? STATE_READY : STATE_NOT_WORN;
      setOutputs();
      beep(700, 120);
    }
  }

  // Rider cancel during countdown
  if (state == STATE_COUNTDOWN && cancelPressedEdge) {
    cancelEvent();
  }

  // Non-blocking countdown
  if (state == STATE_COUNTDOWN) {
    unsigned long elapsed = millis() - countdownStartMs;
    int secondsLeft = 10 - (int)(elapsed / 1000UL);

    if (secondsLeft < 0) secondsLeft = 0;

    if (secondsLeft != lastCountdownNumber) {
      lastCountdownNumber = secondsLeft;
      Serial.print("[COUNTDOWN] ");
      Serial.println(secondsLeft);
      if (secondsLeft > 0) beep(1900, 100);
    }

    if (elapsed >= COUNTDOWN_MS) {
      confirmEmergency();
    }
  }

  // If LTE was unavailable, retry queued emergency when network returns
  if (pendingAlert && lteAvailable()) {
    Serial.println("[LTE] Network restored -> retrying queued emergency alert");
    transmitEmergencyAlert(pendingAlertEvent, pendingAlertScore, pendingAlertLevel);
  }

  // Press CANCEL after emergency as an acknowledgement shortcut
  if (state == STATE_EMERGENCY && cancelPressedEdge) {
    Serial.println("[EMERGENCY] Alert acknowledged. Press RESET DEMO for a clean restart.");
    beep(900, 120);
  }

  setOutputs();

  if (millis() - lastStatusMs >= STATUS_INTERVAL_MS) {
    lastStatusMs = millis();
    printStatus();
  }

  delay(2);
}

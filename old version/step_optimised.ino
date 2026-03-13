// TMC2208 / generic STEP/DIR control with LCD Keypad Shield
// - Auto homing on power-up
// - Logical coordinates: 0–140 cm (home switch at 35 cm)
// - Shield buttons: presets | RIGHT = re-home
// - A2/A3: forward/backward jog (hold for continuous, tap for 0.5 cm)
// - Limits: A4 = max (140 cm), A5 = home switch (~35 cm)
// - Auto motor disable after idle timeout (reduces heat)

#include <LiquidCrystal.h>

// ── Pins ──────────────────────────────────────────────
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
#define STEP_PIN     12
#define DIR_PIN      11
#define EN_PIN       13
#define BTN_FORWARD  A2
#define BTN_BACKWARD A3
#define LIMIT_MAX    A4
#define LIMIT_HOME   A5

// ════════════════════════════════════════════════════════
// ── CHANGE SPEEDS HERE ─────────────────────────────────
//    Higher number = SLOWER
//    Lower number  = FASTER
//    Safe range: 100 (fastest) to 800 (slowest)
// ════════════════════════════════════════════════════════

constexpr uint16_t HOME_SPEED_US   = 440;   // homing
constexpr uint16_t HOLD_SPEED_US   = 100;   // hold button jog
constexpr uint16_t TAP_SPEED_US    = 400;   // single tap 0.5 cm
constexpr uint16_t TRAVEL_SPEED_US = 400;   // preset buttons

// ════════════════════════════════════════════════════════
// ── AUTO DISABLE ───────────────────────────────────────
#define AUTO_DISABLE
constexpr uint32_t IDLE_TIMEOUT_MS = 5000;
// ════════════════════════════════════════════════════════

// ── Config ────────────────────────────────────────────
constexpr float STEPS_PER_CM         = 400.0f;
constexpr float TAP_DISTANCE_CM      = 0.5f;
constexpr int   STEPS_PER_TAP        = (int)(TAP_DISTANCE_CM * STEPS_PER_CM);
constexpr float MAX_RAIL_CM          = 140.0f;
constexpr float HOME_POSITION_CM     = 25.0f;
constexpr float SAFE_CLEARANCE_CM    = 0.1f;
constexpr int   SAFE_CLEARANCE_STEPS = (int)(SAFE_CLEARANCE_CM * STEPS_PER_CM);

// ── Presets ───────────────────────────────────────────
constexpr float PRESET_45  = 45.0f;
constexpr float PRESET_58  = 58.0f;
constexpr float PRESET_81  = 81.0f;
constexpr float PRESET_130 = 139.0f;

// ── State ─────────────────────────────────────────────
float    currentPosition_cm = 0.0f;
uint32_t lastDisplayUpdate  = 0;
uint32_t lastMoveTime       = 0;
bool     motorEnabled       = true;
bool     lastFwdState       = true;
bool     lastBwdState       = true;

// ── Helpers ───────────────────────────────────────────

void enableMotor() {
  if (!motorEnabled) {
    digitalWrite(EN_PIN, LOW);
    delayMicroseconds(200);
    motorEnabled = true;
  }
  lastMoveTime = millis();
}

void disableMotor() {
  if (motorEnabled) {
    digitalWrite(EN_PIN, HIGH);
    motorEnabled = false;
    lcd.setCursor(0, 1);
    lcd.print("-- IDLE/OFF --  ");
  }
}

inline void stepOnce(uint16_t halfPeriodUs) {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(halfPeriodUs);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(halfPeriodUs);
}

inline bool limitHit(bool forward) {
  return digitalRead(forward ? LIMIT_MAX : LIMIT_HOME) == LOW;
}

inline void setDir(bool forward) {
  digitalWrite(DIR_PIN, forward ? LOW : HIGH);
}

inline void clampPosition() {
  currentPosition_cm = constrain(currentPosition_cm, 0.0f, MAX_RAIL_CM);
}

int read_LCD_buttons() {
  int adc = analogRead(A0);
  if (adc > 1000) return -1;
  if (adc <  50)  return 0;
  if (adc < 195)  return 1;
  if (adc < 380)  return 2;
  if (adc < 555)  return 3;
  if (adc < 790)  return 4;
  return -1;
}

// ── Display ───────────────────────────────────────────

void updateDisplay(const char* status = "Idle") {
  int whole = (int)currentPosition_cm;
  int frac  = (int)roundf((currentPosition_cm - whole) * 10.0f);
  if (frac >= 10) { whole++; frac = 0; }
  whole = max(whole, 0);

  int pct = (int)roundf(currentPosition_cm / MAX_RAIL_CM * 100.0f);
  pct = constrain(pct, 0, 100);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pos: ");
  lcd.print(whole); lcd.print("."); lcd.print(frac);
  lcd.print(" cm");
  lcd.setCursor(0, 1);
  lcd.print(pct); lcd.print("% "); lcd.print(status);
}

// ── Motion ────────────────────────────────────────────

long moveSteps(long steps, bool forward, uint16_t speedUs) {
  enableMotor();
  setDir(forward);
  delayMicroseconds(20);
  long taken = 0;
  for (; taken < steps; taken++) {
    if (limitHit(forward)) break;
    stepOnce(speedUs);
    lastMoveTime = millis();
  }
  return taken;
}

void homeToZero() {
  enableMotor();
  setDir(false);
  delayMicroseconds(20);
  while (digitalRead(LIMIT_HOME) == HIGH)
    stepOnce(HOME_SPEED_US);

  setDir(true);
  delayMicroseconds(20);
  for (int i = 0; i < SAFE_CLEARANCE_STEPS; i++)
    stepOnce(HOME_SPEED_US);

  currentPosition_cm = HOME_POSITION_CM;
  lastMoveTime = millis();
  lcd.setCursor(0, 1);
  lcd.print("Homed          ");
}

void doSingleMove(bool forward) {
  bool tooFar = forward ? (currentPosition_cm >= MAX_RAIL_CM - TAP_DISTANCE_CM + 0.01f)
                        : (currentPosition_cm <= TAP_DISTANCE_CM - 0.01f);
  if (tooFar || limitHit(forward)) {
    lcd.clear(); lcd.print(forward ? "Max limit!" : "Min limit!");
    delay(800);
    updateDisplay();
    return;
  }

  long taken = moveSteps(STEPS_PER_TAP, forward, TAP_SPEED_US);
  float delta = taken / STEPS_PER_CM;
  currentPosition_cm += forward ? delta : -delta;
  clampPosition();
  updateDisplay();
}

void moveToPreset(float target, const char* label) {
  target = constrain(target, 0.0f, MAX_RAIL_CM);

  lcd.clear();
  lcd.print("To "); lcd.print(label);
  lcd.setCursor(0, 1); lcd.print("Moving...");

  bool forward = (target > currentPosition_cm);
  long total   = (long)(fabsf(target - currentPosition_cm) * STEPS_PER_CM + 0.5f);
  long taken   = moveSteps(total, forward, TRAVEL_SPEED_US);

  currentPosition_cm += (forward ? 1.0f : -1.0f) * taken / STEPS_PER_CM;
  clampPosition();
  updateDisplay();
}

// ── Setup ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Starting...");
  lcd.setCursor(0, 1);
  lcd.print("Homing...");

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN,  OUTPUT);
  pinMode(EN_PIN,   OUTPUT);
  pinMode(BTN_FORWARD,  INPUT_PULLUP);
  pinMode(BTN_BACKWARD, INPUT_PULLUP);
  pinMode(LIMIT_MAX,    INPUT_PULLUP);
  pinMode(LIMIT_HOME,   INPUT_PULLUP);

  digitalWrite(EN_PIN,   LOW);
  digitalWrite(STEP_PIN, LOW);
  motorEnabled = true;

  homeToZero();
  updateDisplay();
}

// ── Loop ──────────────────────────────────────────────

void loop() {

#ifdef AUTO_DISABLE
  if (motorEnabled && (millis() - lastMoveTime >= IDLE_TIMEOUT_MS)) {
    disableMotor();
  }
#endif

  int btn = read_LCD_buttons();
  switch (btn) {
    case 4: moveToPreset(PRESET_130, "130 cm"); break;
    case 1: moveToPreset(PRESET_45,  "45 cm");  break;
    case 2: moveToPreset(PRESET_58,  "58 cm");  break;
    case 3: moveToPreset(PRESET_81,  "81 cm");  break;
    case 0:
      lcd.clear(); lcd.print("Homing...");
      homeToZero();
      updateDisplay();
      delay(400);
      while (read_LCD_buttons() == 0) {}
      break;
  }

  bool fwd = (digitalRead(BTN_FORWARD)  == LOW);
  bool bwd = (digitalRead(BTN_BACKWARD) == LOW);

  if (fwd && !bwd && !limitHit(true) && currentPosition_cm < MAX_RAIL_CM - 0.05f) {
    enableMotor();
    setDir(true);
    stepOnce(HOLD_SPEED_US);
    currentPosition_cm += 1.0f / STEPS_PER_CM;
    if (millis() - lastDisplayUpdate >= 500) {
      lastDisplayUpdate = millis();
      updateDisplay("Jog");
    }
    return;
  }

  if (bwd && !fwd && !limitHit(false) && currentPosition_cm > 0.05f) {
    enableMotor();
    setDir(false);
    stepOnce(HOLD_SPEED_US);
    currentPosition_cm -= 1.0f / STEPS_PER_CM;
    if (millis() - lastDisplayUpdate >= 500) {
      lastDisplayUpdate = millis();
      updateDisplay("Jog");
    }
    return;
  }

  if (!fwd && lastFwdState && !bwd) doSingleMove(true);
  if (!bwd && lastBwdState && !fwd) doSingleMove(false);

  lastFwdState = fwd;
  lastBwdState = bwd;

  delay(5);
}

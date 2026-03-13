// ╔══════════════════════════════════════════════════════════════════╗
// ║  Slider.ino  — Camera Slider  (TMC2208 v2 standalone)            ║
// ║                                                                  ║
// ║  FIXES vs previous version:                                      ║
// ║  [1] DIR_PIN logic INVERTED — HIGH=forward, LOW=backward         ║
// ║      (hardware reality was opposite of original assumption)      ║
// ║  [2] homeToZero() direct DIR write corrected (LOW not HIGH)      ║
// ║  [3] calibrateRail() replaces simple homing on startup:          ║
// ║        Step 1 — drive FWD  to A5 (25cm),  back off              ║
// ║        Step 2 — drive BWD  to A4 (140cm), back off              ║
// ║        Step 3 — measure actual rail length, warn if off          ║
// ║        Step 4 — set currentCM = HOME_POSITION_CM                 ║
// ║  [4] MIN_RAIL_CM (25cm) used instead of 0cm throughout          ║
// ║  [5] All WebSerial integration preserved                         ║
// ║                                                                  ║
// ║  COORDINATE SYSTEM                                               ║
// ║    A5 (LIMIT_MIN)  = 25 cm  ← forward/motor end                 ║
// ║    A4 (LIMIT_HOME) = 140 cm ← backward/home end                 ║
// ║    DIR HIGH = forward  → toward A5   (position DECREASES)       ║
// ║    DIR LOW  = backward → toward A4   (position INCREASES)       ║
// ╚══════════════════════════════════════════════════════════════════╝

#include <LiquidCrystal.h>
#include "calib.h"
#include "config.h"

#define WS_JOG_CM      0.05f   // 0.5mm web jog increment
#define WS_SOFT_MIN_CM MIN_RAIL_CM
#define WS_SOFT_MAX_CM MAX_RAIL_CM

#include "WebSerial.h"

// ── LCD ──────────────────────────────────────────────────────────────
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// ── Derived constants ────────────────────────────────────────────────
static const int BACKOFF_STEPS = (int)(BACKOFF_CM      * STEPS_PER_CM);
static const int TAP_STEPS     = (int)(TAP_DISTANCE_CM * STEPS_PER_CM);

// ── Runtime rail limits (extern declared in calib.h) ─────────────────
//  Initialised to nominals — calibrateRail() overwrites with
//  belt-measured values on every boot.
float railMinCM  = NOMINAL_MIN_CM;
float railMaxCM  = NOMINAL_MAX_CM;
float railHomeCM = NOMINAL_MAX_CM;

// ── State ────────────────────────────────────────────────────────────
// NOT static — WebSerial.h needs extern linkage
float    currentCM       = NOMINAL_MAX_CM;   // updated after calibrateRail()

static bool     motorEnabled    = false;
// Set true after calibrateRail() — causes the first homeToZero() call
// (which the web page fires on connect) to skip motion and just confirm
// position. Cleared after that first call so subsequent HOME commands
// work normally.
static bool     freshFromCalib  = false;
static uint32_t lastMoveTime    = 0;
static uint32_t lastDisplayTime = 0;

// Debounce state
static int      rawLcdBtn       = -1;
static int      debouncedBtn    = -1;
static uint32_t btnStableTime   = 0;
static bool     btnFired        = false;
static bool     lastRightState  = false;
static const uint16_t BTN_DEBOUNCE_MS = 60;

static bool     lastFwdState    = false;
static bool     lastBwdState    = false;
static uint16_t rampCounter     = 0;

// currentDir: false = DIR LOW  = backward = toward A4 (140cm, position increases)
//             true  = DIR HIGH = forward  = toward A5 ( 25cm, position decreases)
static bool     currentDir      = false;

// ════════════════════════════════════════════════════════════════════
// ── Limit helpers ────────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════
inline bool limitMinHit()  { return digitalRead(LIMIT_MIN)  == LOW; }
inline bool limitHomeHit() { return digitalRead(LIMIT_HOME) == LOW; }

// ════════════════════════════════════════════════════════════════════
// ── Motor enable / disable ───────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════
void enableMotor() {
  if (!motorEnabled) {
    digitalWrite(EN_PIN, LOW);
    delayMicroseconds(200);
    motorEnabled = true;
    rampCounter  = 0;
    WS_DBG("MOTOR: ENABLED");
  }
  lastMoveTime = millis();
}

void disableMotor() {
  if (motorEnabled) {
    digitalWrite(EN_PIN, HIGH);
    motorEnabled = false;
    WS_DBG("MOTOR: DISABLED (idle)");
  }
}

// ════════════════════════════════════════════════════════════════════
// ── Direction ────────────────────────────────────────────────────────
//
//  *** BUG FIX ***
//  Original code:  forward ? LOW  : HIGH
//  Fixed code:     forward ? HIGH : LOW
//
//  Reason: on this board DIR_PIN HIGH drives the motor toward A5
//  (the forward/motor end).  The original assumption was inverted.
// ════════════════════════════════════════════════════════════════════
void setDir(bool forward) {
  if (forward != currentDir || !motorEnabled) {
    digitalWrite(DIR_PIN, forward ? HIGH : LOW);   // ← FIXED (was LOW : HIGH)
    delayMicroseconds(2000);
    rampCounter = 0;
    currentDir  = forward;
  }
}

// ════════════════════════════════════════════════════════════════════
// ── Soft-start ramp ──────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════
inline uint16_t rampedUs(uint16_t targetUs) {
  if (targetUs >= RAMP_START_US || rampCounter >= RAMP_STEPS)
    return targetUs;
  uint32_t us = RAMP_START_US -
                ((uint32_t)(RAMP_START_US - targetUs) * rampCounter) / RAMP_STEPS;
  rampCounter++;
  return (uint16_t)us;
}

// ════════════════════════════════════════════════════════════════════
// ── Step pulse ───────────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════
inline void stepOnce(uint16_t halfUs) {
  uint16_t us = rampedUs(halfUs);
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(us);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(us);
}

// ════════════════════════════════════════════════════════════════════
// ── Position clamp  (uses MIN_RAIL_CM, not 0) ────────────────────────
// ════════════════════════════════════════════════════════════════════
inline void clampPos() {
  if (currentCM < MIN_RAIL_CM) currentCM = MIN_RAIL_CM;
  if (currentCM > MAX_RAIL_CM) currentCM = MAX_RAIL_CM;
}

// ════════════════════════════════════════════════════════════════════
// ── Display ──────────────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════
void updateDisplay(const char* msg, bool force = false) {
  uint32_t now = millis();
  if (!force && (now - lastDisplayTime < DISPLAY_PERIOD_MS)) return;
  lastDisplayTime = now;

  int whole = (int)currentCM;
  int frac  = (int)((currentCM - whole) * 10.0f + 0.5f);
  if (frac >= 10) { whole++; frac = 0; }

  lcd.setCursor(0, 0);
  if (WS_ESTOP_ACTIVE) {
    lcd.print("** ESTOP ACTIVE *");
  } else {
    lcd.print("Pos:");
    lcd.print(whole); lcd.print("."); lcd.print(frac);
    lcd.print(" cm        ");
  }

  lcd.setCursor(0, 1);
  lcd.print(motorEnabled ? msg : "-- IDLE/OFF --");
  lcd.print("         ");
}

// ════════════════════════════════════════════════════════════════════
// ── LCD button reader  0=RIGHT 1=UP 2=DOWN 3=LEFT 4=SELECT -1=none ─
// ════════════════════════════════════════════════════════════════════
int readLcdButton() {
  int v = analogRead(LCD_BTN);
  if (v > BTN_SELECT_MAX + 50) return -1;
  if (v < BTN_RIGHT_MAX)       return 0;
  if (v < BTN_UP_MAX)          return 1;
  if (v < BTN_DOWN_MAX)        return 2;
  if (v < BTN_LEFT_MAX)        return 3;
  if (v < BTN_SELECT_MAX)      return 4;
  return -1;
}

// ════════════════════════════════════════════════════════════════════
// ── ESTOP helper (used inside blocking move loops) ───────────────────
// ════════════════════════════════════════════════════════════════════
static bool checkEstopSerial() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "ESTOP") {
      disableMotor();
      WS_POS(currentCM);
      updateDisplay("!! E-STOP !!", true);
      return true;
    }
  }
  return false;
}

// ════════════════════════════════════════════════════════════════════
// ── STARTUP CALIBRATION (two-switch sweep) ───────────────────────────
//
//  Called ONCE from setup().
//
//  Step 1 — Drive FORWARD to A5 (motor/min end, 25cm)
//             Confirms limit switch works and sets reference.
//  Step 2 — Back off A5 switch
//  Step 3 — Drive BACKWARD to A4 (home/max end, 140cm)
//             Counts steps from A5 backoff position to A4 trigger.
//  Step 4 — Back off A4 switch
//  Step 5 — Measure actual rail length, print to Serial
//           (warns if > CAL_TOLERANCE_CM away from expected)
//  Step 6 — Set currentCM = HOME_POSITION_CM (140cm)
// ════════════════════════════════════════════════════════════════════
void calibrateRail() {
  WS_DBG("CAL: start");
  enableMotor();

  // ── Step 1: find A5 (forward / motor end) ───────────────────────
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Cal: find A5");
  lcd.setCursor(0, 1); lcd.print("-> motor end...");
  WS_DBG("CAL: driving forward to A5");

  // Drive forward (DIR HIGH) toward A5
  digitalWrite(DIR_PIN, HIGH);          // forward = DIR HIGH on this board
  delayMicroseconds(2000);
  currentDir  = true;
  rampCounter = 0;

  while (!limitMinHit()) {
    stepOnce(SPEED_CAL_US);
    if (checkEstopSerial()) return;
  }
  WS_DBG("CAL: A5 hit (motor end, 25cm)");
  lcd.setCursor(0, 1); lcd.print("A5 hit!         ");
  delay(300);

  // ── Step 2: back off A5 ─────────────────────────────────────────
  WS_DBG("CAL: backoff from A5");
  setDir(false);   // backward (DIR LOW)
  rampCounter = 0;
  for (int i = 0; i < BACKOFF_STEPS; i++) {
    stepOnce(SPEED_CAL_US);
    if (checkEstopSerial()) return;
    if (limitHomeHit()) { WS_DBG("CAL: A4 hit during A5 backoff!"); break; }
  }

  // ── Step 3: drive backward to A4, counting steps ────────────────
  lcd.setCursor(0, 0); lcd.print("Cal: find A4");
  lcd.setCursor(0, 1); lcd.print("-> home end... ");
  WS_DBG("CAL: driving backward to A4 (counting steps)");

  setDir(false);   // backward (DIR LOW)
  rampCounter = 0;
  long calSteps  = 0;
  bool a4Found   = false;

  while (calSteps < CAL_MAX_STEPS) {
    stepOnce(SPEED_CAL_US);
    calSteps++;
    if (checkEstopSerial()) return;
    if (limitHomeHit()) { a4Found = true; break; }
  }

  if (!a4Found) {
    // Safety: A4 never triggered — fall back to nominal limits
    Serial.println("#CAL: ERROR — A4 not found within max steps! Using nominal limits.");
    lcd.setCursor(0, 0); lcd.print("CAL ERROR!      ");
    lcd.setCursor(0, 1); lcd.print("A4 not found!   ");
    railMinCM  = NOMINAL_MIN_CM;
    railMaxCM  = NOMINAL_MAX_CM;
    railHomeCM = NOMINAL_MAX_CM;
    currentCM  = railHomeCM;
    lastMoveTime = millis();
    WS_POS(currentCM);
    delay(2000);
    updateDisplay("NOMINAL limits!", true);
    return;
  }

  WS_DBG("CAL: A4 hit (home end)");
  lcd.setCursor(0, 1); lcd.print("A4 hit!         ");
  delay(300);

  // ── Step 4: back off A4 ─────────────────────────────────────────
  WS_DBG("CAL: backoff from A4");
  setDir(true);   // forward
  rampCounter = 0;
  for (int i = 0; i < BACKOFF_STEPS; i++) {
    stepOnce(SPEED_CAL_US);
    if (checkEstopSerial()) return;
    if (limitMinHit()) { WS_DBG("CAL: A5 hit during A4 backoff!"); break; }
  }

  // ── Step 5: compute belt-measured rail limits ────────────────────
  //
  //  calSteps = steps driven from A5-backoff-point to A4 trigger.
  //  Subtract BACKOFF_STEPS (the A5 backoff) to get true A5→A4 span.
  //
  //  span_cm   = (calSteps - BACKOFF_STEPS) / STEPS_PER_CM
  //  railMinCM = NOMINAL_MIN_CM + MIN_SWITCH_OFFSET_CM  (A5 = fixed ref)
  //  railMaxCM = railMinCM + span_cm                    (A4 derived)
  //  railHomeCM= railMaxCM
  // ────────────────────────────────────────────────────────────────
  float spanCM   = (float)(calSteps - BACKOFF_STEPS) / STEPS_PER_CM;
  float errorCM  = fabsf(spanCM - NOMINAL_SPAN_CM);

  railMinCM  = NOMINAL_MIN_CM + MIN_SWITCH_OFFSET_CM;
  railMaxCM  = railMinCM + spanCM;
  railHomeCM = railMaxCM;

  Serial.print("#CAL: belt-measured span = "); Serial.print(spanCM, 2);
  Serial.print(" cm  |  railMin = ");          Serial.print(railMinCM, 1);
  Serial.print(" cm  railMax = ");             Serial.print(railMaxCM, 1);
  Serial.print(" cm  |  error vs nominal = "); Serial.print(errorCM, 2);
  Serial.println(" cm");

  if (errorCM > CAL_TOLERANCE_CM) {
    Serial.print("#CAL: WARNING — span differs from nominal by ");
    Serial.print(errorCM, 2);
    Serial.println(" cm. Measured value is still used.");
    lcd.setCursor(0, 0); lcd.print("CAL WARN        ");
    lcd.setCursor(0, 1);
    lcd.print(spanCM, 1); lcd.print("cm span  ");
    delay(2000);
  } else {
    lcd.setCursor(0, 0); lcd.print("Cal OK          ");
    lcd.setCursor(0, 1);
    lcd.print(spanCM, 1); lcd.print("cm span  ");
    delay(800);
  }

  // ── Step 6: set position to measured home ───────────────────────
  currentCM    = railHomeCM;
  lastMoveTime = millis();
  WS_POS(currentCM);

  Serial.print("#CAL: done — currentCM = "); Serial.print(currentCM, 1); Serial.println(" cm");
  freshFromCalib = true;   // skip next homeToZero() motion (web connect HOME)
  updateDisplay("Homed!", true);
  delay(400);
}

// ════════════════════════════════════════════════════════════════════
// ── HOMING (RIGHT button — fast single-switch re-home to A4) ─────────
// ════════════════════════════════════════════════════════════════════
void homeToZero() {
  // If calibrateRail() just ran (e.g. web page auto-sends HOME on connect),
  // skip motion — we are already at the correct home position.
  if (freshFromCalib) {
    freshFromCalib = false;
    WS_DBG("HOME: skipped — already homed by calibrateRail()");
    WS_POS(currentCM);
    updateDisplay("Ready", true);
    return;
  }

  WS_DBG("HOME: start");

  enableMotor();
  updateDisplay("Homing...", true);

  // Phase 1: drive BACKWARD (DIR LOW) toward A4 (140cm)
  // *** BUG FIX: was HIGH, corrected to LOW for this board ***
  digitalWrite(DIR_PIN, LOW);          // backward = DIR LOW on this board
  delayMicroseconds(2000);
  currentDir  = false;                 // false = backward
  rampCounter = 0;

  while (!limitHomeHit()) {
    if (checkEstopSerial()) return;
    stepOnce(SPEED_HOME_US);
  }
  WS_DBG("HOME: A4 triggered");

  // Phase 2: back off FORWARD (DIR HIGH) to release A4 switch
  setDir(true);   // forward = DIR HIGH
  for (int i = 0; i < BACKOFF_STEPS; i++) {
    stepOnce(SPEED_HOME_US);
    if (checkEstopSerial()) return;
    if (limitMinHit()) { WS_DBG("HOME: A5 safety hit during backoff"); break; }
  }

  currentCM    = HOME_POSITION_CM;
  lastMoveTime = millis();
  WS_POS(currentCM);
  updateDisplay("Homed!", true);
  WS_DBG("HOME: done — 140.0cm");
  delay(600);
}

// ════════════════════════════════════════════════════════════════════
// ── goHome — smooth approach then switch-lock ────────────────────────
// ════════════════════════════════════════════════════════════════════
void goHome() {
  if (fabsf(currentCM - HOME_POSITION_CM) > HOME_NEAR_CM) {
    moveToPreset(HOME_POSITION_CM, "Home");
  }
  homeToZero();
  updateDisplay("Ready", true);
}

// ════════════════════════════════════════════════════════════════════
// ── Tap move (nudge after jog release) ───────────────────────────────
// ════════════════════════════════════════════════════════════════════
void doTapMove(bool forward) {
  // Guard: already at limit?
  if (forward  && (currentCM <= MIN_RAIL_CM + TAP_DISTANCE_CM - 0.01f || limitMinHit())) {
    updateDisplay("Min limit!", true); delay(400); updateDisplay("Ready", true); return;
  }
  if (!forward && (currentCM >= MAX_RAIL_CM - TAP_DISTANCE_CM + 0.01f || limitHomeHit())) {
    updateDisplay("Max limit!", true); delay(400); updateDisplay("Ready", true); return;
  }

  enableMotor();
  setDir(forward);

  long taken = 0;
  for (; taken < TAP_STEPS; taken++) {
    if (forward ? limitMinHit() : limitHomeHit()) break;
    if (checkEstopSerial()) {
      currentCM += (forward ? -1.0f : 1.0f) * ((float)taken / STEPS_PER_CM);
      clampPos();
      WS_POS(currentCM);
      return;
    }
    stepOnce(SPEED_TAP_US);
    lastMoveTime = millis();
  }

  currentCM += (forward ? -1.0f : 1.0f) * ((float)taken / STEPS_PER_CM);
  clampPos();
  WS_POS(currentCM);
  updateDisplay("Ready", true);
}

// ════════════════════════════════════════════════════════════════════
// ── Preset move ──────────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════
void moveToPreset(float targetCM, const char* label) {
  if (targetCM < MIN_RAIL_CM) targetCM = MIN_RAIL_CM;
  if (targetCM > MAX_RAIL_CM) targetCM = MAX_RAIL_CM;

  if (fabsf(targetCM - currentCM) < 0.3f) {
    updateDisplay("Already there", true);
    WS_POS(currentCM);
    delay(400);
    updateDisplay("Ready", true);
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("Go: "); lcd.print(label);
  if (label[strlen(label)-1] != 'm') lcd.print("cm");
  lcd.print("    ");
  lcd.setCursor(0, 1);
  lcd.print("RIGHT=Abort     ");

  bool  forward = (targetCM < currentCM);
  long  total   = (long)(fabsf(targetCM - currentCM) * STEPS_PER_CM + 0.5f);
  float startCM = currentCM;

  lastFwdState = false;
  lastBwdState = false;

  enableMotor();
  setDir(forward);

  long     taken   = 0;
  uint32_t lastRpt = 0;

  for (; taken < total; taken++) {
    if (forward ? limitMinHit() : limitHomeHit()) {
      WS_DBG("PRESET: hard limit hit");
      break;
    }
    if (readLcdButton() == 0) {          // RIGHT = abort
      WS_DBG("PRESET: ABORTED");
      updateDisplay("Aborted!", true);
      delay(400);
      currentCM = startCM + (forward ? -1.0f : 1.0f) * ((float)taken / STEPS_PER_CM);
      clampPos();
      WS_POS(currentCM);
      lastFwdState = false;
      lastBwdState = false;
      homeToZero();
      updateDisplay("Ready", true);
      lastRightState = true;
      btnFired       = true;
      return;
    }
    if (checkEstopSerial()) {
      currentCM = startCM + (forward ? -1.0f : 1.0f) * ((float)taken / STEPS_PER_CM);
      clampPos();
      WS_POS(currentCM);
      return;
    }

    stepOnce(SPEED_TRAVEL_US);
    lastMoveTime = millis();

    // Intermediate position broadcast every 150ms
    if (millis() - lastRpt >= 150) {
      float mid = startCM + (forward ? -1.0f : 1.0f) * ((float)(taken + 1) / STEPS_PER_CM);
      if (mid < MIN_RAIL_CM) mid = MIN_RAIL_CM;
      if (mid > MAX_RAIL_CM) mid = MAX_RAIL_CM;
      WS_POS(mid);
      lastRpt = millis();
    }
  }

  currentCM = startCM + (forward ? -1.0f : 1.0f) * ((float)taken / STEPS_PER_CM);
  clampPos();
  WS_POS(currentCM);
  updateDisplay("Ready", true);
}

// ════════════════════════════════════════════════════════════════════
// ── Setup ────────────────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("CameraSlider");
  lcd.setCursor(0, 1);
  lcd.print("Waiting serial..");

  pinMode(EN_PIN,       OUTPUT);
  pinMode(STEP_PIN,     OUTPUT);
  pinMode(DIR_PIN,      OUTPUT);
  pinMode(BTN_FORWARD,  INPUT_PULLUP);
  pinMode(BTN_BACKWARD, INPUT_PULLUP);
  pinMode(LIMIT_MIN,    INPUT_PULLUP);
  pinMode(LIMIT_HOME,   INPUT_PULLUP);

  // Motor OFF, direction set backward (toward A4 = DIR LOW on this board)
  digitalWrite(EN_PIN,   HIGH);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN,  LOW);
  motorEnabled = false;
  currentDir   = false;

  // ── Wait for Serial connection before doing anything ─────────────
  //  Carriage will NOT move until you open Serial monitor or the
  //  web page connects. LCD counts up seconds while waiting.
  //  Blink the LCD so it's obvious the board is alive but idle.
  {
    uint32_t blinkTime = millis();
    uint8_t  dots      = 0;
    while (!Serial) {
      if (millis() - blinkTime >= 500) {
        blinkTime = millis();
        lcd.setCursor(0, 1);
        lcd.print("Waiting serial");
        for (uint8_t i = 0; i < dots; i++) lcd.print(".");
        lcd.print("  ");
        dots = (dots + 1) % 4;
      }
    }
  }

  lcd.setCursor(0, 1);
  lcd.print("Calibrating...  ");
  delay(300);

  // ── Full two-switch calibration (only runs once, after connect) ───
  calibrateRail();

  updateDisplay("Ready", true);
  WS_INIT(currentCM);   // sends READY + POS to web controller
}

// ════════════════════════════════════════════════════════════════════
// ── Loop ─────────────────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════
void loop() {
  WS_TICK(currentCM);

  // ── Auto-disable after idle ──────────────────────────────────────
  if (motorEnabled && (millis() - lastMoveTime >= IDLE_TIMEOUT_MS)) {
    disableMotor();
    updateDisplay("-- IDLE/OFF --", true);
  }

  bool fwd = (digitalRead(BTN_FORWARD)  == LOW);
  bool bwd = (digitalRead(BTN_BACKWARD) == LOW);

  // ── Debounced LCD button ─────────────────────────────────────────
  int nowBtn = readLcdButton();

  if (nowBtn != rawLcdBtn) {
    rawLcdBtn     = nowBtn;
    btnStableTime = millis();
    if (nowBtn == -1) btnFired = false;
  }

  bool btnPressed = (!btnFired
                  && rawLcdBtn != -1
                  && (millis() - btnStableTime) >= BTN_DEBOUNCE_MS);

  if (btnPressed) {
    btnFired     = true;
    debouncedBtn = rawLcdBtn;
  }

  // ── RIGHT: go home ───────────────────────────────────────────────
  bool rightNow = (debouncedBtn == 0 && btnPressed);
  if (rightNow) {
    debouncedBtn = -1;
    WS_DBG("LCD: RIGHT -> goHome()");
    goHome();
    lastFwdState = (digitalRead(BTN_FORWARD)  == LOW);
    lastBwdState = (digitalRead(BTN_BACKWARD) == LOW);
  }

  // ── Preset buttons ───────────────────────────────────────────────
  if (!WS_ESTOP_ACTIVE && btnPressed && debouncedBtn > 0) {
    int firedBtn = debouncedBtn;
    debouncedBtn = -1;
    switch (firedBtn) {
      case 1: moveToPreset(PRESET_A, "45cm");  break;  // UP
      case 2: moveToPreset(PRESET_B, "58cm");  break;  // DOWN
      case 3: moveToPreset(PRESET_C, "81cm");  break;  // LEFT
      case 4: moveToPreset(PRESET_D, "125cm"); break;  // SELECT
    }
    fwd = (digitalRead(BTN_FORWARD)  == LOW);
    bwd = (digitalRead(BTN_BACKWARD) == LOW);
    lastFwdState = fwd;
    lastBwdState = bwd;
    rawLcdBtn     = readLcdButton();
    btnStableTime = millis();
    btnFired      = true;
  }

  // ── Jog + tap-on-release (blocked during estop) ──────────────────
  if (!WS_ESTOP_ACTIVE) {

    // Forward jog — toward A5 (position decreases, stays above MIN_RAIL_CM)
    if (fwd && !bwd && !limitMinHit() && currentCM > MIN_RAIL_CM + 0.05f) {
      enableMotor();
      setDir(true);
      stepOnce(SPEED_JOG_US);
      currentCM -= 1.0f / STEPS_PER_CM;
      clampPos();
      updateDisplay("Jog Fwd ->25cm");
      lastFwdState = true;
      lastBwdState = false;
      return;
    }

    // Backward jog — toward A4 (position increases, stays below MAX_RAIL_CM)
    if (bwd && !fwd && !limitHomeHit() && currentCM < MAX_RAIL_CM - 0.05f) {
      enableMotor();
      setDir(false);
      stepOnce(SPEED_JOG_US);
      currentCM += 1.0f / STEPS_PER_CM;
      clampPos();
      updateDisplay("Jog Bwd ->140cm");
      lastFwdState = false;
      lastBwdState = true;
      return;
    }

    if (!fwd && lastFwdState && !bwd) doTapMove(true);
    if (!bwd && lastBwdState && !fwd) doTapMove(false);
  }

  lastFwdState = fwd;
  lastBwdState = bwd;

  delay(5);
}

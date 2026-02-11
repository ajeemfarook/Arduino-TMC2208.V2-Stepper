// TMC2208 single-axis control with TWO buttons + TWO limit switches + HOME button
// Position tracking in cm — rail = 0 to 114 cm
// AUTO HOMING at startup + MANUAL HOMING anytime via HOME button
// + LCD1602 16x2 display (parallel 4-bit mode) on Leonardo

#include <LiquidCrystal.h>

// ───── LCD Setup (pins chosen to avoid conflict with stepper) ─────
LiquidCrystal lcd(11, A0, A1, A2, A3, A4);  // RS, E, DB4, DB5, DB6, DB7

#define STEP_PIN       9
#define DIR_PIN       10
#define EN_PIN         8

#define BTN_FORWARD      6
#define BTN_BACKWARD     7
#define BTN_HOME         5       // press to go home (0 cm)

#define LIMIT_FORWARD   12       // LOW = hit (max / 114 cm)
#define LIMIT_BACKWARD  13       // LOW = hit (min / home / 0 cm)

#define STEP_DELAY_US    400     // normal running speed (µs)
#define HOMING_SPEED_US  550     // gentler during homing

// ───── Calibration ─────
// Confirmed: 200 steps = 0.5 cm  ⇒  400 steps = 1 cm
const float STEPS_PER_CM     = 400.0;          // steps per 1 cm
const float TAP_DISTANCE_CM  = 0.5;            // cm per tap
const int   STEPS_PER_TAP    = 200;            // steps per tap

const float MAX_RAIL_CM      = 114.0;
const float SAFE_CLEARANCE_CM = 0.3;           // back off ~3 mm after home hit

float currentPosition_cm = 0.0;

// For periodic display update during continuous move
unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL_MS = 500;   // update every 500 ms during hold

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  // Initialize LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Stepper Ready");
  lcd.setCursor(0, 1);
  lcd.print("Homing...");

  Serial.println("\n=== Stepper – 0.5 cm steps + HOME + LCD ===");
  Serial.println("Btn6 (6): hold = forward   | tap = +0.5 cm");
  Serial.println("Btn7 (7): hold = backward  | tap = -0.5 cm");
  Serial.println("Btn5 (5): press = go HOME (0 cm)");
  Serial.println("Limits: 12 = max,  13 = min/home");

  Serial.print("Calibration → ");
  Serial.print(STEPS_PER_CM, 1);
  Serial.print(" steps/cm   |   ");
  Serial.print(STEPS_PER_TAP);
  Serial.print(" steps = ");
  Serial.print(TAP_DISTANCE_CM, 2);
  Serial.println(" cm per tap\n");

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);

  pinMode(BTN_FORWARD,   INPUT_PULLUP);
  pinMode(BTN_BACKWARD,  INPUT_PULLUP);
  pinMode(BTN_HOME,      INPUT_PULLUP);
  pinMode(LIMIT_FORWARD, INPUT_PULLUP);
  pinMode(LIMIT_BACKWARD,INPUT_PULLUP);

  digitalWrite(EN_PIN, LOW);
  digitalWrite(STEP_PIN, LOW);

  Serial.println("Startup: homing to 0 cm...");
  homeToZero();
  Serial.println("Startup homing complete → at 0.0 cm");

  updateDisplay();    // Show initial position on LCD
  printPosition();
  Serial.println("----------------------------------------");
}

void loop() {
  bool btnFwd  = (digitalRead(BTN_FORWARD) == LOW);
  bool btnBwd  = (digitalRead(BTN_BACKWARD) == LOW);
  bool btnHome = (digitalRead(BTN_HOME) == LOW);

  bool atMax = (digitalRead(LIMIT_FORWARD) == LOW);
  bool atMin = (digitalRead(LIMIT_BACKWARD) == LOW);

  // HOME button pressed
  if (btnHome) {
    Serial.println("\nHOME button pressed → moving to 0 cm...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Homing...");
    homeToZero();
    Serial.println("Homing finished → now at home (0.0 cm)");
    updateDisplay();
    printPosition();
    delay(300);
    while (digitalRead(BTN_HOME) == LOW) delay(10);
    return;
  }

  bool moving = false;

  // Continuous hold - forward
  if (btnFwd && !atMax && currentPosition_cm < MAX_RAIL_CM - 0.05) {
    digitalWrite(DIR_PIN, LOW);
    stepOnce(STEP_DELAY_US);
    currentPosition_cm += 1.0 / STEPS_PER_CM;
    moving = true;
  }
  // Continuous hold - backward
  else if (btnBwd && !atMin && currentPosition_cm > 0.05) {
    digitalWrite(DIR_PIN, HIGH);
    stepOnce(STEP_DELAY_US);
    currentPosition_cm -= 1.0 / STEPS_PER_CM;
    moving = true;
  }

  // Periodic display update during continuous movement
  if (moving) {
    unsigned long now = millis();
    if (now - lastPrintTime >= PRINT_INTERVAL_MS) {
      lastPrintTime = now;
      updateDisplay();
      printPosition();   // keep Serial too
    }
  }

  // Tap detection
  static bool lastFwd = HIGH;
  bool currFwd = digitalRead(BTN_FORWARD);
  if (currFwd == LOW && lastFwd == HIGH && !btnBwd && !btnHome) {
    doSingleMove(true);
    lastPrintTime = millis();
  }
  lastFwd = currFwd;

  static bool lastBwd = HIGH;
  bool currBwd = digitalRead(BTN_BACKWARD);
  if (currBwd == LOW && lastBwd == HIGH && !btnFwd && !btnHome) {
    doSingleMove(false);
    lastPrintTime = millis();
  }
  lastBwd = currBwd;
}

// ────────────────────────────────────────────────

void stepOnce(int delayUs) {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(delayUs);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(delayUs);
}

void homeToZero() {
  digitalWrite(DIR_PIN, HIGH);   // backward

  while (digitalRead(LIMIT_BACKWARD) == HIGH) {
    stepOnce(HOMING_SPEED_US);
  }

  Serial.print("→ Home limit hit!  ");

  digitalWrite(DIR_PIN, LOW);   // forward = away from switch
  int clearanceSteps = (int)(SAFE_CLEARANCE_CM * STEPS_PER_CM + 0.5);

  for (int i = 0; i < clearanceSteps; i++) {
    stepOnce(HOMING_SPEED_US);
  }

  currentPosition_cm = 0.0;
  Serial.println("backed off → position = 0.0 cm");
}

void doSingleMove(bool forward) {
  Serial.print("Tap → ");

  if (forward) {
    Serial.print("+0.5 cm   ");
    if (currentPosition_cm >= MAX_RAIL_CM - TAP_DISTANCE_CM + 0.01 || digitalRead(LIMIT_FORWARD) == LOW) {
      Serial.println("BLOCKED (max/limit)");
      lcd.clear();
      lcd.print("Max limit!");
      return;
    }
  } else {
    Serial.print("-0.5 cm   ");
    if (currentPosition_cm <= TAP_DISTANCE_CM - 0.01 || digitalRead(LIMIT_BACKWARD) == LOW) {
      Serial.println("BLOCKED (min/limit)");
      lcd.clear();
      lcd.print("Min limit!");
      return;
    }
  }

  digitalWrite(DIR_PIN, forward ? LOW : HIGH);

  int limitPin = forward ? LIMIT_FORWARD : LIMIT_BACKWARD;
  bool hit = false;

  for (int i = 0; i < STEPS_PER_TAP; i++) {
    if (digitalRead(limitPin) == LOW) {
      hit = true;
      Serial.print("LIMIT → stopped early  ");
      lcd.clear();
      lcd.print("Limit hit!");
      break;
    }
    stepOnce(STEP_DELAY_US);
  }

  currentPosition_cm += forward ? TAP_DISTANCE_CM : -TAP_DISTANCE_CM;

  if (currentPosition_cm > MAX_RAIL_CM) currentPosition_cm = MAX_RAIL_CM;
  if (currentPosition_cm < 0) currentPosition_cm = 0;

  if (hit) {
    Serial.print("at ");
    Serial.print(currentPosition_cm, 1);
    Serial.println(" cm");
  } else {
    Serial.println("done");
  }

  updateDisplay();
  printPosition();
}

// ───── Shared display update (LCD + Serial) ─────
void updateDisplay() {
  int cm_whole    = (int)currentPosition_cm;
  int cm_fraction = (int)round((currentPosition_cm - cm_whole) * 10.0);

  if (cm_fraction < 0) cm_fraction = 0;
  if (cm_fraction >= 10) {
    cm_whole++;
    cm_fraction -= 10;
  }
  if (cm_whole < 0) cm_whole = 0;

  int percent = (int)round(currentPosition_cm / MAX_RAIL_CM * 100.0);
  percent = constrain(percent, 0, 100);

  // LCD output
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pos: ");
  lcd.print(cm_whole);
  lcd.print(".");
  lcd.print(cm_fraction);
  lcd.print(" cm");

  lcd.setCursor(0, 1);
  lcd.print(percent);
  lcd.print("%  ");

  if (digitalRead(BTN_FORWARD) == LOW) {
    lcd.print(">> Forward");
  } else if (digitalRead(BTN_BACKWARD) == LOW) {
    lcd.print("<< Backward");
  } else if (digitalRead(BTN_HOME) == LOW) {
    lcd.print("Homing...");
  } else {
    lcd.print("Idle");
  }
}

void printPosition() {
  int cm_whole    = (int)currentPosition_cm;
  int cm_fraction = (int)round((currentPosition_cm - cm_whole) * 10.0);
  if (cm_fraction < 0) cm_fraction = 0;
  if (cm_fraction >= 10) { cm_whole++; cm_fraction -= 10; }
  if (cm_whole < 0) cm_whole = 0;

  int percent = (int)round(currentPosition_cm / MAX_RAIL_CM * 100.0);
  percent = constrain(percent, 0, 100);

  Serial.print("Position: ");
  Serial.print(cm_whole);
  Serial.print(".");
  Serial.print(cm_fraction);
  Serial.print(" cm   (");
  Serial.print(percent);
  Serial.println(" %)");
}
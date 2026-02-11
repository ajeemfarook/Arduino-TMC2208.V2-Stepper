// TMC2208 single-axis control with TWO buttons + SAFETY LIMIT SWITCH
// Button 1 (pin 6)  → Forward while held
// Button 2 (pin 7)  → Backward while held
// Release both      → Stop
// Limit switch (pin 11, NO + GND) → STOP immediately if triggered (end of rail safety)

#define STEP_PIN     9
#define DIR_PIN     10
#define EN_PIN       8

#define BUTTON_FORWARD   6
#define BUTTON_BACKWARD  7

#define LIMIT_SWITCH    11      // ← Safety limit switch (NO wired to this pin + GND)

#define STEP_DELAY_US  600   // adjust: higher = slower/safer, lower = faster (test carefully)

bool motorEnabled = true;     // We'll disable if limit hit (optional)

void setup() {
  Serial.begin(115200);
  Serial.println("Button + Limit Switch ready");
  Serial.println("Btn6 = Forward | Btn7 = Backward | Release = Stop");
  Serial.println("Limit pin 11: LOW = triggered → motor STOP");

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  
  pinMode(BUTTON_FORWARD,   INPUT_PULLUP);
  pinMode(BUTTON_BACKWARD,  INPUT_PULLUP);
  pinMode(LIMIT_SWITCH,     INPUT_PULLUP);   // internal pull-up + switch to GND

  digitalWrite(EN_PIN, LOW);      // enable driver (LOW = on)
  digitalWrite(DIR_PIN, LOW);     // default forward
  digitalWrite(STEP_PIN, LOW);
}

void loop() {
  // Read buttons and limit switch
  bool fwdPressed   = (digitalRead(BUTTON_FORWARD)   == LOW);
  bool bwdPressed   = (digitalRead(BUTTON_BACKWARD)  == LOW);
  bool limitTriggered = (digitalRead(LIMIT_SWITCH)   == LOW);  // LOW = pressed/clicked

  // SAFETY: if limit switch is triggered → STOP everything
  if (limitTriggered) {
    Serial.println("!!! LIMIT SWITCH TRIGGERED - MOTOR STOPPED !!!");
    // Optional: disable driver to save power/heat
    // digitalWrite(EN_PIN, HIGH);
    return;   // no steps sent → motor stops
  }

  // Normal operation: only move if at least one button pressed AND limit NOT hit
  if (!fwdPressed && !bwdPressed) {
    return;   // both released → stop
  }

  // Direction: forward has priority if both pressed
  if (fwdPressed) {
    digitalWrite(DIR_PIN, LOW);   // LOW = forward (swap to HIGH if direction wrong)
    // Serial.println("→ Forward");
  } 
  else if (bwdPressed) {
    digitalWrite(DIR_PIN, HIGH);  // HIGH = backward
    // Serial.println("← Backward");
  }

  // Generate one step pulse
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(STEP_DELAY_US);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(STEP_DELAY_US);
}
// TMC2208 single-axis control with TWO physical buttons
// Button 1 (pin 9)  → Forward while held
// Button 2 (pin 10) → Backward while held
// Release both      → Stop
//
// Pin assignments as requested:
// STEP →  9
// DIR  → 10
// EN   →  8

#define STEP_PIN     9
#define DIR_PIN     10
#define EN_PIN       8

#define BUTTON_FORWARD   6    // ← changed to free pin
#define BUTTON_BACKWARD  7    // ← changed to free pin

#define STEP_DELAY_US  600   // smaller = faster (try 400–1000)
// increase if motor skips / vibrates, decrease if too slow

void setup() {
  Serial.begin(115200);  // optional – for debugging messages
  Serial.println("Button control ready: Btn1 (pin9)=Forward, Btn2 (pin10)=Backward, release=Stop");

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  
  // WARNING: buttons are on the same pins as STEP & DIR → this will NOT work properly!
  // You MUST change the button pins to free ones (see recommendations below)
  pinMode(BUTTON_FORWARD,   INPUT_PULLUP);
  pinMode(BUTTON_BACKWARD,  INPUT_PULLUP);

  digitalWrite(EN_PIN, LOW);      // enable driver (LOW = on for most TMC2208)
  digitalWrite(DIR_PIN, LOW);     // default direction = forward
  digitalWrite(STEP_PIN, LOW);
}

void loop() {
  bool fwdPressed = (digitalRead(BUTTON_FORWARD) == LOW);
  bool bwdPressed = (digitalRead(BUTTON_BACKWARD) == LOW);

  // Stop if both buttons are released
  if (!fwdPressed && !bwdPressed) {
    return;   // no steps → motor stops
  }

  // Direction: forward has priority if both pressed
  if (fwdPressed) {
    digitalWrite(DIR_PIN, LOW);   // LOW = forward (swap to HIGH if direction is wrong)
    // Serial.println("→ Forward");   // uncomment for debug
  } 
  else if (bwdPressed) {
    digitalWrite(DIR_PIN, HIGH);  // HIGH = backward
    // Serial.println("← Backward");  // uncomment for debug
  }

  // Send one step pulse
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(STEP_DELAY_US);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(STEP_DELAY_US);
}
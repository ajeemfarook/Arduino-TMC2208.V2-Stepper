// TMC2208 single-axis control with serial commands
// f = forward, b = backward, s = stop
// Works with Arduino Uno + TMC2208 in standalone mode

#define STEP_PIN  9
#define DIR_PIN   10
#define EN_PIN    8       // comment out EN control if tied to GND permanently

#define STEP_DELAY_US  600   // smaller = faster (400–800 typical starting range)
// adjust according to your motor + load + current setting

volatile bool moving = false;
volatile bool forwardDir = true;   // true = forward, false = backward

void setup() {
  Serial.begin(115200);
  Serial.println("Ready. Commands: f = forward, b = backward, s = stop");

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  
  digitalWrite(EN_PIN, LOW);      // enable driver (LOW = on)
  digitalWrite(DIR_PIN, LOW);     // initial direction: forward
  digitalWrite(STEP_PIN, LOW);
}

void loop() {
  // Check for serial command (non-blocking)
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    switch (cmd) {
      case 'f':
      case 'F':
        digitalWrite(DIR_PIN, LOW);     // forward = LOW (change if your wiring is inverted)
        moving = true;
        forwardDir = true;
        Serial.println("→ Moving FORWARD");
        break;
        
      case 'b':
      case 'B':
        digitalWrite(DIR_PIN, HIGH);    // backward = HIGH
        moving = true;
        forwardDir = false;
        Serial.println("← Moving BACKWARD");
        break;
        
      case 's':
      case 'S':
      case ' ':
        moving = false;
        Serial.println("STOPPED");
        break;
        
      default:
        // ignore other characters
        break;
    }
  }

  // Generate steps while moving
  if (moving) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(STEP_DELAY_US);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(STEP_DELAY_US);
  }
}
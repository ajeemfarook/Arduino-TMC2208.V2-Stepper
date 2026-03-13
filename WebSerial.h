// ╔══════════════════════════════════════════════════════════════════╗
// ║  WebSerial.h  v2                                                 ║
// ║  Header-only Web Serial bridge for Slider.ino                   ║
// ║                                                                  ║
// ║  INTEGRATION — exactly 4 edits to Slider.ino, nothing else:     ║
// ║                                                                  ║
// ║   1.  After your other #includes (top of file):                  ║
// ║         #include "WebSerial.h"                                   ║
// ║                                                                  ║
// ║   2.  End of setup(), after homeToZero():                        ║
// ║         WS_INIT(currentCM);                                      ║
// ║                                                                  ║
// ║   3.  FIRST line of loop():                                       ║
// ║         WS_TICK(currentCM);                                      ║
// ║                                                                  ║
// ║   4.  After every position assignment in the motion functions:   ║
// ║         WS_POS(currentCM);                                       ║
// ║       — end of homeToZero()                                      ║
// ║       — end of moveToPreset()                                    ║
// ║       — end of doTapMove()                                       ║
// ║                                                                  ║
// ║  OPTIONAL — wrap all debug Serial.println() calls:              ║
// ║         WS_DBG("your message");                                  ║
// ║       Prefixes with # so the web parser ignores them.            ║
// ║                                                                  ║
// ║  SOFT LIMITS (override before #include):                         ║
// ║    #define WS_SOFT_MIN_CM  25.0f    closest allowed (patient)   ║
// ║    #define WS_SOFT_MAX_CM 130.0f    furthest allowed (home)     ║
// ║                                                                  ║
// ║  JOG INCREMENT (override before #include):                       ║
// ║    #define WS_JOG_CM  0.1f                                       ║
// ║                                                                  ║
// ║  PROTOCOL (115200 baud, newline-terminated):                     ║
// ║    IN  ← HOME | GOTO <cm> | JOG_FWD | JOG_BWD                  ║
// ║          JOG_STOP | STATUS | ESTOP | ESTOP_CLEAR                ║
// ║    OUT → POS:<cm>     confirmed position (sent after every move) ║
// ║          WARN:<text>  rejected command / boundary violation      ║
// ║          ESTOP        estop triggered confirmation               ║
// ║          READY        system armed / estop cleared               ║
// ║          #<text>      debug lines — ignored by web parser        ║
// ╚══════════════════════════════════════════════════════════════════╝

#pragma once
#include <Arduino.h>

// ── Tunables ──────────────────────────────────────────────────────────
#ifndef WS_SOFT_MIN_CM
  #define WS_SOFT_MIN_CM  25.0f
#endif
#ifndef WS_SOFT_MAX_CM
  #define WS_SOFT_MAX_CM 130.0f
#endif
#ifndef WS_JOG_CM
  #define WS_JOG_CM 0.1f
#endif

// ── Required symbols from Slider.ino / calib.h / config.h ─────────────
// The compiler will error here — not at runtime — if any are missing.
// These are the only functions WebSerial.h calls from Slider.ino.
extern float    currentCM;      // main position variable (must be non-static)

// NOTE: SPEED_JOG_US, STEPS_PER_CM, MAX_RAIL_CM, HOME_POSITION_CM are
//       macros defined in calib.h, not variables. They are used directly
//       in the implementation below, so no extern declarations are needed.

void homeToZero();
void moveToPreset(float targetCM, const char* label);
void disableMotor();
void enableMotor();
void setDir(bool forward);
void stepOnce(uint16_t halfUs);
void clampPos();
void updateDisplay(const char* msg, bool force);
bool limitMinHit();
bool limitHomeHit();

// ── Internal implementation (all private to this translation unit) ────
namespace _ws {

  static bool _eStop = false;

  // ── Primitives ────────────────────────────────────────────────────────

  static inline void _sendPos(float cm) {
    Serial.print("POS:"); Serial.println(cm, 1);
  }

  static inline void _warn(const char* msg) {
    Serial.print("WARN:"); Serial.println(msg);
  }

  static bool _inBounds(float cm) {
    if (cm < WS_SOFT_MIN_CM) {
      Serial.print(F("WARN:Below minimum "));
      Serial.print((int)WS_SOFT_MIN_CM);
      Serial.print(F("cm — got ")); Serial.print(cm,1); Serial.println(F("cm"));
      return false;
    }
    if (cm > WS_SOFT_MAX_CM) {
      Serial.print(F("WARN:Above maximum "));
      Serial.print((int)WS_SOFT_MAX_CM);
      Serial.print(F("cm — got ")); Serial.print(cm,1); Serial.println(F("cm"));
      return false;
    }
    return true;
  }

  // ── Jog one fixed increment using Slider.ino motion primitives ───────
  // forward=true → toward 0cm (patient)  forward=false → toward 130cm
  static void _jogStep(bool forward) {
    if (forward  && limitMinHit())  { _warn("Hardware min limit hit"); return; }
    if (!forward && limitHomeHit()) { _warn("Hardware max limit hit"); return; }

    enableMotor();
    setDir(forward);

    const int steps = max(1, (int)(WS_JOG_CM * STEPS_PER_CM + 0.5f));
    for (int i = 0; i < steps; i++) {
      if (forward ? limitMinHit() : limitHomeHit()) break;
      stepOnce(SPEED_JOG_US);
    }
    currentCM += forward ? -WS_JOG_CM : WS_JOG_CM;
    clampPos();
    _sendPos(currentCM);
  }

  // ── Trigger estop ─────────────────────────────────────────────────────
  static void _triggerEstop() {
    disableMotor();
    _eStop = true;
    Serial.println(F("ESTOP"));
    updateDisplay("!! E-STOP !!", true);
    _sendPos(currentCM);
  }

  // ── Command dispatcher ────────────────────────────────────────────────
  static void dispatch(const String& cmd) {

    // ── ESTOP — highest priority, no gate ────────────────────────────────
    if (cmd == F("ESTOP")) {
      _triggerEstop();
      return;
    }

    // ── ESTOP_CLEAR — re-arm, no auto-home ───────────────────────────────
    if (cmd == F("ESTOP_CLEAR")) {
      _eStop = false;
      Serial.println(F("READY"));
      updateDisplay("E-Stop cleared", true);
      _sendPos(currentCM);
      return;
    }

    // ── STATUS — always answered ──────────────────────────────────────────
    if (cmd == F("STATUS")) {
      _sendPos(currentCM);
      if (_eStop) Serial.println(F("ESTOP"));
      return;
    }

    // ── Gate all motion while estop is active ─────────────────────────────
    if (_eStop) {
      _warn("E-Stop active — send ESTOP_CLEAR to resume");
      return;
    }

    // ── HOME ──────────────────────────────────────────────────────────────
    if (cmd == F("HOME")) {
      homeToZero();
      // homeToZero sets currentCM = HOME_POSITION_CM internally;
      // WS_POS in homeToZero() reports the final position.
      return;
    }

    // ── JOG_FWD ───────────────────────────────────────────────────────────
    if (cmd == F("JOG_FWD")) {
      if (currentCM <= WS_SOFT_MIN_CM + 0.01f) {
        Serial.print(F("WARN:At minimum "));
        Serial.print((int)WS_SOFT_MIN_CM);
        Serial.println(F("cm — cannot go closer"));
        return;
      }
      _jogStep(true);
      return;
    }

    // ── JOG_BWD ───────────────────────────────────────────────────────────
    if (cmd == F("JOG_BWD")) {
      if (currentCM >= WS_SOFT_MAX_CM - 0.01f) {
        Serial.print(F("WARN:At maximum "));
        Serial.print((int)WS_SOFT_MAX_CM);
        Serial.println(F("cm — cannot go farther"));
        return;
      }
      _jogStep(false);
      return;
    }

    // ── JOG_STOP — click-jog model; just confirm position ─────────────────
    if (cmd == F("JOG_STOP")) {
      _sendPos(currentCM);
      return;
    }

    // ── GOTO <cm> ─────────────────────────────────────────────────────────
    if (cmd.startsWith(F("GOTO "))) {
      const float target = cmd.substring(5).toFloat();
      if (!_inBounds(target)) return;
      char label[12];
      dtostrf(target, 4, 1, label);
      strcat(label, "cm");
      moveToPreset(target, label);
      // WS_POS at end of moveToPreset() reports final position.
      return;
    }

    // Unknown command — drop silently (don't echo noise to web)
  }

} // namespace _ws


// ════════════════════════════════════════════════════════════════════
//  PUBLIC API — 4 macros + 1 flag.  Nothing else touches Slider.ino.
// ════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────
// WS_INIT(currentCM)
//   End of setup(), after homeToZero().
//   Sends READY + initial POS: to the web controller.
// ─────────────────────────────────────────────────────────────────────
#define WS_INIT(cm)  do {           \
  Serial.println(F("READY"));       \
  _ws::_sendPos(cm);                \
} while(0)

// ─────────────────────────────────────────────────────────────────────
// WS_TICK(currentCM)
//   First line of loop().
//   Reads one newline-terminated command per loop iteration.
//   One-command-per-tick prevents serial buffer flooding.
// ─────────────────────────────────────────────────────────────────────
#define WS_TICK(cm)  do {                                         \
  if (Serial.available()) {                                       \
    String _ws_line = Serial.readStringUntil('\n');               \
    _ws_line.trim();                                              \
    if (_ws_line.length() > 0) _ws::dispatch(_ws_line);          \
  }                                                               \
} while(0)

// ─────────────────────────────────────────────────────────────────────
// WS_POS(currentCM)
//   After every position assignment.
//   Sends POS:<cm> to the web controller.
//   Required in: homeToZero, moveToPreset, doTapMove
// ─────────────────────────────────────────────────────────────────────
#define WS_POS(cm)   _ws::_sendPos(cm)

// ─────────────────────────────────────────────────────────────────────
// WS_ESTOP_ACTIVE
//   Boolean flag. True when estop is active.
//   Use to guard LCD preset buttons and non-web motion:
//     if (!WS_ESTOP_ACTIVE) { moveToPreset(...); }
// ─────────────────────────────────────────────────────────────────────
#define WS_ESTOP_ACTIVE  (_ws::_eStop)

// ─────────────────────────────────────────────────────────────────────
// WS_DBG(literal_string)
//   Replaces debug Serial.println() calls.
//   Prefixes '#' so the web parser ignores the line.
//   Uses F() to keep the string in flash, not RAM.
//   Example: WS_DBG("HOME: A4 triggered")
// ─────────────────────────────────────────────────────────────────────
#define WS_DBG(msg)  do { Serial.print('#'); Serial.println(F(msg)); } while(0)
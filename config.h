// ╔══════════════════════════════════════════════════════════════════╗
// ║              config.h  —  PIN MAP                               ║
// ╚══════════════════════════════════════════════════════════════════╝

#pragma once

// ── STEP / DIR / EN ─────────────────────────────────────────────────
#define STEP_PIN      12
#define DIR_PIN       13
#define EN_PIN        11    // TMC2208 standalone: active-LOW enable

// ── DIR LOGIC ───────────────────────────────────────────────────────
//  On this board:
//    DIR_PIN HIGH → forward  (toward motor / A5 / position decreases)
//    DIR_PIN LOW  → backward (toward home  / A4 / position increases)
//
//  This is the OPPOSITE of the default assumption.
//  setDir(forward=true)  → DIR HIGH
//  setDir(forward=false) → DIR LOW
//  homeToZero phase 1    → DIR LOW (driving toward A4 / 130cm)
// ────────────────────────────────────────────────────────────────────

// ── JOG BUTTONS ─────────────────────────────────────────────────────
#define BTN_FORWARD   A1    // toward A5 / motor end  (position decreases)
#define BTN_BACKWARD  A2    // toward A4 / home end   (position increases)

// ── LIMIT SWITCHES (active-LOW, internal pull-up) ────────────────────
#define LIMIT_MIN     A5    // 25 cm — forward/motor end stop
#define LIMIT_HOME    A4    // 130 cm — backward/home end stop

// ── LCD KEYPAD SHIELD ────────────────────────────────────────────────
#define LCD_RS   8
#define LCD_EN   9
#define LCD_D4   4
#define LCD_D5   5
#define LCD_D6   6
#define LCD_D7   7
#define LCD_BTN  A0

// LCD button ADC thresholds
#define BTN_RIGHT_MAX    50
#define BTN_UP_MAX      195
#define BTN_DOWN_MAX    380
#define BTN_LEFT_MAX    555
#define BTN_SELECT_MAX  790
// 0=RIGHT  1=UP  2=DOWN  3=LEFT  4=SELECT  -1=none

// ── DISPLAY ──────────────────────────────────────────────────────────
#define DISPLAY_PERIOD_MS  200

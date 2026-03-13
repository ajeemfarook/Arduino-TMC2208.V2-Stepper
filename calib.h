// ╔══════════════════════════════════════════════════════════════════╗
// ║              calib.h  —  CALIBRATION FILE                       ║
// ║  Creality 42-34  +  TMC2208 v2 standalone                        ║
// ║  MS1=0  MS2=0  →  8x microstep  |  21-tooth GT2 belt            ║
// ╚══════════════════════════════════════════════════════════════════╝

#pragma once

// ── MECHANICAL ──────────────────────────────────────────────────────
//
//  Creality 42-34:  1.8° (200 steps/rev)  |  0.8 A rated  |  0.4 N·m
//
//  TMC2208 standalone MS1/MS2 → microstep:
//    MS1=0 MS2=0 →  8x   ← YOUR SETTING
//    MS1=0 MS2=1 →  4x
//    MS1=1 MS2=0 →  2x
//    MS1=1 MS2=1 → 16x
//
//  Steps/cm = (200 × 8) / (21 × 2mm / 10) = 1600 / 4.2 = 380.952
// ────────────────────────────────────────────────────────────────────
#define MOTOR_STEPS_PER_REV   200
#define BELT_TEETH            21
#define BELT_PITCH_MM         2.0f
#define MICROSTEPS            8          // MS1=0 MS2=0

#define STEPS_PER_CM \
  ((float)(MOTOR_STEPS_PER_REV * MICROSTEPS) / \
   ((float)(BELT_TEETH) * BELT_PITCH_MM / 10.0f))
// = 380.952 steps/cm

// ── RAIL GEOMETRY ───────────────────────────────────────────────────
//
//  Physical layout:
//
//    [MOTOR / A5 end]──────────────────────────[A4 end / HOME]
//              25 cm                                140 cm
//              (A5 switch)                          (A4 switch)
//
//  A5 (LIMIT_MIN)  = forward  end ← closest to motor
//  A4 (LIMIT_HOME) = backward end ← homing target
//
// ── HOW THE DYNAMIC CALIBRATION WORKS ──────────────────────────────
//
//  On every boot, calibrateRail() drives to both switches and counts
//  the steps between them. From that step count and STEPS_PER_CM
//  (derived purely from your belt/pulley/microstep values above) it
//  calculates the real rail span in cm and sets railMinCM / railMaxCM
//  as runtime variables.
//
//  You never need to measure the rail with a tape — the belt math does
//  it. NOMINAL_MIN_CM / NOMINAL_MAX_CM are only used as:
//    1. Sanity-check bounds (warn if measured span is implausible)
//    2. Fallback if a switch never triggers (safety timeout)
//
//  Formula used inside calibrateRail():
//    span_cm   = steps_A5_to_A4 / STEPS_PER_CM
//    railMinCM = NOMINAL_MIN_CM                  (A5 switch = fixed ref)
//    railMaxCM = NOMINAL_MIN_CM + span_cm        (A4 derived from span)
//
// ── NOMINAL VALUES (reference / sanity bounds only) ─────────────────
#define NOMINAL_MIN_CM      25.0f   // expected A5 switch position
#define NOMINAL_MAX_CM     140.0f   // expected A4 switch position
#define NOMINAL_SPAN_CM    (NOMINAL_MAX_CM - NOMINAL_MIN_CM)  // 115 cm

// ── RUNTIME LIMITS (set by calibrateRail(), used everywhere else) ────
//
//  These replace MIN_RAIL_CM / MAX_RAIL_CM / HOME_POSITION_CM.
//  Declared extern here — defined once in Slider.ino.
//  After calibrateRail() completes these hold the belt-measured values.
extern float railMinCM;    // measured A5 position  (≈ NOMINAL_MIN_CM)
extern float railMaxCM;    // measured A4 position  (≈ NOMINAL_MAX_CM)
extern float railHomeCM;   // = railMaxCM (carriage parks here)

// Convenience aliases so the rest of the code reads naturally
#define MIN_RAIL_CM    railMinCM
#define MAX_RAIL_CM    railMaxCM
#define HOME_POSITION_CM railHomeCM

// ── A5 SWITCH OFFSET ────────────────────────────────────────────────
//  Fine-trim if the carriage position reads slightly wrong at the A5
//  end after calibration. Jog to the switch body, read the LCD,
//  set deficit = LCD_reading − NOMINAL_MIN_CM.
//  Positive → reads too high  |  Negative → reads too low
#define MIN_SWITCH_OFFSET_CM   0.0f   // ← adjust after first run if needed

#define BACKOFF_CM          0.5f    // retract from switch after trigger
#define TAP_DISTANCE_CM     0.5f    // nudge after jog release
#define HOME_NEAR_CM        1.0f    // within this → full re-home on RIGHT

// ── CALIBRATION SANITY TOLERANCE ────────────────────────────────────
//  calibrateRail() warns on Serial if the measured span differs from
//  NOMINAL_SPAN_CM by more than this. Non-fatal — measured value
//  is still used even if the warning fires.
#define CAL_TOLERANCE_CM    1.0f    // acceptable ± error in cm

// ── CALIBRATION SAFETY TIMEOUT ──────────────────────────────────────
//  Max steps to drive before giving up if a switch never triggers.
//  Set to NOMINAL_SPAN_CM × 1.3 as a safe ceiling.
#define CAL_MAX_STEPS  (long)((NOMINAL_SPAN_CM * 1.3f) * STEPS_PER_CM)

// ── SPEED (half-period µs — HIGHER = SLOWER) ────────────────────────
#define SPEED_CAL_US     1500    // slow sweep during startup calibration
#define SPEED_HOME_US     800    // homing (RIGHT button)
#define SPEED_JOG_US      300    // hold-jog buttons
#define SPEED_TAP_US      600    // tap nudge after jog release
#define SPEED_TRAVEL_US   250    // preset moves

// ── SOFT-START RAMP ─────────────────────────────────────────────────
#define RAMP_START_US    2000    // slow end of ramp (half-period)
#define RAMP_STEPS        600    // steps to reach full speed

// ── IDLE AUTO-DISABLE ───────────────────────────────────────────────
#define IDLE_TIMEOUT_MS   5000   // ms idle before motor de-energises

// ── PRESET POSITIONS (cm) ───────────────────────────────────────────
#define PRESET_A   45.0f   // UP     button
#define PRESET_B   58.0f   // DOWN   button
#define PRESET_C   81.0f   // LEFT   button
#define PRESET_D  125.0f   // SELECT button

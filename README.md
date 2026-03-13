# Slider Controller

Arduino-based motorised camera slider with web serial control, automatic rail calibration, and LCD keypad interface.

---

## Hardware

| Part | Spec |
|---|---|
| MCU | Arduino Uno / Mega |
| Driver | TMC2208 v2 — standalone STEP/DIR/EN mode |
| Motor | Creality 42-34 NEMA17 (0.8 A, 0.4 N·m) |
| Belt | GT2, 21-tooth pulley |
| Microstepping | 8x — MS1=LOW, MS2=LOW |
| Steps/cm | 380.952 (auto-calculated from belt/pulley math) |
| Rail | 25 cm (A5 switch) → 140 cm (A4 switch) |
| LCD | DFRobot LCD Keypad Shield (16×2) |

---

## Wiring

```
Arduino   →   TMC2208
D12       →   STEP
D13       →   DIR
D11       →   EN          (active LOW)

Arduino   →   Limits
A5        →   LIMIT_MIN   (25 cm / motor end)   active LOW, internal pull-up
A4        →   LIMIT_HOME  (140 cm / home end)   active LOW, internal pull-up

Arduino   →   Jog buttons
A1        →   BTN_FORWARD   (toward 25 cm)      active LOW, internal pull-up
A2        →   BTN_BACKWARD  (toward 140 cm)     active LOW, internal pull-up
```

TMC2208 MS1 and MS2 pins must both be tied LOW for 8x microstepping.

---

## File Structure

```
CameraSlider/
├── Slider.ino      — main sketch
├── calib.h         — all tunable parameters (edit this first)
├── config.h        — pin map (edit if you re-wire)
├── WebSerial.h     — header-only web serial bridge
└── README.md
```

**Only `calib.h` needs editing for most changes** — speeds, presets, rail geometry, switch offset.

---

## Coordinate System

```
[MOTOR / A5 end] ────────────────────────── [A4 end / HOME]
      25 cm                                      140 cm
   (LIMIT_MIN)                               (LIMIT_HOME)
   DIR HIGH = forward →                ← backward = DIR LOW
   position decreases                     position increases
```

---

## Boot Sequence

1. Power on → LCD shows **"Waiting serial.."** — motor stays off, nothing moves
2. Open web page or Serial monitor → Serial connects
3. LCD shows **"Calibrating..."** → rail sweep begins automatically
4. Carriage drives to A5 (25 cm), backs off
5. Carriage drives to A4 (140 cm), counts steps, measures real span
6. Runtime limits `railMinCM` / `railMaxCM` are set from belt math — no tape measure needed
7. LCD shows **"Ready"** → web page receives `READY` + `POS:140.0`

Calibration runs **once per power cycle**, triggered by the first Serial connection. The carriage will not move before that.

---

## LCD Keypad Buttons

| Button | Action |
|---|---|
| RIGHT | Go home (smooth move to 140 cm, then switch-lock) |
| UP | Move to preset A (45 cm) |
| DOWN | Move to preset B (58 cm) |
| LEFT | Move to preset C (81 cm) |
| SELECT | Move to preset D (125 cm) |

During any preset move, **RIGHT = abort** → re-homes automatically.

Jog buttons (A1 / A2): hold to jog, release triggers a short tap nudge.

---

## Web Serial Commands

| Command | Description |
|---|---|
| `HOME` | Home to A4 switch |
| `GOTO <cm>` | Move to position in cm (e.g. `GOTO 75.5`) |
| `JOG_FWD` | Jog one increment toward A5 |
| `JOG_BWD` | Jog one increment toward A4 |
| `JOG_STOP` | Confirm position after jog |
| `STATUS` | Request current position |
| `ESTOP` | Emergency stop — disables motor immediately |
| `ESTOP_CLEAR` | Re-arm after estop |

Responses:

| Response | Meaning |
|---|---|
| `POS:<cm>` | Current confirmed position |
| `READY` | System armed / estop cleared |
| `ESTOP` | Estop triggered |
| `WARN:<text>` | Rejected command or boundary violation |
| `#<text>` | Debug line — ignored by web parser |

Baud rate: **115200**. All commands newline-terminated.

---

## Calibration Tuning (`calib.h`)

### Steps/cm — automatically calculated
```cpp
// Change any of these and STEPS_PER_CM updates automatically
#define MOTOR_STEPS_PER_REV   200    // 1.8° motor
#define BELT_TEETH            21     // pulley tooth count
#define BELT_PITCH_MM         2.0f   // GT2 = 2 mm
#define MICROSTEPS            8      // must match MS1/MS2 pin state
```

### A5 switch offset — if position reads slightly wrong at the motor end
```cpp
#define MIN_SWITCH_OFFSET_CM  0.0f   // positive = reads too high, negative = reads too low
```
**How to measure:** after calibration, jog to the A5 switch body, read the LCD. Set `deficit = LCD_reading − 25.0`.

### Speed tuning
```cpp
#define SPEED_CAL_US     1500   // calibration sweep (higher = slower)
#define SPEED_HOME_US     800   // RIGHT button homing
#define SPEED_JOG_US      300   // hold-jog
#define SPEED_TAP_US      600   // tap nudge after jog release
#define SPEED_TRAVEL_US   250   // preset moves
```

### Preset positions
```cpp
#define PRESET_A   45.0f   // UP
#define PRESET_B   58.0f   // DOWN
#define PRESET_C   81.0f   // LEFT
#define PRESET_D  125.0f   // SELECT
```

---

## Libraries Required

No extra libraries needed. Only the Arduino built-in `LiquidCrystal` is used.

---

## Notes

- Motor auto-disables after 5 s idle to prevent heat buildup (42-34 runs warm at 0.8 A)
- ESTOP from web disables motor mid-move and reports last known position
- Serial monitor and web page can be used interchangeably same protocol
- If the A4 switch never triggers during calibration (broken wire, bad switch), the system falls back to nominal limits (25–140 cm) and shows `CAL ERROR` on the LCD

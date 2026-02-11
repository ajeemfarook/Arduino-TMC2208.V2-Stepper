# Thorlabs PDX4/M Linear Stage Stepper Control with LCD

Arduino Leonardo (or Uno compatible) project to control a Thorlabs PDX4/M (or similar) linear stage using TMC2208 stepper driver.

Features:
- Two buttons: forward (+) and backward (−) — hold for continuous move, tap for 0.5 cm step
- Home button — returns to 0 cm using backward limit switch
- Two limit switches (forward & backward)
- LCD1602 16×2 display showing current position (cm), percentage, and status
- Automatic homing at startup
- Position tracking with software limits

## Hardware Requirements

- Arduino Leonardo (or Uno)
- TMC2208 stepper driver
- NEMA17 stepper motor (or compatible with your stage)
- 3 × momentary push buttons (forward, backward, home)
- 2 × limit switches (normally open)
- LCD1602 (parallel 4-bit mode, no I2C backpack)
- 10kΩ potentiometer (recommended for contrast) or fixed resistor/GND trick
- Power supply for motor (12–24 V depending on TMC2208)

## Pin Connections

| Function              | Leonardo Pin | LCD Pin | Description                              |
|-----------------------|--------------|---------|------------------------------------------|
| STEP                  | 9            | —       | TMC2208 STEP                             |
| DIR                   | 10           | —       | TMC2208 DIR                              |
| EN                    | 8            | —       | TMC2208 EN (active LOW)                  |
| Forward button        | 6            | —       | INPUT_PULLUP, pressed = LOW              |
| Backward button       | 7            | —       | INPUT_PULLUP                             |
| Home button           | 5            | —       | INPUT_PULLUP                             |
| Forward limit switch  | 12           | —       | INPUT_PULLUP, hit = LOW                  |
| Backward limit switch | 13           | —       | INPUT_PULLUP, hit = LOW                  |
| LCD RS                | 11           | 4       | Register Select                          |
| LCD Enable (E)        | A0           | 6       | Enable                                   |
| LCD DB4               | A1           | 11      | Data 4                                   |
| LCD DB5               | A2           | 12      | Data 5                                   |
| LCD DB6               | A3           | 13      | Data 6                                   |
| LCD DB7               | A4           | 14      | Data 7                                   |
| LCD RW                | —            | 5       | Must connect to GND                      |
| LCD VSS               | —            | 1       | GND                                      |
| LCD VDD               | —            | 2       | 5V                                       |
| LCD VO (contrast)     | —            | 3       | GND (or 10k pot middle)                  |
| LCD A (backlight +)   | —            | 15      | 5V                                       |
| LCD K (backlight -)   | —            | 16      | GND                                      |

**Note**: If text is invisible or only black squares appear, try connecting LCD pin 3 (VO) to 5V instead of GND.

## Calibration

Current settings:
- 200 steps = 0.5 cm → 400 steps/cm
- Each tap moves 0.5 cm
- Adjust `STEPS_PER_CM` and `STEPS_PER_TAP` if your stage moves differently

## How to use

1. Power on → automatic homing to 0 cm
2. Hold forward/backward button → continuous move
3. Tap forward/backward → move 0.5 cm
4. Press home button → return to 0 cm
5. LCD shows:  
   Line 1: `Pos: 12.5 cm`  
   Line 2: `11%  >> Forward` (or Idle / Homing...)

## License

MIT License – feel free to modify and share.

Made for Thorlabs PDX4/M stage automation.

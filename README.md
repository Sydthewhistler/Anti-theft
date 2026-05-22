# Anti-Theft IoT Device — UCA21

> Motion-based anti-theft system using the UCA21 board (ATmega328PB), a KXTJ3-1057 accelerometer, and a 4×4 keypad for alarm deactivation.

---

## Features

- **Motion detection** via KXTJ3-1057 accelerometer (I2C)
- **Smart detection algorithm** — sustained movement (~8s) triggers the alarm, isolated shocks are ignored
- **Penalty system** — repeated short movements progressively increase sensitivity to prevent bypassing the system
- **Audible alarm** — external buzzer (TMB12A05) activated on theft detection
- **Visual alarm** — WS2812 RGB LEDs (×21)
- **Arm / Disarm** — via integrated button BT0 (D2)
- **Alarm deactivation** — via 4×4 keypad with secret code entry
- **LoRa alert** *(optional)* — sends notification via LoRaWAN to The Things Network on alarm trigger

---

## Hardware

| Component | Description |
|---|---|
| UCA21 board | ATmega328PB, 3.3V, 8MHz |
| KXTJ3-1057 | 3-axis accelerometer, I2C (0x0E) |
| WS2812 / WS2811 | 21× RGB LEDs, D4 |
| TMB12A05 | Active buzzer 3.3V, D8 |
| 4×4 keypad | Matrix keypad for code entry |
| BT0 button | Integrated button, D2 |

---

## Pin Mapping

| Pin | Role |
|---|---|
| D2 | Button BT0 — arm / disarm (INPUT_PULLUP) |
| D4 | WS2812 LEDs data + I2C power supply |
| D8 | External buzzer output |
| A0, A1, A2, A3 | Keypad rows R1→R4 |
| D5, D7, D9, D3 | Keypad columns C1→C4 |

---

## Dependencies

Install the following libraries via the Arduino Library Manager:

| Library | Author |
|---|---|
| FastLED | Daniel Garcia |
| Keypad | Mark Stanley, Alexander Brevig |
| KXTJ3-1057 | Leonardo Bispo (ldab) |

Board package:
- **RFTHings AVR Boards** by RFThings Vietnam
- URL: `https://rfthings.github.io/ArduinoBoardManagerJSON/package_rfthings-avr_index.json`
- Select board: **UCA** → Board version: **3.9 and newer : AT328PB**

---

## Detection Algorithm

The detection uses a dynamic counter system to avoid both false positives and false negatives:

```
Movement sample  →  counter += INCREMENT (starts at 3)
Calm sample      →  counter -= 1
Alarm trigger    →  counter >= 150  (~8s of continuous movement at 6.25 Hz)
```

**Penalty system** — if movement resumes after a pause (≥1s), `INCREMENT` increases by 3 (up to 15 max). This prevents someone from bypassing the alarm by moving the bag in small repeated jolts.

**Calm reset** — after ~3s of sustained calm, `INCREMENT` resets to its base value.

### Key Parameters

| Parameter | Value | Description |
|---|---|---|
| `SEUIL_DIFF` | 1.5 | Movement threshold (in g) |
| `INCREMENT_BASE` | 3 | Base counter increment per movement sample |
| `INCREMENT_MAX` | 15 | Maximum increment (penalty cap) |
| `INCR_PENALITE` | 3 | Penalty added on each movement restart |
| `SEUIL_ALARME` | 150 | Counter value to trigger alarm (~8s at 6.25 Hz) |
| `CALME_RESET` | 18 | Calm samples before INCREMENT resets (~3s) |
| `sampleRate` | 6.25 Hz | Accelerometer sampling rate |

---

## System States

```
OFF ──[BT0]──► ARMED ──[theft detected]──► ALARM
 ▲                │                           │
 └────[BT0]───────┘              [correct code on keypad]
 └───────────────────────────────────────────┘
```

| State | LED | Buzzer | Detection |
|---|---|---|---|
| OFF | — | OFF | No |
| ARMED | — | OFF | Yes |
| ALARM | Red | ON | — |

---

## Keypad Usage

The keypad is **only active when the alarm is triggered**.

| Key | Action |
|---|---|
| `0`–`9`, `A`–`D` | Add digit to input |
| `#` | Validate code |
| `*` | Clear current input |

The default secret code is `1234`. Change it in the source:

```cpp
const String CODE_SECRET = "1234";
```

---

## Button (BT0 — D2)

| Situation | Press result |
|---|---|
| System OFF | Arms the system |
| System ARMED | Disarms the system |
| Alarm active | Prompts to use keypad (button ignored) |

---

## Debug Mode

To enable serial debug output (115200 baud), uncomment at the top of the sketch:

```cpp
#define DEBUG
```

Debug output shows per-sample values:
```
diff=0.12  cpt=0/150  incr=3  calme=5  buzz=OFF  -> calme
diff=2.45  cpt=3/150  incr=3  calme=0  buzz=OFF  -> mouvement...
```

---

## Notes

- The WS2812 LEDs on pin D4 also power the I2C bus on the UCA21 board. FastLED must be initialized before the IMU.
- The buzzer operates at 3.3V — sound level is lower than rated 5V spec but sufficient for a prototype.
- If adding LoRa, move the buzzer from D8 to D9 (D8 is used by the RFM95W reset pin).
- The board runs at 8MHz/3.3V — use board version **AT328PB** in the Arduino IDE.

---

## Author

Project developed by **Sydney Cavallin** — L1 Informatique, Université Côte d'Azur / École 42 Nice  
Based on UCA21 example code by **Fabien Ferrero**, Professor @UCA
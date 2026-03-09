# ESP32 Hardware Schematic — Columbia CT-6 Boiler Dosing Controller

## System Overview

### Standalone (single ESP32)

All sensors and blowdown are on the main ESP32. This is the default configuration.

```
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                        24 VDC POWER SUPPLY                                 │
 │                        (DIN-rail mount)                                    │
 └──────┬──────────────┬──────────────────┬───────────────┬───────────────────┘
        │              │                  │               │
        │         ┌────┴────┐     ┌───────┴───────┐  ┌───┴───────────────┐
        │         │ 5V Buck │     │  Blowdown     │  │ S4 Actuator       │
        │         │Converter│     │  Relay/Resistor│  │ Power (24VDC)     │
        │         └────┬────┘     │  Circuit       │  └───────────────────┘
        │              │          └───────┬────────┘
        │         ┌────┴────┐            │
        │         │  3.3V   │            │
        │         │  LDO    │            │
        │         └────┬────┘            │
        │              │                 │
 ┌──────┴──────────────┴─────────────────┴────────────────────────────────────┐
 │                        ESP32 DevKit V1 (38-pin)                            │
 │   STEPPER          SENSORS           COMMS          ACTUATOR       UI      │
 │   MOTORS           & ADC             & LED          CONTROL               │
 │   ───────          ──────            ─────          ────────       ──      │
 │   3x A4988         EZO-EC (UART)     I2C Bus        SPDT Relay    LCD     │
 │   + Nema17         MAX31865 (VSPI)   WS2812 LED     + MOSFET      Encoder │
 │                    SD Card (VSPI)    WiFi AP+STA                          │
 │                    ADS1115 (I2C)     Web UI :80                           │
 │                    Water Meter                                            │
 │                    FW Pump Monitor                                        │
 └────────────────────────────────────────────────────────────────────────────┘
```

### With coprocessor (ESP32 DevKit at boiler panel)

Main ESP32 talks to the boiler-panel coprocessor over RS-485. The coprocessor (ESP32 DevKit) hosts conductivity/temperature sensing and blowdown (and optional solenoid); the main runs fuzzy logic, pump control, logging, and UI using coprocessor telemetry and by sending commands.

```
 ┌─────────────────────────────────────────────────────────────────────────────────┐
 │                          24 VDC POWER SUPPLY                                   │
 └──────┬────────────────┬──────────────────┬────────────────┬───────────────────┘
        │                 │                  │                │
        │            ┌────┴────┐      ┌──────┴──────┐   ┌─────┴────────────┐
        │            │ 5V Buck │      │  Main ESP32 │   │ ESP32 DevKit      │
        │            │Converter│      │  RS-485     │   │ (boiler panel)    │
        │            └────┬────┘      │  DE/RE*     │   │ EZO-EC, MAX31865  │
        │                 │           └──────┬───────┘   │ Blowdown relay   │
        │            ┌────┴────┐            │          │ 4-20mA + CT: ADC  │
        │            │  3.3V   │            │ RS-485   │ Solenoid relay    │
        │            │  LDO    │            │ A/B/GND  │ (auto-direction)  │
        │            └────┬────┘            │          └────────┬─────────┘
        │                 │                 │                   │
 ┌──────┴─────────────────┴─────────────────┴───────────────────┴─────────────────┐
 │  ESP32 DevKit V1 (38-pin)                    │  ESP32 DevKit (boiler panel)     │
 │  STEPPER │ COMMS/LED │ UI   │ RS-485 (Ser2)  │  Telemetry → Main                │
 │  3x A4988│ I2C+WS2812│ LCD  │ GPIO25/36+DE_RE│  Commands ← Main (open/close)    │
 │  Pumps   │ WiFi :80  │ Enc  │               │  Relays: blowdown, solenoid      │
 └─────────────────────────────────────────────────────────────────────────────────┘
 * Main board DE/RE; panel uses auto-direction RS-485 (no DE pin).
```

### System variants

| Variant | Description |
|--------|-------------|
| **Standalone** | All sensors (EZO-EC, MAX31865, ADS1115) and blowdown relay on main ESP32. Single enclosure. |
| **Coprocessor (ESP32 DevKit)** | Main ESP32 connects to a second ESP32 DevKit at the boiler panel via half-duplex RS-485 (panel uses auto-direction transceiver, no DE pin). Panel hosts conductivity/temperature sensing and blowdown (and optional solenoid). Main uses telemetry for fuzzy logic and sends blowdown/solenoid commands. See [Coprocessor_Communication_Logic.md](Coprocessor_Communication_Logic.md). |

---

## ESP32 Full Pin Map

```
                          ┌──────────────┐
                          │  ESP32 DevKit│
                          │    38-Pin    │
                          │              │
              3V3 ────────┤ 3V3      VIN ├──────── 5V
              GND ────────┤ GND      GND ├──────── GND
                          │              │
  Enc Btn (sel) ← GPIO0  ┤ D0       D23 ├ GPIO23 → VSPI MOSI (MAX31865 + SD)
   Encoder B/DT ← GPIO2  ┤ D2       D22 ├ GPIO22 → I2C SCL (LCD + ADS1115)
  Blowdown Relay → GPIO4 ┤ D4       D1  ├ TX0      (USB Serial - reserved)
   WS2812 Data  → GPIO5  ┤ D5       D3  ├ RX0      (USB Serial - reserved)
                          │              │
                          │  FLASH PINS  │
                          │  GPIO 6–11   │
                          │  DO NOT USE  │
                          │              │
  Stepper1 STEP → GPIO12 ┤ D12*     D21 ├ GPIO21 → I2C SDA (LCD + ADS1115)
  Stepper EN    → GPIO13 ┤ D13      D19 ├ GPIO19 → SD Card CS
  Stepper1 DIR  → GPIO14 ┤ D14      D18 ├ GPIO18 → VSPI SCK (MAX31865 + SD)
  Encoder A/CLK ← GPIO15 ┤ D15      D5  │         (see above)
  MAX31865 CS   → GPIO16 ┤ D16      D17 ├ GPIO17 ← AUX Input (Drum Level)

  * GPIO12 = strapping pin. External 10k pull-down to GND required.
                          │              │
  EZO-EC TX     → GPIO25 ┤ D25      D16 │         (see above)
  Stepper2 DIR  → GPIO26 ┤ D26      D4  │         (see above)
  Stepper2 STEP → GPIO27 ┤ D27      D0  │         (see above)
  Stepper3 DIR  → GPIO32 ┤ D32      D2  │         (see above)
  Stepper3 STEP → GPIO33 ┤ D33      D15 │         (see above)
                          │              │
  Water Meter   ← GPIO34 ┤ D34      D13 │         (see above)
  FW Pump Mon   ← GPIO35 ┤ D35      D12 │         (see above)
  EZO-EC RX     ← GPIO36 ┤ VP       D14 │         (see above)
  MAX31865 MISO ← GPIO39 ┤ VN       D27 │         (see above)
                          │              │
                          └──────────────┘

    ← = Input     → = Output     ↔ = Bidirectional (I2C)
```

---

## Complete GPIO Assignment Table

| GPIO | Function | Dir | Peripheral | Bus | Notes |
|------|----------|-----|------------|-----|-------|
| **0** | Encoder Button (select/menu) | IN | KY-040 SW | — | Strapping pin! External 10k pull-up required. Active LOW. Short press=select, long press=menu. |
| **2** | Encoder Output B (DT) | IN | KY-040 DT | — | Strapping pin. Must be LOW for serial flash. |
| **4** | Blowdown Relay Coil | OUT | 2N7000 MOSFET gate | — | Drives SPDT relay via N-ch MOSFET. LOW=closed, HIGH=open. |
| **5** | WS2812 LED Data | OUT | WS2812B strip (8 LEDs) | — | 330 ohm series resistor on data line. |
| **12** | Stepper 1 STEP (H2SO3) | OUT | A4988 #1 STEP | — | **Strapping pin! External 10k pull-down to GND required.** |
| **13** | Stepper Common ENABLE | OUT | A4988 #1/#2/#3 EN | — | Active LOW. Shared by all 3 drivers. |
| **14** | Stepper 1 DIR (H2SO3) | OUT | A4988 #1 DIR | — | |
| **15** | Encoder Output A (CLK) | IN | KY-040 CLK | — | Strapping pin. Pull-up keeps HIGH at boot (OK). |
| **16** | MAX31865 Chip Select | OUT | Adafruit MAX31865 | VSPI | Active LOW. |
| **17** | AUX Input 1 (Drum Level) | IN | Dry contact switch | — | Internal pull-up enabled. |
| **18** | VSPI SCK | OUT | MAX31865 + SD card | VSPI | Shared SPI clock. |
| **19** | SD Card CS | OUT | Micro-SD module | VSPI | Chip select for SD card. |
| **21** | I2C SDA | I/O | LCD (0x27) + ADS1115 (0x48) | I2C | 4.7k pull-up to 3.3V. 400 kHz Fast Mode. |
| **22** | I2C SCL | OUT | LCD (0x27) + ADS1115 (0x48) | I2C | 4.7k pull-up to 3.3V. |
| **23** | VSPI MOSI | OUT | MAX31865 + SD card | VSPI | Shared SPI data out. |
| **25** | EZO-EC UART TX / RS-485 TX | OUT | Atlas EZO-EC RX (standalone) or RS-485 DI (coprocessor) | UART2 | Standalone: 9600 baud to EZO. Coprocessor: 115200 baud to RS-485 transceiver. |
| **26** | Stepper 2 DIR (NaOH) | OUT | A4988 #2 DIR | — | |
| **27** | Stepper 2 STEP (NaOH) | OUT | A4988 #2 STEP | — | |
| **32** | Stepper 3 DIR (Amine) | OUT | A4988 #3 DIR | — | |
| **33** | Stepper 3 STEP (Amine) | OUT | A4988 #3 STEP | — | |
| **34** | Water Meter Pulse | IN | Contact closure (1 pulse/gal) | — | Input-only. External 10k pull-up to 3.3V required. Interrupt-driven. |
| **35** | Feedwater Pump Monitor | IN | PC817 optocoupler output | — | Input-only. External 10k pull-up to 3.3V. Active LOW (pump ON). |
| **36** | EZO-EC UART RX | IN | Atlas EZO-EC TX | UART2 | Input-only. ESP32 RX ← EZO TX. **Coprocessor variant:** RS-485 RX (from transceiver RO). |
| **39** | MAX31865 MISO / SD MISO | IN | Adafruit MAX31865 + SD card | VSPI | Input-only. Shared VSPI MISO. |

**Coprocessor variant:** When the coprocessor link is used, Serial2 is repurposed for RS-485: GPIO25 = RS-485 TX (to transceiver DI), GPIO36 = RS-485 RX (from transceiver RO). A DE/RE pin is required on the **main** board for half-duplex: drive HIGH when transmitting, LOW when receiving. Recommended DE/RE GPIO: **GPIO17** if AUX input (drum level) is not used; otherwise set per board in `pin_definitions.h`. The **Atlas EZO-EC** is on the **boiler panel** coprocessor (ESP32 DevKit), connected via Serial1 (GPIO9 RX, GPIO10 TX, 9600 baud); MAX31865 may also be on the panel if pins allow, or temperature is sent as fixed/default.

> **Note:** GPIO19 was previously unassigned. It now serves as the SD card chip select
> on the shared VSPI bus (MOSI=23, MISO=39, SCK=18). GPIO23 and GPIO18 are shared
> between the MAX31865 and SD card; a FreeRTOS mutex protects concurrent access.

### Unused / Reserved GPIOs

| GPIO | Reason |
|------|--------|
| 1 (TX0) | USB Serial TX — do not use |
| 3 (RX0) | USB Serial RX — do not use |
| 6–11 | Internal SPI flash — do not use |

When the coprocessor link is used, GPIO25 and GPIO36 are dedicated to RS-485 on the main board. EZO-EC (and optionally MAX31865) are on the C3 board; the main receives conductivity and temperature only via C3 telemetry.

---

## Subsystem Schematics

### 1. Stepper Motor Drivers (3x A4988 + Nema17)

Three peristaltic/dosing pumps driven by Nema17 steppers through A4988 driver boards (CNC Shield V4.0 architecture).

```
                        ┌──────────────────────────────────────────────┐
                        │              A4988 Driver Board #1           │
    ESP32               │         (Hydrogen Sulfite — H2SO3)          │
   ┌──────┐             │                                              │
   │GPIO12├─── STEP ───→┤ STEP                              COIL A+ ──├──→ Nema17
   │       │    │        │           A4988               COIL A- ──├──→ Motor
   │       │  ┌─┴──┐     │                                COIL B+ ──├──→ Winding
   │       │  │10k │     │                                COIL B- ──├──→ Leads
   │       │  │pull│     │                                            │
   │       │  │down│     │ VMOT ←── 12V                              │
   │       │  └─┬──┘     │ GND  ←── GND                              │
   │       │    GND      │ VDD  ←── 3.3V (logic)                     │
   │GPIO14├─── DIR  ───→┤ DIR                                        │
   │GPIO13├─── EN   ───→┤ EN (active LOW)                            │
   └──────┘             │                                              │
                        │ MS1 MS2 MS3: Set for 1/16 microstepping     │
                        │ (MS1=H, MS2=H, MS3=H)                       │
                        └──────────────────────────────────────────────┘
    NOTE: GPIO12 is a strapping pin. The 10k pull-down resistor to GND
    is REQUIRED to ensure LOW at boot (selects 3.3V flash voltage).

                        ┌──────────────────────────────────────────────┐
                        │              A4988 Driver Board #2           │
                        │         (Sodium Hydroxide — NaOH)            │
   ┌──────┐             │                                              │
   │GPIO27├─── STEP ───→┤ STEP                              COIL A+ ──├──→ Nema17
   │GPIO26├─── DIR  ───→┤ DIR           A4988               COIL A- ──├──→ Motor
   │GPIO13├─── EN   ───→┤ EN (shared)                       COIL B+ ──├──→ Winding
   └──────┘             │                                   COIL B- ──├──→ Leads
                        │ VMOT ←── 12V      GND ←── GND               │
                        └──────────────────────────────────────────────┘

                        ┌──────────────────────────────────────────────┐
                        │              A4988 Driver Board #3           │
                        │              (Amine)                         │
   ┌──────┐             │                                              │
   │GPIO33├─── STEP ───→┤ STEP                              COIL A+ ──├──→ Nema17
   │GPIO32├─── DIR  ───→┤ DIR           A4988               COIL A- ──├──→ Motor
   │GPIO13├─── EN   ───→┤ EN (shared)                       COIL B+ ──├──→ Winding
   └──────┘             │                                   COIL B- ──├──→ Leads
                        │ VMOT ←── 12V      GND ←── GND               │
                        └──────────────────────────────────────────────┘

    Stepper Configuration:
    ┌───────────────────────────────────────────┐
    │ Steps/Rev:       200 (1.8 deg)            │
    │ Microstepping:   1/16 (3200 steps/rev)    │
    │ Max Speed:       1000 steps/sec           │
    │ Acceleration:    500 steps/sec^2          │
    │ Motor Current:   Set via A4988 Vref pot   │
    │ Enable:          Active LOW (shared GPIO13)│
    └───────────────────────────────────────────┘
```

**Current limit adjustment:** Set each A4988 Vref potentiometer to match the Nema17 motor rated current. Formula: `Vref = I_motor × 8 × R_sense`. Typical R_sense on A4988 boards = 0.068 ohm.

---

### 2. Conductivity Sensor (Atlas EZO-EC + Sensorex CS675HTTC)

```
    ┌──────────────┐         UART (9600 baud)        ┌──────────────────┐
    │    ESP32      │                                  │  Atlas Scientific │
    │               │                                  │  EZO-EC Circuit   │
    │  GPIO25 (TX) ─┼──────────────────────────────────┤→ RX              │
    │  GPIO36 (RX) ─┼──────────────────────────────────┤← TX              │
    │               │                                  │                  │
    │               │                                  │  PRB ────────────┼───→ Sensorex
    │               │                                  │  PRB ────────────┼───→ CS675HTTC
    │               │                                  │                  │    Conductivity
    │               │                                  │  VCC ←── 3.3V   │    Probe
    │               │                                  │  GND ←── GND    │    (K=1.0)
    │               │                                  │                  │
    └──────────────┘                                  └──────────────────┘

    EZO-EC UART Protocol:
    ┌──────────────────────────────────────────────────────────────┐
    │ Baud Rate:   9600                                           │
    │ Format:      8N1, CR terminated                             │
    │ Commands:    R (read), RT,xx.x (read w/temp), Cal,xxx      │
    │ Outputs:     EC, TDS, Salinity, Specific Gravity            │
    │ Cell Const:  K=1.0 (range 0-10,000 uS/cm)                  │
    └──────────────────────────────────────────────────────────────┘
```

---

### 3. PT1000 RTD Temperature Sensor (Adafruit MAX31865)

```
    ┌──────────────┐      Software SPI        ┌─────────────────────┐
    │    ESP32      │                           │  Adafruit MAX31865  │
    │               │                           │  Breakout Board     │
    │  GPIO16 (CS) ─┼───────────────────────────┤→ CS                │
    │  GPIO23 (MOSI)┼───────────────────────────┤→ SDI               │
    │  GPIO39 (MISO)┼───────────────────────────┤← SDO               │    ┌───────────┐
    │  GPIO18 (SCK) ┼───────────────────────────┤→ CLK               │    │  PT1000   │
    │               │                           │                     │    │  RTD      │
    │               │                           │  RTD+ ──────────────┼────┤  Sensor   │
    │               │                           │  RTD- ──────────────┼────┤  (2-wire) │
    │               │                           │                     │    └───────────┘
    │               │                           │  VIN  ←── 3.3V     │
    │               │                           │  GND  ←── GND      │
    └──────────────┘                           └─────────────────────┘

    MAX31865 Configuration:
    ┌──────────────────────────────────────────────────────┐
    │ RTD Type:            PT1000 (1000 Ω at 0 deg C)     │
    │ Reference Resistor:  4300 Ω (Rref for PT1000)       │
    │ Wiring:              2-wire (solder jumpers on board)│
    │ SPI Mode:            Software SPI (bit-bang)         │
    │ Accuracy:            +/- 0.5 deg C typical          │
    └──────────────────────────────────────────────────────┘

    NOTE: For PT1000 2-wire config, solder the 2/4-wire jumper on
    the Adafruit board. Connect RTD leads to F+ and F- terminals
    and bridge F-/RTD- with a jumper wire.
```

---

### 4. Blowdown Valve (Assured Automation E26NRXS4UV-EP420C)

#### 4a. Control Signal — Relay-Switched 4-20mA Generator

```
                      24 VDC
                        │
           ┌────────────┼────────────────┐
           │            │                │
      ┌────┴────┐  ┌────┴────┐           │
      │ R_close │  │ R_open  │           │
      │ 3.3 kΩ  │  │ 680 Ω   │           │
      │ 1% 0.5W │  │ 1% 0.5W │           │
      └────┬────┘  └────┬────┘           │
           │            │                │
          NC           NO                │
           │            │                │
           └─────┬──────┘                │
                 │                       │
              COMMON                     │
              (K1 SPDT Relay)            │
                 │                       │
                 ▼                       │
        Actuator DPS                     │
        Input (+)                        │
        (Terminal 2)                     │
                                         │
        Actuator DPS                     │
        Input (-)  ──────────────────────┘
        (Terminal 1)                  (24V GND)


    Relay Coil Drive Circuit:
    ─────────────────────────

                     5V (or 3.3V per relay spec)
                      │
                 ┌────┴────┐
                 │  Relay  │
                 │  Coil   │ K1 (SPDT, 5V coil)
                 │  (K1)   │
                 └────┬────┘
              D1      │
           1N4148 ──┤├── (flyback diode across coil)
                     │
                  ┌──┴──┐
                  │Drain│
                  │     │ Q1 (2N7000 N-ch MOSFET)
                  │Gate │
                  └──┬──┘
                     │
    ESP32 GPIO4 ─────┤
                     │
                  R_gate (1 kΩ) pull-down
                     │
                    GND


    Relay Truth Table:
    ┌──────────────┬───────────────┬──────────────────┬──────────┬────────┐
    │ GPIO4 State  │ Relay State   │ Resistor         │ Current  │ Valve  │
    ├──────────────┼───────────────┼──────────────────┼──────────┼────────┤
    │ LOW (default)│ De-energized  │ R_close (3.3 kΩ) │ ~4.2 mA  │ CLOSED │
    │ HIGH         │ Energized     │ R_open (680 Ω)   │ ~20.6 mA │ OPEN   │
    └──────────────┴───────────────┴──────────────────┴──────────┴────────┘
```

#### 4b. Position Feedback — 4-20mA via ADS1115 External ADC

```
    Actuator DPS                          ┌──────────────────┐
    Feedback Output (+)                   │   ADS1115 16-bit │
    (Terminal E2)                         │   External ADC   │
         │                                │                  │
         │                                │  A0 (CH0) ───────┼──── Voltage sense point
    ┌────┴─────┐                          │                  │
    │ R_sense  │                          │  VDD ←── 3.3V   │
    │ 150 Ω    │                          │  GND ←── GND    │
    │ 1% 0.25W │                          │  SCL ←── GPIO22 │ ← I2C (shared bus)
    └────┬─────┘                          │  SDA ↔── GPIO21 │ ← I2C (shared bus)
         │                                │  ADDR ── GND    │ → I2C addr 0x48
         ├──── C_filt (0.1 uF) ── GND    │                  │
         │                                └──────────────────┘
         │
         ●──── → ADS1115 Channel 0 (A0)
         │
    Actuator DPS
    Feedback Output (-)
         │
        GND (24V return, shared with ESP32 GND)


    Feedback Current-to-Voltage Conversion:
    ┌─────────────────────────────────────────────────────┐
    │  4 mA  × 150 Ω = 0.60 V  →  Valve CLOSED          │
    │  12 mA × 150 Ω = 1.80 V  →  Valve in transit       │
    │  20 mA × 150 Ω = 3.00 V  →  Valve OPEN             │
    │  < 3 mA (< 0.45V)        →  Wiring fault            │
    └─────────────────────────────────────────────────────┘

    ADS1115 provides 16-bit resolution on shared I2C bus.
    No additional GPIO required beyond the existing I2C lines.
```

---

### 5. I2C Bus (LCD + ADS1115)

```
                           3.3V
                            │  │
                         ┌──┴──┴──┐
                         │4.7k 4.7k│  Pull-up resistors
                         └──┬──┬──┘
                            │  │
    ESP32 GPIO21 (SDA) ─────┼──┼──── SDA bus
    ESP32 GPIO22 (SCL) ─────┼──┼──── SCL bus
                            │  │
               ┌────────────┤  ├────────────┐
               │            │  │            │
        ┌──────┴──────┐    │  │     ┌──────┴──────┐
        │   20x4 LCD  │    │  │     │   ADS1115   │
        │  + PCF8574  │    │  │     │  16-bit ADC │
        │  I2C Backpack│   │  │     │             │
        │             │    │  │     │ Addr: 0x48  │
        │ Addr: 0x27  │   SDA SCL  │ CH0: Valve  │
        │             │             │   Feedback  │
        │ VCC ← 5V   │             │ VCC ← 3.3V  │
        │ GND ← GND  │             │ GND ← GND   │
        └─────────────┘             └─────────────┘

    I2C Configuration:
    ┌───────────────────────────────────────────────┐
    │ Speed:    400 kHz (Fast Mode)                 │
    │ Pull-ups: 4.7 kΩ to 3.3V on SDA and SCL      │
    │ Devices:  LCD @ 0x27, ADS1115 @ 0x48          │
    │                                               │
    │ LCD: HD44780 controller, 20 cols x 4 rows     │
    │      Powered from 5V (PCF8574 is 5V tolerant, │
    │      ESP32 3.3V logic accepted as HIGH)       │
    └───────────────────────────────────────────────┘
```

---

### 6. WS2812 RGB LED Status Strip

```
    ESP32 GPIO5 ──→ [330 Ω series resistor] ──→ DIN ┌────────────────────────┐
                                                    │  WS2812B LED Strip     │
                                                    │  8 Addressable LEDs    │
                     5V ────────────────────────────┤← VCC                   │
                    GND ────────────────────────────┤← GND                   │
                                                    └────────────────────────┘

    100-470 uF electrolytic cap across VCC-GND near first LED.
    330 Ω resistor in series on data line (near GPIO5 output).

    NOTE: The WS2812 datasheet requires VIH >= 0.7 × VDD (3.5V at 5V),
    but ESP32 outputs 3.3V. In practice this works reliably — most
    WS2812 chips accept 3.3V, and the first LED re-outputs at 5V
    levels for the rest of the chain. A 74HCT125 level shifter can
    be added if you experience flickering, but is usually not needed.

    LED Index Assignments:
    ┌─────┬──────────────────┬─────────────────────────────────────┐
    │ LED │ Function         │ Color Coding                        │
    ├─────┼──────────────────┼─────────────────────────────────────┤
    │  0  │ Power Status     │ Green = OK, Red = Error             │
    │  1  │ WiFi Status      │ Blue = Connected, Off = Disconnected│
    │  2  │ Conductivity     │ Color indicates level               │
    │  3  │ Blowdown         │ Yellow when active                  │
    │  4  │ Pump 1 (H2SO3)  │ Cyan when running                   │
    │  5  │ Pump 2 (NaOH)   │ Magenta when running                │
    │  6  │ Pump 3 (Amine)  │ Yellow when running                 │
    │  7  │ Alarm            │ Red when alarm active               │
    └─────┴──────────────────┴─────────────────────────────────────┘
    Brightness: 128/255 (50%)
```

---

### 7. Water Meter Input

```
                                      3.3V
                                       │
                                    ┌──┴──┐
                                    │ 10k │ External pull-up
                                    └──┬──┘    (GPIO34 has no internal pull-up)
                                       │
    ESP32 GPIO34 (input only) ─────────┤
                                       │
                                  ┌────┴────┐
                                  │ Water   │
                                  │ Meter   │  Contact closure
                                  │ Contact │  (dry contact, 1 pulse per gallon)
                                  └────┬────┘
                                       │
                                      GND

    ┌──────────────────────────────────────────────────────┐
    │ Signal Type:    Dry contact closure (reed switch)    │
    │ Rate:           1 pulse per gallon                   │
    │ Debounce:       50 ms (software)                     │
    │ Interrupt:      Falling edge on GPIO34               │
    │ Pull-up:        External 10k to 3.3V (required)      │
    └──────────────────────────────────────────────────────┘
```

---

### 8. Feedwater Pump Monitor (110VAC via Optocoupler)

Monitors the CT-6 boiler feedwater pump contactor. The pump-on indicator
light (110VAC) drives a PC817 optocoupler through series resistors. The
optocoupler's phototransistor pulls GPIO35 LOW when the pump is running.

```
    110VAC Hot (pump-on light) ─────┐
                                    │
                                  R1 (33 kΩ, ½W)
                                    │
                                  R2 (33 kΩ, ½W)          3.3V
                                    │                       │
                              ┌─────┤                    ┌──┴──┐
                         D1   │     │                    │ 10k │  External pull-up
                       1N4007 │   ┌─┴──┐                └──┬──┘  (GPIO35 has no
                     (reverse │   │LED │ PC817              │      internal pull-up)
                    protection│   └─┬──┘                    │
                              └─────┤        Collector ─────┤
                                    │                       │
    110VAC Neutral ─────────────────┘        Emitter ──── GND
                                                            │
                                    ESP32 GPIO35 ───────────┘

    ┌────────────────────────────────────────────────────────────────┐
    │ Pump ON:  Optocoupler conducts → GPIO35 = LOW                 │
    │ Pump OFF: Optocoupler open    → GPIO35 = HIGH (10k pull-up)   │
    │ Tracks:   Cycle count, per-cycle duration, cumulative on-time │
    │ Logs:     FW_PUMP_ON / FW_PUMP_OFF events to TimescaleDB      │
    └────────────────────────────────────────────────────────────────┘
```

---

### 9. Rotary Encoder (KY-040) — Sole Input Device

The rotary encoder is the **only physical input device**. Rotation
navigates menus / adjusts values. The push button serves as both
select (short press) and menu enter/exit (long press).

```
                    3.3V
                     │ │ │
                  10k│ │ │10k
                     │ │ │           ┌──────────────┐
    ESP32 GPIO15 ────┘ │ └──── A ───┤  KY-040      │
    ESP32 GPIO2  ──────┘       B ───┤  Rotary      │
                                    │  Encoder     │
                     3.3V           │              │
                      │             │  + ←── 3.3V  │
                   ┌──┴──┐         │  GND ← GND   │
                   │ 10k │         │              │
                   └──┬──┘         └──────┬───────┘
                      │                   │
    ESP32 GPIO0  ─────┘              SW (push button)
                                    Active LOW

    ┌──────────────────────────────────────────────────────────┐
    │ Rotate CW:       Next screen / increment value           │
    │ Rotate CCW:      Prev screen / decrement value           │
    │ Short press:     Select / confirm                        │
    │ Long press:      Enter / exit menu (1500 ms threshold)   │
    │                                                          │
    │ Steps Per Notch:     4 pulses per detent                 │
    │ Rotation Debounce:   5 ms                                │
    │ Button Debounce:     50 ms                               │
    │                                                          │
    │ GPIO0 NOTE: This is a boot strapping pin. An external    │
    │ 10k pull-up to 3.3V is REQUIRED. The encoder button      │
    │ pulls LOW when pressed, which is safe during normal       │
    │ operation. Ensure button is NOT pressed during boot/flash.│
    └──────────────────────────────────────────────────────────┘
```

---

### 10. Auxiliary Input — Drum Level Switch

```
                                     3.3V
                                      │
                                   ┌──┴──┐
                                   │ 10k │  (or use internal pull-up)
                                   └──┬──┘
                                      │
    ESP32 GPIO17 ─────────────────────┤
                                      │
                                 ┌────┴────┐
                                 │  Drum   │
                                 │  Level  │  Float switch or
                                 │ Switch  │  level probe
                                 └────┬────┘
                                      │
                                     GND
```

---

### 11. SD Card Module (Shared VSPI Bus)

The micro-SD card module shares the hardware VSPI bus with the MAX31865.
A FreeRTOS mutex (`spiMutex`) ensures only one device uses the bus at a time.

```
                         Shared VSPI Bus
                    ┌─────────────────────────────┐
                    │  MOSI = GPIO23               │
                    │  MISO = GPIO39 (input-only)  │
                    │  SCK  = GPIO18               │
                    └──┬──────────────────────┬────┘
                       │                      │
              ┌────────┴────────┐    ┌────────┴────────┐
              │  MAX31865 RTD   │    │  Micro-SD Card  │
              │  CS = GPIO16    │    │  CS = GPIO19    │
              └─────────────────┘    └─────────────────┘

    Micro-SD Module Wiring:
    ─────────────────────────────────────────────────
    ESP32 GPIO19  ───→  CS   (chip select)
    ESP32 GPIO23  ───→  MOSI (data in)
    ESP32 GPIO39  ←───  MISO (data out)     (via module level shifter)
    ESP32 GPIO18  ───→  SCK  (clock)
    3.3V          ───→  VCC
    GND           ───→  GND

    ┌──────────────────────────────────────────────────────┐
    │ SPI Clock:     4 MHz (SD_SPI_FREQ)                   │
    │ Card Format:   FAT32, up to 32 GB                    │
    │ Bus Sharing:   Mutex-protected with MAX31865          │
    │ Log Format:    CSV files in /logs/YYYY-MM-DD.csv      │
    │ Flush:         Every 30 seconds                       │
    │ NOTE: Most micro-SD modules include onboard 3.3V     │
    │       regulator and level shifters. Verify the module │
    │       accepts 3.3V logic on MISO/MOSI/SCK/CS.        │
    └──────────────────────────────────────────────────────┘
```

---

### 12. RS-485 Transceiver (main board, coprocessor variant)

When the ESP32 DevKit coprocessor is used, the main ESP32 connects via half-duplex RS-485. Serial2 (GPIO25 = TX, GPIO36 = RX) drives a transceiver on the main board; DE and /RE are tied together and driven by one GPIO (e.g. GPIO17). The panel coprocessor uses an auto-direction RS-485 module (no DE pin).

```
    ESP32                    MAX485 (or equivalent)              Twisted pair
   ┌──────┐                  ┌─────────────────────┐             A/B + GND
   │GPIO25├── TX ───────────→┤ DI                  │
   │      │                  │         RO ─────────┼────────────→ GPIO36 (RX)
   │      │                  │         DE ──┬──────┤
   │      │                  │        /RE ───┘      │
   │DE_RE ├──────────────────→┤ (DE and /RE tied)   │
   │(e.g. │                  │         A ───────────┼────────────→ RS-485 A
   │GPIO17)                  │         B ───────────┼────────────→ RS-485 B
   │      │                  │       GND ───────────┼────────────→ GND
   └──────┘                  └─────────────────────┘

    DE/RE: HIGH = drive bus (TX); LOW = receive (RX).
    After each TX, wait 1–2 character times (~2 ms at 115200) before reading.
```

| Parameter | Value |
|-----------|-------|
| Baud rate | 115200 |
| Format | 8N1 |
| Turn-around | 1–2 character times after TX before RX (see [Coprocessor_Communication_Logic.md](Coprocessor_Communication_Logic.md)) |

---

### 13. ESP32 DevKit boiler panel (coprocessor)

When the coprocessor topology is used, an **ESP32 DevKit** (esp32dev) sits at the boiler panel. It runs RS-485 to the main ESP32 (auto-direction transceiver — **no DE pin**), drives blowdown and optional solenoid relays, reads 4–20 mA valve feedback and two CT channels via its **internal ADC** (GPIO36 valve, GPIO39/34 CTs; 150 Ω sense for valve), and hosts the **Atlas Scientific EZO-EC** on **Serial1** (GPIO9 RX, GPIO10 TX). The main ESP32 receives conductivity (and temperature) only via coprocessor telemetry over RS-485. See `include/c3_pin_definitions.h` and `test_programs/README.md`.

**Block diagram:**

```
    5V or 3.3V ──────────────────────────→ ESP32 DevKit VIN/3V3
    GND (shared with main) ──────────────→ ESP32 DevKit GND

    RS-485 A/B/GND (from main transceiver) → Auto-direction RS-485 module (no DE); Serial2 RX=GPIO16, TX=GPIO17
    GPIO4  → Blowdown relay module
    GPIO15 → Solenoid relay module (optional)
    GPIO36 → 4–20 mA valve feedback (internal ADC, 150 Ω sense)
    GPIO39 → Boiler power CT (internal ADC, DC bias + software RMS)
    GPIO34 → Low water CT (internal ADC, DC bias + software RMS)
    GPIO10 (Serial1 TX) → EZO-EC RX; GPIO9 (Serial1 RX) ← EZO-EC TX; 9600 baud
```

**Pin map (ESP32 DevKit):**

| GPIO | Function |
|------|----------|
| 4 | Blowdown relay |
| 5 | MAX31865 CS (VSPI) |
| 9 | EZO-EC RX (Serial1) |
| 10 | EZO-EC TX (Serial1) |
| 15 | Solenoid relay |
| 16 | RS-485 RX (Serial2) |
| 17 | RS-485 TX (Serial2) |
| 18 | MAX31865 SCK (VSPI) |
| 19 | MAX31865 MISO (VSPI) |
| 23 | MAX31865 MOSI (VSPI) |
| 34 | ADC1 — low water CT (DC bias) |
| 36 | ADC1 — 4–20 mA valve feedback |
| 39 | ADC1 — boiler power CT (DC bias) |
| — | RS-485 DE/RE = not used (auto-direction) |

**Internal ADC (no ADS1115):** Valve 4–20 mA is read on **GPIO36** (ADC1_CH0) via 150 Ω sense resistor (0.6–3.0 V). **GPIO39** and **GPIO34** are used for the two SCT-013-005 CT channels with DC bias + software RMS (see Section 13a). Valve feedback is used in telemetry; CTs can be used for boiler-on and low-water detection in firmware.

**Atlas EZO-EC:** The conductivity board is at the boiler panel. Connect EZO-EC RX to **GPIO10** (Serial1 TX), EZO-EC TX to **GPIO9** (Serial1 RX); 9600 baud, CR-terminated commands. Firmware uses HardwareSerial (Serial1). Temperature in telemetry may be fixed (e.g. 25°C) unless an RTD or DS18B20 is added.

**Power:** Supply the panel from 5V or 3.3V; connect GND to main and RS-485 GND.

### 13a. SCT-013-005 current transformers with internal ADC — schematic requirements

The coprocessor uses **two current transformer (CT) clamps** read by the **internal ADC**: **boiler power-on** (GPIO39) and **low water** (GPIO34). The design assumes the **SCT-013-005** (or equivalent 5 A / 1 V voltage-output CT). Datasheet: [Seeed Studio SCT013](https://files.seeedstudio.com/wiki/AC_Current_Sensor/Datasheet_of_SCT013.pdf).

**SCT-013-005 (5 A / 1 V) key specs:**

| Parameter | Value |
|-----------|--------|
| Rated input | 5 A RMS |
| Rated output | **1 V** (AC voltage; **built-in burden resistor**) |
| Sensitivity | 200 mV/A |
| Max current | 15 A |
| Frequency | 50 Hz – 1 kHz |
| Connector | 3.5 mm stereo jack |

Because this model has a **built-in burden**, the CT outputs **0–1 V AC** directly (no external burden resistor). Two interface options:

---

**Option A — DC bias + software RMS (recommended; line frequency stable)**

The ADC sees the **AC waveform** shifted into the positive range by a DC bias; firmware samples over an integer number of line cycles and computes RMS. No rectifier needed. See [OpenEnergyMonitor: CT Sensors - Interfacing with an Arduino](https://docs.openenergymonitor.org/electricity-monitoring/ct-sensors/interface-with-arduino.html).

1. **DC bias circuit (per CT channel)**  
   - Resistor divider from **3.3 V** to **GND**: two equal resistors (e.g. **10 kΩ** each) → **midpoint = 1.65 V**.  
   - **Coupling capacitor** (e.g. **10 µF** non-polarized or back-to-back electrolytics): one end to the **SCT-013-005 output** (one wire), other end to the **midpoint**.  
   - **ADC input (GPIO39 or GPIO34)** and the other end of the divider connect to this **midpoint**.  
   - The other CT wire goes to **GND**.  
   - The ADC therefore sees **1.65 V + AC**, so the waveform stays between ~0.25 V and ~3.05 V (for ±1.4 V peak).

2. **Schematic (per channel):**

```
    3.3 V  ---- R1 (10 kΩ) ----+---- to ADC GPIO39 (or GPIO34)
                                  |
                                 C1 (10 µF)  ---- SCT-013-005 (one wire)
                                  |
    GND    ---- R2 (10 kΩ) ----+  |
                                |
                                +---- SCT-013-005 other wire to GND
```

3. **Firmware:** Coprocessor samples the ADC pin over a 40 ms window (~2 cycles at 50 Hz or 2.4 at 60 Hz), subtracts the DC mean (bias), computes RMS of the AC component, then scales to current (200 mV/A). Set `C3_CT_LINE_HZ` to 50 or 60 in `c3_pin_definitions.h`.

4. **Parts per CT (Option A):** Two 10 kΩ resistors (R1, R2), one 10 µF coupling cap (C1). Optional: series R and clamp diodes for overvoltage protection.

---

**Option B — AC-to-DC conditioning (rectifier + filter)**

1. **No external burden** — SCT-013-005 already provides 0–1 V AC. Do not leave the CT open when current flows.  
2. **AC-to-DC:** Full-wave rectifier + RC low-pass, or an RMS-to-DC IC (e.g. AD736). Add DC offset so 0 A is in range (e.g. 0–3.3 V).  
3. **Protection:** Series R and clamp diodes so that open CT or transients cannot damage the ESP32 ADC.  
4. **Connection:** CT → conditioning → ADC GPIO39 or GPIO34; conditioning GND to ESP32 GND.

**Block diagram (Option B):**

```
    SCT-013-005 (0–1 V AC)  →  [AC-to-DC + offset]  →  [clamp]  →  ADC GPIO39 or GPIO34
```

---

**Summary:** For **Option A** (DC bias + software RMS), use the R1/R2/C1 circuit above per channel; no rectifier. Firmware uses `readCTChannelRMS()` on the internal ADC pins and reports V RMS and A RMS. For **Option B**, use rectifier/filter and single-sample reads; scale in firmware (e.g. 1 V DC ≈ 5 A).

---

## Power Supply Architecture

```
    ┌───────────────────┐
    │   AC Mains Input  │
    │   120/240 VAC     │
    └────────┬──────────┘
             │
    ┌────────┴──────────┐
    │   24 VDC Supply   │  DIN-rail mount, 60W+ recommended
    │   (Primary Rail)  │
    └──┬──────┬─────┬───┘
       │      │     │
       │      │     └──────────────────────────────────────── 24V → S4 Actuator Power
       │      │                                                     (L/+  and  N/-)
       │      │
       │      └── 24V → Blowdown relay/resistor control circuit
       │
    ┌──┴──────────┐
    │ DC-DC Buck  │  24V → 12V (for stepper motors)
    │ 24V → 12V   │  2A+ rated
    └──┬──────────┘
       │
       └── 12V → A4988 VMOT (all 3 drivers)

    ┌──────────────┐
    │ DC-DC Buck   │  24V → 5V (or dedicated 5V supply)
    │ 24V → 5V     │  2A+ rated
    └──┬───────────┘
       │
       ├── 5V → ESP32 VIN
       ├── 5V → LCD VCC (via PCF8574 backpack)
       ├── 5V → WS2812B LED strip VCC
       ├── 5V → Relay coil (if 5V relay)
       │
       └── (ESP32 onboard LDO produces 3.3V from 5V input)
           │
           ├── 3.3V → A4988 VDD (logic supply)
           ├── 3.3V → EZO-EC VCC
           ├── 3.3V → MAX31865 VIN
           ├── 3.3V → ADS1115 VDD
           ├── 3.3V → KY-040 encoder VCC
           └── 3.3V → Pull-up resistors (I2C, inputs)

    ┌─────────────────────────────────────────────────────────┐
    │ IMPORTANT: All GNDs must be connected together:         │
    │   24V GND = 12V GND = 5V GND = 3.3V GND = ESP32 GND   │
    │   = Actuator signal GND                                 │
    │                                                         │
    │ Use star grounding or a common ground bus bar.           │
    │ Keep high-current (motor, actuator) grounds separate     │
    │ from signal grounds where possible, joining at a         │
    │ single point.                                            │
    └─────────────────────────────────────────────────────────┘
```

---

## Complete Bill of Materials

### Microcontroller

| Qty | Component | Part Number / Description | Notes |
|-----|-----------|---------------------------|-------|
| 1 | ESP32 DevKit V1 | ESP-WROOM-32, 38-pin | Main controller, WiFi/BLE |

### Coprocessor variant (main board)

| Qty | Component | Part Number / Description | Notes |
|-----|-----------|---------------------------|-------|
| 1 | RS-485 transceiver | MAX485 or equivalent | Half-duplex; DI/RO/DE/RE, A/B; main board only |
| 1 | DE/RE GPIO | — | Use e.g. GPIO17 or per-board choice in `pin_definitions.h` |

### Coprocessor variant (ESP32 DevKit boiler panel)

| Qty | Component | Part Number / Description | Notes |
|-----|-----------|---------------------------|-------|
| 1 | ESP32 DevKit V1 | ESP-WROOM-32, 38-pin | Boiler panel coprocessor |
| 1 | RS-485 module | Auto-direction (no DE pin) | Half-duplex; connect Serial2 RX=GPIO16, TX=GPIO17 |
| 1–2 | Relay module(s) | 3.3V/5V logic, 1 or 2 channels | Blowdown (GPIO4), optional solenoid (GPIO15) |
| 1 | R_sense | 150 Ω, 1%, 0.25W metal film | 4–20 mA to voltage → internal ADC (GPIO36) |
| 2 | CT clamp SCT-013-005 | 5 A / 1 V, built-in burden | Boiler power-on (GPIO39), low water (GPIO34); [datasheet](https://files.seeedstudio.com/wiki/AC_Current_Sensor/Datasheet_of_SCT013.pdf). Section 13a: DC bias + software RMS or AC-to-DC conditioning. |

Valve feedback and two CT inputs use the **internal ADC** (GPIO36 valve, GPIO39/34 CTs). No ADS1115. Each CT uses DC bias + software RMS (Option A in Section 13a) or AC-to-DC conditioning.

### Stepper Motors & Drivers

| Qty | Component | Part Number / Description | Notes |
|-----|-----------|---------------------------|-------|
| 3 | Nema17 Stepper Motor | 1.8 deg, 200 steps/rev | For peristaltic/dosing pumps |
| 3 | A4988 Stepper Driver | Pololu A4988 or equivalent | 1/16 microstepping, current adjustable |
| 1 | CNC Shield V4.0 (optional) | Adapted for ESP32 | Or wire A4988 boards individually |

### Sensors

| Qty | Component | Part Number / Description | Notes |
|-----|-----------|---------------------------|-------|
| 1 | Sensorex CS675HTTC/P1K | Conductivity probe, K=1.0 | Toroidal, high-temp, boiler rated |
| 1 | Atlas Scientific EZO-EC | Conductivity circuit (UART mode) | 9600 baud, CR terminated |
| 1 | PT1000 RTD Sensor | 2-wire platinum RTD | Temperature compensation |
| 1 | Adafruit MAX31865 Breakout | PT1000 RTD-to-Digital | With 4300 ohm Rref for PT1000 |
| 1 | ADS1115 Breakout | 16-bit ADC, I2C (0x48) | Blowdown valve feedback reading |

### Blowdown Valve Assembly

| Qty | Component | Part Number / Description | Notes |
|-----|-----------|---------------------------|-------|
| 1 | Ball Valve + Actuator | Assured Automation E26NRXS4UV-EP420C | 1" SS, 4-20mA, fail-closed |
| 1 | SPDT Relay | 5V coil, 100mA+ contacts | Selects 4mA/20mA resistor |
| 1 | R_open | 680 ohm, 1%, 0.5W metal film | Sets 20 mA (valve open) |
| 1 | R_close | 3.3 kohm, 1%, 0.5W metal film | Sets 4 mA (valve closed) |
| 1 | Q1 — N-ch MOSFET | 2N7000 | Relay coil driver |
| 1 | D1 — Flyback Diode | 1N4148 or 1N4007 | Relay coil EMF protection |
| 1 | R_gate | 1 kohm | MOSFET gate pull-down |
| 1 | R_sense | 150 ohm, 1%, 0.25W metal film | 4-20mA feedback to voltage |
| 1 | C_filt | 0.1 uF ceramic | ADC input noise filter |

### SD Card (Local Data Logger)

| Qty | Component | Part Number / Description | Notes |
|-----|-----------|---------------------------|-------|
| 1 | Micro-SD Card Module | SPI breakout (e.g., Adafruit 254) | 3.3V logic, VSPI bus |
| 1 | Micro-SD Card | FAT32 formatted, 4–32 GB | Class 10 or better recommended |

### User Interface

| Qty | Component | Part Number / Description | Notes |
|-----|-----------|---------------------------|-------|
| 1 | 20x4 LCD Display | HD44780 + PCF8574 I2C backpack | I2C address 0x27 (or 0x3F) |
| 1 | KY-040 Rotary Encoder | With push button | Primary navigation input |
| 1 | WS2812B LED Strip | 8 LEDs, addressable RGB | Status indicators |
| 1 | Level Shifter (optional) | 74HCT125 or similar | 3.3V→5V for WS2812 data line. Usually not needed — see note in schematic. |

### Inputs

| Qty | Component | Part Number / Description | Notes |
|-----|-----------|---------------------------|-------|
| 1 | Water Meter | Contact closure, 1 pulse/gal | Dry contact reed switch |
| 1 | PC817 Optocoupler | DIP-4 (or EL817, 4N25) | 110VAC isolation for feedwater pump monitor |
| 2 | Resistor | 33 kΩ, ½W, metal film | Series current limit for optocoupler (110VAC side) |
| 1 | 1N4007 Diode | 1000V, 1A | Reverse voltage protection for optocoupler LED |
| 1 | Drum Level Switch (optional) | Float switch or probe | Auxiliary safety input |

### Passive Components

| Qty | Component | Value | Notes |
|-----|-----------|-------|-------|
| 2 | I2C Pull-up Resistors | 4.7 kohm | SDA and SCL to 3.3V |
| 3 | Input Pull-up Resistors | 10 kohm | GPIO34, GPIO35, GPIO0 to 3.3V |
| 1 | GPIO12 Pull-down Resistor | 10 kohm | GPIO12 to GND — **required** for boot strapping |
| 1 | WS2812 Series Resistor | 330-470 ohm | Data line protection |
| 1 | WS2812 Bypass Cap | 100-470 uF electrolytic | Power filtering near first LED |
| 1 | 100 nF Decoupling Cap | 0.1 uF ceramic | Near ADS1115 VDD |
| 3 | A4988 Decoupling Caps | 100 uF electrolytic | Near each A4988 VMOT (may be on board) |

### Power Supplies

| Qty | Component | Part Number / Description | Notes |
|-----|-----------|---------------------------|-------|
| 1 | 24 VDC Power Supply | DIN-rail, 60W+ | Primary supply for actuator + system |
| 1 | DC-DC Buck 24V→12V | 2A+ | Stepper motor VMOT supply |
| 1 | DC-DC Buck 24V→5V | 2A+ | ESP32, LCD, LEDs, relay |

---

## Wiring Color Code Recommendation

| Wire Color | Signal |
|------------|--------|
| Red | +24V, +12V, +5V (power positive) |
| Black | GND (all grounds) |
| Yellow | SPI CLK / STEP signals |
| Green | SPI MOSI / DIR signals |
| Blue | SPI MISO / I2C SDA |
| White | I2C SCL / UART TX |
| Orange | UART RX / Enable |
| Purple | Interrupt inputs (water meter, encoder) |
| Grey | Analog / feedback signals |

---

## Design Notes and Warnings

1. **Strapping Pins:** GPIO0, GPIO2, GPIO12, and GPIO15 affect boot behavior. **GPIO12 requires an external 10k pull-down resistor to GND** to guarantee the flash voltage regulator stays at 3.3V. GPIO0 requires a 10k pull-up (provided for the encoder button). GPIO15's encoder pull-up keeps it HIGH (enables boot log — safe). Alternatively, burn the VDD_SDIO eFuse once with `espefuse.py set_flash_voltage 3.3V` to permanently ignore GPIO12's strapping function.

2. **Input-Only Pins:** GPIO34, GPIO35, GPIO36 (VP), and GPIO39 (VN) are input-only with no internal pull-up/pull-down. External pull-up resistors are required for digital inputs on these pins.

3. **ADC2 vs WiFi:** ADC2 channels (GPIOs 0, 2, 4, 12-15, 25-27) cannot be used for analog reads while WiFi is active. All analog sensing uses either ADC1 pins or the external ADS1115.

4. **Common Ground:** The 24V supply ground, 12V ground, 5V ground, and ESP32 ground MUST all be connected together. Isolation is not used in this design. Use a common ground bus.

5. **Software SPI for MAX31865:** Hardware SPI pins (18, 19, 23) overlap with other functions. The MAX31865 uses software (bit-banged) SPI to avoid conflicts, with GPIO39 (input-only) as MISO.

6. **Shared Enable Pin:** All three A4988 drivers share GPIO13 as the enable pin. When any stepper needs to run, all drivers are enabled. Individual motors are controlled via their respective STEP pins. Idle motors hold position when enabled.

7. **Fail-Safe Blowdown:** Two independent fail-safe layers: (a) relay de-energizes on ESP32 power loss → 4 mA → valve closed, (b) actuator EP420C fails closed on signal loss.

8. **ADS1115 for Feedback:** The external 16-bit ADC reads the blowdown valve position feedback, avoiding ESP32 ADC nonlinearity issues and ADC2/WiFi conflicts. It shares the I2C bus with the LCD at address 0x48.

9. **GPIO19:** Used for SD card CS on the shared VSPI bus. For other expansion (e.g. alarm relay), use another spare when available.

10. **Coprocessor — RS-485 at boot:** When the coprocessor link is used, main board GPIO25/36 are connected to the RS-485 transceiver. Ensure the main board DE/RE pin is driven LOW at boot so it does not drive the bus; firmware should set DE/RE LOW until the first TX. Panel uses auto-direction (no DE).

11. **Coprocessor — Comms lost:** If no telemetry is received from the panel for the configured timeout (e.g. 5 s), the main should treat the link as lost and enter safe behavior: do not open blowdown, use last-known or safe defaults for display/logging. See [Coprocessor_Communication_Logic.md](Coprocessor_Communication_Logic.md).

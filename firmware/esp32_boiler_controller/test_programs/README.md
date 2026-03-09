# ESP32 Boiler Controller Test Programs

This directory contains standalone test programs for verifying individual components
of the Columbia CT-6 Boiler Dosing Controller.

## Available Test Programs

| Program | Description | Dependencies |
|---------|-------------|--------------|
| `test_stepper_pumps.cpp` | A4988 drivers, motor operation, calibration | AccelStepper |
| `test_dosing_pump.cpp` | Single dosing pump + water meter + LCD + encoder with per-gallon / per-day dosing menu | LiquidCrystal_I2C, AccelStepper, encoder |
| `test_conductivity_sensor.cpp` | Conductivity probe, Pt1000 RTD, calibration | - |
| `test_water_meter.cpp` | Pulse counting, flow rate, totalizer | - |
| `test_lcd_display.cpp` | I2C LCD, custom characters, screen layouts | LiquidCrystal_I2C |
| `test_wifi_api.cpp` | WiFi connection, HTTP client, API posting | ArduinoJson |
| `test_fuzzy_logic.cpp` | Membership functions, rule evaluation, scenarios | - |
| `test_gpio_pins.cpp` | All GPIO pins, I2C scan, relay/stepper tests | - |
| `test_ezo_conductivity.cpp` | Atlas Scientific EZO-EC UART, PT1000 RTD via MAX31865 | Adafruit_MAX31865 |
| `test_ezo_ds18b20.cpp` | EZO-EC + DS18B20 temp sensor (MAX31865 substitute) | OneWire, DallasTemperature |
| `test_integration.cpp` | Full system integration test with simulation | - |
| `test_fault_scenarios.cpp` | DeviceManager, SelfTest, SensorHealth fault injection (T1-T22) | - |
| `test_sensor_health_edge_cases.cpp` | SensorHealth edge cases, safe mode transitions (E1-E12) | - |
| `test_a4988_current_limit.cpp` | A4988 Vref/current limit setup, stall testing | - |
| `test_blowdown_valve.cpp` | Blowdown valve relay control, 4-20mA feedback via ADS1115 | - |
| `test_dual_temp_conductivity.cpp` | PT1000 RTD + DS18B20 + EZO-EC side-by-side comparison | Adafruit_MAX31865, OneWire, DallasTemperature |
| `test_coprocessor_protocol.cpp` | RS-485 coprocessor protocol: CRC16, frame build/parse, validity | coprocessor_protocol |
| `c3_coprocessor_stub.cpp` | ESP32 DevKit coprocessor stub: RS-485 (auto-direction), EZO on Serial1, internal ADC valve, telemetry (build with env `esp32dev_coprocessor`) | coprocessor_protocol |
| `test_c3_io.cpp` | **ESP32 DevKit**: Blowdown + solenoid relays (GPIO4/15), valve 4–20 mA + 2× CT RMS via internal ADC (GPIO36/39/34). Build: `test_c3_io` | c3_pin_definitions |

## ESP32 DevKit pin map (boiler panel coprocessor)

Tests and firmware for the **ESP32 DevKit** (boiler panel) use `include/c3_pin_definitions.h`:

| Function | GPIO | Notes |
|----------|------|-------|
| RS-485 TX | 17 | Serial2 (auto-direction module, no DE pin) |
| RS-485 RX | 16 | Serial2 |
| RS-485 DE/RE | — | Not used (auto-direction) |
| Blowdown relay | 4 | Relay module IN (high = on) |
| Solenoid relay | 15 | Relay module IN |
| Valve 4–20 mA | 36 | Internal ADC (150 Ω sense) |
| CT boiler power | 39 | Internal ADC (DC bias + software RMS) |
| CT low water | 34 | Internal ADC (DC bias + software RMS) |
| EZO-EC | 9 (RX), 10 (TX) | Serial1, 9600 baud |
| MAX31865 | 5 (CS), 18/23/19 (SCK/MOSI/MISO) | VSPI |

**Internal ADC:** Valve on GPIO36; CTs on GPIO39/34 with **DC bias circuit** (two 10 kΩ + 10 µF cap per channel) and firmware **software RMS** (see schematic Section 13a).

## Building with PlatformIO

Each test program has a corresponding environment in `platformio.ini`.

### Using PlatformIO CLI

```bash
# Build a specific test program
pio run -e test_stepper_pumps

# Build and upload to ESP32
pio run -e test_stepper_pumps -t upload

# Monitor serial output after upload
pio run -e test_stepper_pumps -t upload -t monitor
```

### Using VS Code with PlatformIO Extension

1. Open the project in VS Code
2. Click the PlatformIO icon in the sidebar
3. Expand "Project Tasks"
4. Select the desired test environment (e.g., `test_stepper_pumps`)
5. Click "Build" or "Upload"

## Available Environments

```ini
[env:test_stepper_pumps]      # Stepper motor/pump test
[env:test_dosing_pump]        # Dosing pump + water meter + LCD + encoder
[env:test_conductivity_sensor] # Conductivity sensor test
[env:test_water_meter]         # Water meter pulse test
[env:test_lcd_display]         # LCD display test
[env:test_wifi_api]            # WiFi and API test
[env:test_fuzzy_logic]         # Fuzzy logic controller test
[env:test_gpio_pins]           # GPIO pin test
[env:test_ezo_conductivity]    # EZO-EC + PT1000 RTD test
[env:test_integration]                  # Full integration test
[env:test_fault_scenarios]             # Fault injection tests (T1-T22)
[env:test_sensor_health_edge_cases]    # Sensor health edge cases (E1-E12)
[env:test_ezo_ds18b20]                # EZO-EC + DS18B20 (no MAX31865 needed)
[env:test_a4988_current_limit]        # A4988 Vref/current limit setup
[env:test_blowdown_valve]             # Blowdown valve relay + 4-20mA feedback
[env:test_dual_temp_conductivity]     # PT1000 + DS18B20 + EZO-EC dual temp
```

## Usage Instructions

All test programs use a serial menu interface at **115200 baud**.

1. Upload the desired test program to your ESP32
2. Open Serial Monitor at 115200 baud
3. Press `h` or `?` to see the menu
4. Follow the prompts to test each feature

## Test Program Details

### test_stepper_pumps.cpp

Tests the three chemical dosing pumps (H2SO3, NaOH, Amine):
- Enable/disable drivers
- Forward and reverse operation
- Speed adjustment
- Calibration mode (ml per revolution)

### test_dosing_pump.cpp

Standalone dosing test with one stepper-driven chemical pump, the makeup water meter,
LCD, and rotary encoder:

- Uses canonical pins from `pin_definitions.h`:
  - **Dosing pump (Stepper 1 / H2SO3)**:
    - STEP: `STEPPER1_STEP_PIN` (GPIO12, X.STEP on CNC shield)
    - DIR: `STEPPER1_DIR_PIN` (GPIO14, X.DIR)
    - ENABLE (shared): `STEPPER_ENABLE_PIN` (GPIO13, common A4988 enable, active LOW)
  - **Water meter input**:
    - `WATER_METER_PIN` (GPIO34, input-only, 1 pulse = 1 gallon)
    - Debounce and pulses-per-gallon from `WATER_METER_DEBOUNCE_MS` / `WATER_METER_PULSES_PER_GAL`
  - **LCD (20x4 I2C)**:
    - Address: `LCD_I2C_ADDR` (default 0x27)
    - SDA/SCL: `I2C_SDA_PIN` (GPIO21), `I2C_SCL_PIN` (GPIO22)
  - **Rotary encoder**:
    - A/B/button: `ENCODER_PIN_A` (GPIO15), `ENCODER_PIN_B` (GPIO2), `ENCODER_BUTTON_PIN` (GPIO4 on main MCU)

Features:

- Dosing modes:
  - **Per gallon**: fixed cups of chemical per gallon (default 1 cup / 50 gal)
  - **Per day (average)**: target cups per day; computes effective cups/gal from gallons seen today,
    but never doses below the base per-gallon setting
- Two ways to increase chemical:
  - Increase **Dose/Gal** (higher concentration)
  - Increase **Target cups/day** so the system raises effective Dose/Gal with higher water usage
- LCD status screen shows mode, effective Dose/Gal, gallons today, and cups dispensed
- Encoder menu items:
  - `Mode` (PER_GAL / PER_DAY)
  - `Dose/gal` (cups/gal)
  - `Target/day` (cups/day)
  - `Pump prime` (fixed small volume)
  - `Reset totals` (clear today's gallons/cups)
- Serial debug at 115200 baud:
  - `p` = status, `d` = daily usage summary, `z` = reset totals, `s` = emergency stop, `h`/`?` = help

**Encoder details and tuning:**

- Hardware: KY-040 style rotary encoder; firmware assumes **4 quadrature edges per detent** via
  `ENCODER_STEPS_PER_NOTCH` in `pin_definitions.h`.
- UI behavior:
  - One physical click = one logical menu step or value change:
    - Dose/Gal: 0.005 cups/gal per click
    - Dose/Day: 0.2 cups/day per click
    - Calibration grams: 0.1 g per click
  - Acceleration is enabled on menu screens and disabled on value-edit screens so each detent
    maps cleanly to the expected increment.
- Rotation debouncing is handled by the quadrature state machine and per-detent accumulation in
  `encoder.cpp` (no time-based filtering of valid edges inside a click), which avoids missing fast
  detents.
- For troubleshooting detent counting, you can enable a verbose serial harness in
  `test_dosing_pump.cpp` by setting `#define ENCODER_DEBUG 1` near the top of the file; this logs
  menu selection changes and value-edit deltas for manual “N detents → N steps” checks.

### test_conductivity_sensor.cpp

Tests the Sensorex CS675HTTC conductivity probe and Pt1000 RTD:
- AC excitation circuit
- Temperature measurement
- Temperature compensation
- Calibration with standard solutions (1413, 2764, 12880 µS/cm)

### test_water_meter.cpp

Tests the makeup water meter pulse input:
- Interrupt-based pulse counting
- Flow rate calculation
- Totalizer accumulation
- Pulse simulation (using BOOT button)

### test_lcd_display.cpp

Tests the 20x4 I2C LCD display:
- I2C bus scan
- Backlight control
- Custom characters
- Main screen layout
- Alarm screen with blinking

### test_wifi_api.cpp

Tests WiFi and backend API communication:
- Network scanning
- WiFi connection
- HTTP POST to backend
- AP mode with local web server

**Note:** Edit `WIFI_SSID`, `WIFI_PASS`, and `API_HOST` at the top of the file.

### test_fuzzy_logic.cpp

Tests the fuzzy logic controller:
- Membership function calculation
- Rule evaluation
- Input/output visualization
- Preset test scenarios

### test_ezo_conductivity.cpp

Tests the Atlas Scientific EZO-EC conductivity circuit and Adafruit MAX31865 PT1000 RTD:
- EZO-EC UART communication (Serial2, 9600 baud)
- EZO command interface (R, Cal, T, K, Status, etc.)
- PT1000 RTD temperature reading via software SPI
- Temperature-compensated conductivity readings

### test_ezo_ds18b20.cpp

Drop-in replacement for `test_ezo_conductivity.cpp` when the MAX31865 board is unavailable.
Uses a DS18B20 OneWire digital temperature sensor on **GPIO16** for temperature-compensated
conductivity readings:
- DS18B20 initialization, ROM address discovery, OneWire bus scan
- 12-bit temperature readings (0.0625°C resolution)
- Temperature stability analysis (min/max/avg over 20 samples)
- EZO-EC `RT,<temp>` command for temperature-compensated EC readings
- Full EZO calibration suite (dry, single-point, two-point)
- Continuous mode with live DS18B20 + EZO combined output

**Wiring:** DS18B20 DATA → GPIO16, 4.7kΩ pull-up to 3.3V, VCC → 3.3V, GND → GND.
Parasitic power mode also supported (tie VCC to GND).

### test_gpio_pins.cpp

Tests all GPIO pins for hardware verification:
- Output pin toggle
- Input pin state
- ADC readings
- I2C bus scan
- Relay clicking test
- Stepper pulse test

### test_integration.cpp

Full system integration test:
- Runs all individual component tests
- 60-second operational simulation
- Generates pass/fail report

### test_fault_scenarios.cpp

Automated fault injection test harness for the error handling modules:
- **T1-T11:** Core fault scenarios (probe missing, intermittent faults, device enable/disable,
  stale readings, I2C recovery, dependencies, safe mode, zero-reading rejection, status reporting)
- **T12-T22:** Extended scenarios (temperature/feedback fault recovery, concurrent faults,
  boundary checks, stale data safe mode, unknown feed modes, fault counter limits,
  sentinel values, conductivity range boundaries, hold time enforcement, fault-clear-refault cycles)

Runs automatically on boot — all tests execute sequentially and print PASS/FAIL summary.
No interactive menu. Requires only an ESP32 with no external hardware connected.

### test_sensor_health_edge_cases.cpp

Edge case tests for SensorHealthMonitor and its DeviceManager integration:
- **E1-E4:** Staleness detection, safe mode auto-exit gaps, measurement freshness, I2C rate limiting
- **E5-E8:** Rapid oscillation, feedback range validation gaps, safe mode re-entry, disabled sensors
- **E9-E12:** Mask consistency, initial state verification, safe mode priority, suspect flag lifecycle

Documents known behavioral gaps with `KNOWN GAP` annotations in test output.
Runs automatically on boot with PASS/FAIL summary. No external hardware required.

### test_a4988_current_limit.cpp

Guides the user through setting the A4988 current limit via the Vref potentiometer:
- Select board type (Pololu, StepStick, FYSETC, Generic, or custom Rsense)
- Current-to-Vref reference table and calculator
- Slow step mode for measuring Vref with a multimeter
- Stall test at increasing speeds to verify current is sufficient
- Step-by-step procedure printed in the menu

**Procedure:** Select board type (`r`) -> Calculate target Vref (`V`) -> Enable drivers (`e`) ->
Slow step (`1`/`2`/`3`) while adjusting pot -> Verify with stall test (`4`/`5`/`6`).

### test_blowdown_valve.cpp

Tests the Assured Automation E26NRXS4UV-EP420C blowdown ball valve:
- SPDT relay control on GPIO4 (4mA closed / 20mA open via resistor select) on panel/coprocessor ESP32
- ADS1115 16-bit ADC reads 4-20mA position feedback (150 ohm sense resistor)
- Open/close with real-time feedback tracking and position confirmation
- Full cycle test with automatic timing measurement
- Continuous feedback monitor mode (500ms updates)
- I2C bus scan to locate ADS1115
- Fault detection for wiring issues or stuck valve

**Wiring:** GPIO4 -> MOSFET -> relay coil. Relay NC -> 3.3k (4mA closed).
Relay NO -> 680 ohm (20mA open). Actuator feedback -> 150 ohm -> ADS1115 CH0.

### test_dual_temp_conductivity.cpp

Runs PT1000 RTD (MAX31865) and DS18B20 digital temperature sensor simultaneously
alongside the Atlas Scientific EZO-EC conductivity circuit:
- Side-by-side temperature comparison with delta tracking
- 60-second comparison mode with delta min/max/avg statistics
- Selectable temp source for EZO compensation (RTD, DS18B20, average, or manual)
- Auto-fallback: if the selected sensor fails, uses the other or manual temp
- Full EZO calibration suite (dry, single-point, two-point)
- Individual sensor diagnostics (SPI, OneWire, UART)

**Wiring:** MAX31865 software SPI on adjacent 30-pin DevKitC right-header pins
(CS=GPIO19, SCK=GPIO18, MOSI=GPIO17, MISO=GPIO16).
DS18B20 DATA -> GPIO4 with 4.7k pull-up to 3.3V. EZO-EC UART TX=GPIO25, RX=GPIO36.

## Pin Definitions
Relay NO -> 680 ohm (20mA open). Actuator feedback -> 150 ohm -> ADS1115 CH0.

## Pin Definitions

Test programs use `#include "pin_definitions.h"` from the main firmware for canonical
pin assignments. If your hardware differs, edit that header — changes propagate to all tests.

## Troubleshooting

### "No I2C devices found"
- Check SDA/SCL wiring (GPIO21/GPIO22)
- Ensure 4.7kΩ pull-up resistors are present
- Try 0x3F instead of 0x27 for LCD address

### "WiFi connection failed"
- Verify SSID and password in the source file
- Check that your network is 2.4GHz (ESP32 doesn't support 5GHz)

### "Stepper motors not moving"
- Check A4988 driver wiring
- Verify 12V/24V power supply is connected
- Ensure ENABLE pin is LOW (drivers enabled)

### "ADC readings stuck at 0 or 4095"
- Check sensor wiring
- Verify voltage divider resistor values
- Ensure correct ADC attenuation (11dB for 0-3.3V)

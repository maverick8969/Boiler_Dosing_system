# ESP32 Boiler Controller Test Programs

This directory contains standalone test programs for verifying individual components
of the Columbia CT-6 Boiler Dosing Controller.

## Available Test Programs

| Program | Description | Dependencies |
|---------|-------------|--------------|
| `test_stepper_pumps.cpp` | A4988 drivers, motor operation, calibration | AccelStepper |
| `test_conductivity_sensor.cpp` | Conductivity probe, Pt1000 RTD, calibration | - |
| `test_water_meter.cpp` | Pulse counting, flow rate, totalizer | - |
| `test_lcd_display.cpp` | I2C LCD, custom characters, screen layouts | LiquidCrystal_I2C |
| `test_wifi_api.cpp` | WiFi connection, HTTP client, API posting | ArduinoJson |
| `test_fuzzy_logic.cpp` | Membership functions, rule evaluation, scenarios | - |
| `test_gpio_pins.cpp` | All GPIO pins, I2C scan, relay/stepper tests | - |
| `test_ezo_conductivity.cpp` | Atlas Scientific EZO-EC UART, PT1000 RTD via MAX31865 | Adafruit_MAX31865 |
| `test_integration.cpp` | Full system integration test with simulation | - |
| `test_fault_scenarios.cpp` | DeviceManager, SelfTest, SensorHealth fault injection (T1-T22) | - |
| `test_sensor_health_edge_cases.cpp` | SensorHealth edge cases, safe mode transitions (E1-E12) | - |

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

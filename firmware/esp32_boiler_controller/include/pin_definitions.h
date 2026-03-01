/**
 * @file pin_definitions.h
 * @brief ESP32 Pin Definitions for Columbia CT-6 Boiler Dosing Controller
 *
 * Hardware Configuration:
 * - 3x Nema17 Stepper Motors with A4988 Drivers (Chemical Pumps)
 *   - Hydrogen Sulfite Pump
 *   - Sodium Hydroxide Pump
 *   - Amine Pump
 * - Water Meter Input (1 pulse per gallon)
 * - 20x4 LCD Display (I2C)
 * - WS2812 RGB LED Strip for Status Indicators
 * - Sensorex CS675HTTC/P1K Conductivity Probe via Atlas Scientific EZO-EC (UART)
 * - PT1000 RTD Temperature Sensor via Adafruit MAX31865 (VSPI, shared bus)
 * - Micro-SD Card Data Logger (VSPI, shared bus with MAX31865)
 * - Feedwater Pump Monitor via PC817 Optocoupler (GPIO35)
 * - Automated Blowdown Valve (Assured Automation E26NRXS4UV-EP420C, 4-20mA)
 *
 * Based on CNC Shield V4.0 architecture adapted for ESP32
 */

#ifndef PIN_DEFINITIONS_H
#define PIN_DEFINITIONS_H

#include <Arduino.h>

// ============================================================================
// STEPPER MOTOR PINS (A4988 Drivers for Chemical Pumps)
// ============================================================================
// Using CNC Shield style pinout adapted for ESP32

// Stepper 1: Hydrogen Sulfite Pump (X-axis on CNC Shield)
// IMPORTANT: GPIO12 is a strapping pin (selects flash voltage at boot).
// An external 10k pull-down resistor to GND is REQUIRED on this pin to
// guarantee LOW at boot (3.3V flash). See Design Notes in schematic doc.
#define STEPPER1_STEP_PIN       GPIO_NUM_12   // X.STEP (10k pull-down required!)
#define STEPPER1_DIR_PIN        GPIO_NUM_14   // X.DIR
#define STEPPER1_ENABLE_PIN     GPIO_NUM_13   // Common Enable (active LOW)
#define STEPPER1_NAME           "H2SO3"

// Stepper 2: Sodium Hydroxide Pump (Y-axis on CNC Shield)
#define STEPPER2_STEP_PIN       GPIO_NUM_27   // Y.STEP
#define STEPPER2_DIR_PIN        GPIO_NUM_26   // Y.DIR
#define STEPPER2_ENABLE_PIN     GPIO_NUM_13   // Common Enable (shared)
#define STEPPER2_NAME           "NaOH"

// Stepper 3: Amine Pump (Z-axis on CNC Shield)
#define STEPPER3_STEP_PIN       GPIO_NUM_33   // Z.STEP
#define STEPPER3_DIR_PIN        GPIO_NUM_32   // Z.DIR
#define STEPPER3_ENABLE_PIN     GPIO_NUM_13   // Common Enable (shared)
#define STEPPER3_NAME           "Amine"

// Stepper Common Enable Pin (Active LOW - shared by all A4988 drivers)
#define STEPPER_ENABLE_PIN      GPIO_NUM_13

// Stepper Motor Configuration
#define STEPPER_STEPS_PER_REV   200           // 1.8 degree Nema17
#define STEPPER_MICROSTEPS      16            // A4988 microstepping setting
#define STEPPER_MAX_SPEED       1000          // Steps per second
#define STEPPER_ACCELERATION    500           // Steps per second^2

// ============================================================================
// WATER METER INPUT
// ============================================================================
// 1 pulse per gallon contact closure input

#define WATER_METER_PIN         GPIO_NUM_34   // Input only GPIO, interrupt capable
#define WATER_METER_DEBOUNCE_MS 50            // Debounce time in milliseconds
#define WATER_METER_PULSES_PER_GAL  1         // Pulses per gallon (configurable)

// ============================================================================
// CONDUCTIVITY SENSOR INTERFACE (Sensorex CS675HTTC/P1K via Atlas Scientific EZO-EC)
// ============================================================================
// Atlas Scientific EZO-EC in UART mode for conductivity measurement
// Adafruit MAX31865 via SPI for PT1000 RTD temperature compensation
// Cell Constant K=1.0 for 0-10,000 uS/cm range

// Atlas Scientific EZO-EC UART (Serial2)
#define EZO_EC_TX_PIN           GPIO_NUM_25   // ESP32 TX → EZO RX (was DAC1 excite)
#define EZO_EC_RX_PIN           GPIO_NUM_36   // ESP32 RX ← EZO TX (was ADC cond sense)
#define EZO_EC_BAUD_RATE        9600          // EZO default baud rate
#define EZO_EC_UART_NUM         2             // Use UART2 (Serial2)

// Adafruit MAX31865 PT1000 RTD (Software SPI)
#define MAX31865_CS_PIN         GPIO_NUM_16   // Chip Select (was BLOWDOWN_NO dual relay)
#define MAX31865_MOSI_PIN       GPIO_NUM_23   // Master Out Slave In (SPI MOSI)
#define MAX31865_MISO_PIN       GPIO_NUM_39   // Master In Slave Out (was ADC temp sense)
#define MAX31865_SCK_PIN        GPIO_NUM_18   // SPI Clock (was AUX_INPUT2)

// PT1000 RTD Configuration
#define RTD_NOMINAL_RESISTANCE  1000.0        // PT1000 nominal resistance at 0°C
#define RTD_REFERENCE_RESISTOR  4300.0        // MAX31865 reference resistor for PT1000
#define RTD_NUM_WIRES           2             // 2-wire RTD configuration (2, 3, or 4)

// Conductivity Sensor Configuration
#define COND_CELL_CONSTANT      1.0           // K=1.0 cm^-1
#define COND_RANGE_MIN          0             // Minimum range (uS/cm)
#define COND_RANGE_MAX          10000         // Maximum range (uS/cm)

// ============================================================================
// BLOWDOWN VALVE CONTROL (Assured Automation E26NRXS4UV-EP420C)
// ============================================================================
// 1" SS ball valve with S4UV actuator and DPS 4-20mA positioner (fail-closed)
//
// Control: SPDT relay selects between two resistors to generate 4mA (closed)
//          or 20mA (open) from the 24VDC supply into the actuator's DPS input.
//          GPIO4 drives the relay coil via MOSFET:
//            LOW  = relay de-energized (NC) = R_close selected = ~4mA  = CLOSED
//            HIGH = relay energized (NO)    = R_open selected  = ~20mA = OPEN
//
// Feedback: Actuator outputs 4-20mA position signal read via ADS1115 external
//           ADC (I2C). 150 ohm sense resistor converts to 0.6-3.0V.

#define BLOWDOWN_RELAY_PIN      GPIO_NUM_4    // SPDT relay coil (via MOSFET)

// 4-20mA Position Feedback via ADS1115 (I2C external ADC)
#define BLOWDOWN_FEEDBACK_ADS_CH    0         // ADS1115 channel 0 for feedback
#define BLOWDOWN_FEEDBACK_R_SENSE   150.0     // Sense resistor (ohms)

// 4-20mA Current Thresholds for binary position detection
#define BLOWDOWN_MA_CLOSED_MAX      5.0       // Below this = confirmed closed
#define BLOWDOWN_MA_OPEN_MIN        19.0      // Above this = confirmed open
#define BLOWDOWN_MA_FAULT_LOW       3.0       // Below this = wiring fault

// Ball Valve Timing (S4 actuator: 14-30 sec per 90 degrees)
#define BALL_VALVE_DELAY_DEFAULT    20        // Default delay in seconds (S4 actuator)
#define BALL_VALVE_DELAY_MAX        99        // Maximum configurable delay

// ============================================================================
// LCD DISPLAY (20x4 I2C)
// ============================================================================
// HD44780-compatible LCD with PCF8574 I2C backpack

#define LCD_I2C_ADDR            0x27          // Common I2C address (may be 0x3F)
#define LCD_COLS                20            // 20 characters per line
#define LCD_ROWS                4             // 4 lines
#define LCD_SDA_PIN             GPIO_NUM_21   // I2C Data
#define LCD_SCL_PIN             GPIO_NUM_22   // I2C Clock

// ============================================================================
// WS2812 RGB LED STATUS INDICATORS
// ============================================================================
// Addressable RGB LED strip for system status

#define WS2812_DATA_PIN         GPIO_NUM_5    // LED data output
#define WS2812_NUM_LEDS         8             // Number of LEDs in strip

// LED Index Assignments
#define LED_POWER               0             // Green = OK, Red = Error
#define LED_WIFI                1             // Blue = Connected, Off = Disconnected
#define LED_CONDUCTIVITY        2             // Color indicates conductivity level
#define LED_BLOWDOWN            3             // Yellow when active
#define LED_PUMP1_H2SO3         4             // Cyan when running
#define LED_PUMP2_NAOH          5             // Magenta when running
#define LED_PUMP3_AMINE         6             // Yellow when running
#define LED_ALARM               7             // Red when alarm active

// LED Brightness (0-255)
#define LED_BRIGHTNESS          128

// ============================================================================
// FEEDWATER PUMP MONITOR (110VAC → optocoupler → GPIO)
// ============================================================================
// Monitors the CT-6 boiler feedwater pump contactor via PC817 optocoupler.
// The pump-on indicator light (110VAC) drives the optocoupler's LED through
// series resistors (2x 33kΩ ½W + 1N4007 reverse protection). The phototransistor
// pulls GPIO35 LOW when the pump is running.
// Tracks: cycle count, per-cycle duration, cumulative on-time.

#define FEEDWATER_PUMP_PIN      GPIO_NUM_35   // Input only GPIO (no internal pull-up)
#define FEEDWATER_PUMP_ACTIVE   LOW           // Optocoupler pulls LOW when pump ON
#define FEEDWATER_PUMP_DEBOUNCE_MS  200       // Debounce for contactor chatter

// ============================================================================
// AUXILIARY DIGITAL INPUTS
// ============================================================================
// For drum level switches or other safety interlocks

#define AUX_INPUT1_PIN          GPIO_NUM_17   // Drum Level Switch 1
// Note: GPIO18 repurposed for MAX31865 SCK. Drum level switch 2 no longer available.

// ============================================================================
// ROTARY ENCODER INTERFACE (Primary Navigation + Select/Menu)
// ============================================================================
// KY-040 style rotary encoder with push button
// Uses hardware interrupts for reliable rotation detection
// Push button serves as select/menu — the only physical button input.

#define ENCODER_PIN_A           GPIO_NUM_15   // CLK - Encoder output A
#define ENCODER_PIN_B           GPIO_NUM_2    // DT  - Encoder output B
#define ENCODER_BUTTON_PIN      GPIO_NUM_0    // SW  - Push button (active LOW, select/menu)

// Encoder Configuration
#define ENCODER_STEPS_PER_NOTCH 4             // Pulses per detent (typical for KY-040)
#define ENCODER_DEBOUNCE_MS     5             // Debounce time for rotation
#define ENCODER_BTN_DEBOUNCE_MS 50            // Debounce time for button
#define ENCODER_LONG_PRESS_MS   1500          // Long press threshold (enter menu)
#define ENCODER_DOUBLE_PRESS_MS 300           // Double press window

// ============================================================================
// I2C BUS CONFIGURATION
// ============================================================================

#define I2C_SDA_PIN             GPIO_NUM_21
#define I2C_SCL_PIN             GPIO_NUM_22
#define I2C_FREQ                400000        // 400kHz Fast Mode

// External ADC (ADS1115) for blowdown valve 4-20mA position feedback
// Required: all ESP32 ADC1 input-only pins are occupied; ADS1115 provides
// 16-bit resolution on the shared I2C bus with no additional GPIO needed.
#define ADS1115_I2C_ADDR        0x48
#define USE_EXTERNAL_ADC        true          // Required for blowdown valve feedback

// ============================================================================
// SPI BUS — Shared VSPI (MAX31865 + SD Card)
// ============================================================================
// Both the MAX31865 PT1000 RTD and SD card module share the ESP32 hardware
// VSPI bus. Each device has its own CS pin; a FreeRTOS mutex (spiMutex)
// protects bus access between the Measurement and Logging tasks.
//
//   MOSI = GPIO23    (shared)
//   MISO = GPIO39    (shared, input-only — fine for MISO)
//   SCK  = GPIO18    (shared)
//   MAX31865 CS = GPIO16
//   SD Card  CS = GPIO19

#define SD_CS_PIN               GPIO_NUM_19   // SD card chip select (VSPI)
#define SD_SPI_FREQ             4000000       // 4 MHz SPI clock for SD card

// ============================================================================
// RS-485 COPROCESSOR LINK (Main ESP32 — when using ESP32-C3 at boiler panel)
// ============================================================================
// When USE_COPROCESSOR_LINK is defined, Serial2 is used for RS-485 to C3.
// EZO-EC and MAX31865 then reside on the C3; this UART is repurposed for the link.
#define CP_LINK_UART_NUM        2             // Serial2
#define CP_LINK_DE_RE_PIN      (-1)           // GPIO for DE/RE (set per board; -1 = not used)
#define CP_LINK_BAUD            115200

// ============================================================================
// PIN VALIDATION
// ============================================================================

// Pins that should NOT be used (reserved or strapping pins)
// GPIO6-11: Connected to integrated SPI flash (DO NOT USE)
// GPIO34-39: Input only (no internal pull-up/down)
// GPIO0: Boot button (use with care)
// GPIO2: Must be LOW during boot for serial flashing

// Safe output pins: 4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33
// Safe input pins: All of above plus 34, 35, 36, 39
// Caution pins: 12 (strapping, needs external 10k pull-down to GND)
//               15 (strapping, boot log control — pull-up is OK)

// ============================================================================
// GPIO SUMMARY TABLE
// ============================================================================
/*
| GPIO | Function              | Direction | Notes                              |
|------|-----------------------|-----------|------------------------------------|
| 0    | ENCODER_BUTTON (sel)  | Input     | Select/menu (strapping! 10k pull-up)|
| 2    | ENCODER_PIN_B (DT)    | Input     | Encoder output B (strapping)        |
| 4    | BLOWDOWN_RELAY        | Output    | SPDT relay for 4-20mA control       |
| 5    | WS2812_DATA           | Output    | LED strip                           |
| 12   | STEPPER1_STEP         | Output    | H2SO3 pump step (10k pull-down!)    |
| 13   | STEPPER_ENABLE        | Output    | Common enable (active LOW)          |
| 14   | STEPPER1_DIR          | Output    | H2SO3 pump direction                |
| 15   | ENCODER_PIN_A (CLK)   | Input     | Encoder output A (strapping)        |
| 16   | MAX31865_CS           | Output    | RTD SPI chip select                 |
| 17   | AUX_INPUT1            | Input     | Drum level switch                   |
| 18   | VSPI_SCK              | Output    | Shared SPI clock (MAX31865 + SD)    |
| 19   | SD_CS                 | Output    | SD card chip select (VSPI)          |
| 21   | I2C_SDA               | I/O       | LCD + ADS1115                       |
| 22   | I2C_SCL               | Output    | LCD + ADS1115                       |
| 23   | VSPI_MOSI             | Output    | Shared SPI data out (MAX31865 + SD) |
| 25   | EZO_EC_TX             | Output    | Atlas EZO-EC UART TX                |
| 26   | STEPPER2_DIR          | Output    | NaOH pump direction                 |
| 27   | STEPPER2_STEP         | Output    | NaOH pump step                      |
| 32   | STEPPER3_DIR          | Output    | Amine pump direction                |
| 33   | STEPPER3_STEP         | Output    | Amine pump step                     |
| 34   | WATER_METER           | Input     | Water meter pulses (input-only)     |
| 35   | FEEDWATER_PUMP_MON    | Input     | Pump contactor via optocoupler      |
| 36   | EZO_EC_RX             | Input     | Atlas EZO-EC UART RX (input-only)   |
| 39   | MAX31865_MISO         | Input     | RTD SPI data in (input-only)        |
| I2C  | ADS1115 CH0           | Input     | Blowdown valve 4-20mA feedback      |
*/

#endif // PIN_DEFINITIONS_H

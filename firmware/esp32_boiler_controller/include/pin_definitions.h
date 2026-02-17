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
 * - PT1000 RTD Temperature Sensor via Adafruit MAX31865 (SPI)
 * - Automated Blowdown Valve (Relay Output)
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
#define STEPPER1_STEP_PIN       GPIO_NUM_12   // X.STEP
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
// BLOWDOWN VALVE CONTROL
// ============================================================================
// Relay output for motorized ball valve or solenoid

#define BLOWDOWN_RELAY_PIN      GPIO_NUM_4    // Relay driver output
#define BLOWDOWN_NC_PIN         GPIO_NUM_4    // Normally Closed contact
// Note: GPIO16 repurposed for MAX31865 CS. Dual relay output no longer available.

// Ball Valve Timing (for motorized actuators)
#define BALL_VALVE_DELAY_DEFAULT    8         // Default delay in seconds (Worcester style)
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
// FLOW SWITCH INPUT
// ============================================================================
// Digital input for flow switch (disables outputs on no flow)

#define FLOW_SWITCH_PIN         GPIO_NUM_35   // Input only GPIO
#define FLOW_SWITCH_ACTIVE      LOW           // Active when flow present

// ============================================================================
// AUXILIARY DIGITAL INPUTS
// ============================================================================
// For drum level switches or other safety interlocks

#define AUX_INPUT1_PIN          GPIO_NUM_17   // Drum Level Switch 1
// Note: GPIO18 repurposed for MAX31865 SCK. Drum level switch 2 no longer available.

// ============================================================================
// KEYPAD INTERFACE (4x4 Matrix)
// ============================================================================
// For local user interface

#define KEYPAD_ROW1_PIN         GPIO_NUM_15
#define KEYPAD_ROW2_PIN         GPIO_NUM_2
#define KEYPAD_ROW3_PIN         GPIO_NUM_0
#define KEYPAD_ROW4_PIN         GPIO_NUM_4    // Shared with blowdown - use alternate config

#define KEYPAD_COL1_PIN         GPIO_NUM_16
#define KEYPAD_COL2_PIN         GPIO_NUM_17
#define KEYPAD_COL3_PIN         GPIO_NUM_18
#define KEYPAD_COL4_PIN         GPIO_NUM_19

// Note: If using full keypad, remap blowdown relay to GPIO_NUM_23
// Alternative configuration for simpler 4-button interface below

// ============================================================================
// ROTARY ENCODER INTERFACE (Primary Navigation)
// ============================================================================
// KY-040 style rotary encoder with push button
// Uses hardware interrupts for reliable rotation detection

#define ENCODER_PIN_A           GPIO_NUM_15   // CLK - Encoder output A
#define ENCODER_PIN_B           GPIO_NUM_2    // DT  - Encoder output B
#define ENCODER_BUTTON_PIN      GPIO_NUM_0    // SW  - Push button (active LOW)

// Encoder Configuration
#define ENCODER_STEPS_PER_NOTCH 4             // Pulses per detent (typical for KY-040)
#define ENCODER_DEBOUNCE_MS     5             // Debounce time for rotation
#define ENCODER_BTN_DEBOUNCE_MS 50            // Debounce time for button
#define ENCODER_LONG_PRESS_MS   1500          // Long press threshold
#define ENCODER_DOUBLE_PRESS_MS 300           // Double press window

// ============================================================================
// SIMPLIFIED BUTTON INTERFACE (Alternative to rotary encoder)
// ============================================================================
// Use these if not using rotary encoder

#define BTN_UP_PIN              GPIO_NUM_15   // Shared with ENCODER_PIN_A
#define BTN_DOWN_PIN            GPIO_NUM_2    // Shared with ENCODER_PIN_B
#define BTN_ENTER_PIN           GPIO_NUM_0    // Shared with ENCODER_BUTTON_PIN
#define BTN_MENU_PIN            GPIO_NUM_19   // Additional menu button (optional)

// ============================================================================
// WIFI INDICATOR
// ============================================================================

#define WIFI_STATUS_LED_PIN     GPIO_NUM_2    // Built-in LED on most ESP32 boards

// ============================================================================
// I2C BUS CONFIGURATION
// ============================================================================

#define I2C_SDA_PIN             GPIO_NUM_21
#define I2C_SCL_PIN             GPIO_NUM_22
#define I2C_FREQ                400000        // 400kHz Fast Mode

// Optional External ADC (ADS1115) for higher precision
#define ADS1115_I2C_ADDR        0x48
#define USE_EXTERNAL_ADC        false         // Set true if using ADS1115

// ============================================================================
// SPI BUS (MAX31865 uses software SPI, hardware SPI available for expansion)
// ============================================================================
// MAX31865 PT1000 RTD uses software SPI on dedicated pins (see conductivity section)
// Hardware SPI pins remain available for future expansion (SD card, etc.)

#define SPI_MOSI_PIN            GPIO_NUM_23   // Shared with MAX31865 MOSI
#define SPI_MISO_PIN            GPIO_NUM_19   // Available for expansion
#define SPI_SCK_PIN             GPIO_NUM_18   // Shared with MAX31865 SCK
#define SD_CS_PIN               GPIO_NUM_5    // Conflicts with WS2812 - choose one

// ============================================================================
// PIN VALIDATION
// ============================================================================

// Pins that should NOT be used (reserved or strapping pins)
// GPIO6-11: Connected to integrated SPI flash (DO NOT USE)
// GPIO34-39: Input only (no internal pull-up/down)
// GPIO0: Boot button (use with care)
// GPIO2: Must be LOW during boot for serial flashing

// Safe output pins: 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33
// Safe input pins: All of above plus 34, 35, 36, 39

// ============================================================================
// GPIO SUMMARY TABLE
// ============================================================================
/*
| GPIO | Function              | Direction | Notes                    |
|------|-----------------------|-----------|--------------------------|
| 0    | ENCODER_BUTTON        | Input     | Push button (strapping!) |
| 2    | ENCODER_PIN_B (DT)    | Input     | Encoder output B         |
| 4    | BLOWDOWN_RELAY        | Output    | Relay driver             |
| 5    | WS2812_DATA           | Output    | LED strip                |
| 12   | STEPPER1_STEP         | Output    | H2SO3 pump step          |
| 13   | STEPPER_ENABLE        | Output    | Common enable (active LOW)|
| 14   | STEPPER1_DIR          | Output    | H2SO3 pump direction     |
| 15   | ENCODER_PIN_A (CLK)   | Input     | Encoder output A         |
| 16   | MAX31865_CS           | Output    | RTD SPI chip select      |
| 17   | AUX_INPUT1            | Input     | Drum level 1             |
| 18   | MAX31865_SCK          | Output    | RTD SPI clock            |
| 19   | BTN_MENU (optional)   | Input     | Extra button             |
| 21   | I2C_SDA               | I/O       | LCD, sensors             |
| 22   | I2C_SCL               | Output    | LCD, sensors             |
| 23   | MAX31865_MOSI         | Output    | RTD SPI data out         |
| 25   | EZO_EC_TX             | Output    | Atlas EZO-EC UART TX     |
| 26   | STEPPER2_DIR          | Output    | NaOH pump direction      |
| 27   | STEPPER2_STEP         | Output    | NaOH pump step           |
| 32   | STEPPER3_DIR          | Output    | Amine pump direction     |
| 33   | STEPPER3_STEP         | Output    | Amine pump step          |
| 34   | WATER_METER           | Input     | Water meter pulses       |
| 35   | FLOW_SWITCH           | Input     | Flow switch              |
| 36   | EZO_EC_RX             | Input     | Atlas EZO-EC UART RX     |
| 39   | MAX31865_MISO         | Input     | RTD SPI data in          |
*/

#endif // PIN_DEFINITIONS_H

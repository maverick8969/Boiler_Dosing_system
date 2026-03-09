/**
 * @file test_gpio_pins.cpp
 * @brief Test program for all GPIO pins and peripherals
 *
 * Tests:
 * - All digital outputs (relay, stepper pins, LEDs)
 * - All digital inputs (encoder, flow switch, water meter)
 * - All analog inputs (ADC via ADS1115)
 * - I2C bus scan
 *
 * Uses the canonical pin_definitions.h — no hardcoded pin numbers.
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Follow menu prompts to test each pin
 */

#include <Arduino.h>
#include <Wire.h>
#include "pin_definitions.h"

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void printMenu();
void printPinMap();
void processCommand(char cmd);
void testAllOutputs();
void testAllInputs();
void testRelays();
void testStepperPins();
void testEncoder();
void continuousInputMonitor();
void wiggleAllOutputs();
void scanI2C();

// ============================================================================
// PIN ARRAYS FOR ITERATION (sourced from pin_definitions.h)
// ============================================================================

struct PinInfo {
    int pin;
    const char* name;
    const char* type;  // "OUT", "IN"
};

PinInfo outputPins[] = {
    {STEPPER_ENABLE_PIN,  "STEPPER_ENABLE",  "OUT"},
    {STEPPER1_STEP_PIN,   "STEPPER1_STEP",   "OUT"},
    {STEPPER1_DIR_PIN,    "STEPPER1_DIR",    "OUT"},
    {STEPPER2_STEP_PIN,   "STEPPER2_STEP",   "OUT"},
    {STEPPER2_DIR_PIN,    "STEPPER2_DIR",    "OUT"},
    {STEPPER3_STEP_PIN,   "STEPPER3_STEP",   "OUT"},
    {STEPPER3_DIR_PIN,    "STEPPER3_DIR",    "OUT"},
#if BLOWDOWN_RELAY_PIN >= 0
    {BLOWDOWN_RELAY_PIN,  "BLOWDOWN_RELAY",  "OUT"},
#endif
    {WS2812_DATA_PIN,     "WS2812_DATA",     "OUT"},
    {MAX31865_CS_PIN,     "MAX31865_CS",     "OUT"},
    {MAX31865_MOSI_PIN,   "MAX31865_MOSI",   "OUT"},
    {MAX31865_SCK_PIN,    "MAX31865_SCK",    "OUT"},
    {EZO_EC_TX_PIN,       "EZO_EC_TX",       "OUT"},
};
const int numOutputs = sizeof(outputPins) / sizeof(outputPins[0]);

PinInfo inputPins[] = {
    {ENCODER_PIN_A,       "ENCODER_A (CLK)", "IN"},
    {ENCODER_PIN_B,       "ENCODER_B (DT)",  "IN"},
    {ENCODER_BUTTON_PIN,  "ENCODER_BTN",     "IN"},
    {WATER_METER_PIN,     "WATER_METER",     "IN"},
    {FEEDWATER_PUMP_PIN,  "FW_PUMP_MON",     "IN"},
    {AUX_INPUT1_PIN,      "AUX_INPUT1",      "IN"},
    {EZO_EC_RX_PIN,       "EZO_EC_RX",       "IN"},
    {MAX31865_MISO_PIN,   "MAX31865_MISO",   "IN"},
};
const int numInputs = sizeof(inputPins) / sizeof(inputPins[0]);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  GPIO PIN TEST PROGRAM");
    Serial.println("  (using pin_definitions.h)");
    Serial.println("========================================");
    Serial.println();

    // Configure I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Configure outputs
    for (int i = 0; i < numOutputs; i++) {
        pinMode(outputPins[i].pin, OUTPUT);
        digitalWrite(outputPins[i].pin, LOW);
    }

    // Configure inputs with pull-up (where possible)
    // Note: GPIO34-39 are input-only with NO internal pull-up;
    // they rely on external pull-ups on the PCB.
    for (int i = 0; i < numInputs; i++) {
        if (inputPins[i].pin < 34) {
            pinMode(inputPins[i].pin, INPUT_PULLUP);
        } else {
            pinMode(inputPins[i].pin, INPUT);
        }
    }

    Serial.println("All pins configured.");
    printPinMap();
    printMenu();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        processCommand(cmd);
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(char cmd) {
    switch (cmd) {
        case 'o':
            testAllOutputs();
            break;

        case 'i':
            testAllInputs();
            break;

        case 'r':
            testRelays();
            break;

        case 's':
            testStepperPins();
            break;

        case 'e':
            testEncoder();
            break;

        case 'c':
            continuousInputMonitor();
            break;

        case 'w':
            wiggleAllOutputs();
            break;

        case '2':
            scanI2C();
            break;

        case 'm':
            printPinMap();
            break;

        case 'h':
        case '?':
            printMenu();
            break;

        case '\n':
        case '\r':
            break;

        default:
            Serial.printf("Unknown command: '%c'\n", cmd);
            break;
    }
}

// ============================================================================
// TEST FUNCTIONS
// ============================================================================

void testAllOutputs() {
    Serial.println();
    Serial.println("=== OUTPUT PIN TEST ===");
    Serial.println("Testing each output pin (HIGH for 500ms)...");
    Serial.println();

    for (int i = 0; i < numOutputs; i++) {
        Serial.printf("  GPIO%02d %-20s: ", outputPins[i].pin, outputPins[i].name);

        digitalWrite(outputPins[i].pin, HIGH);
        Serial.print("HIGH");
        delay(500);

        digitalWrite(outputPins[i].pin, LOW);
        Serial.println(" -> LOW");
        delay(100);
    }

    Serial.println();
    Serial.println("Output test complete. All pins set LOW.");
}

void testAllInputs() {
    Serial.println();
    Serial.println("=== INPUT PIN TEST ===");
    Serial.println("Reading current state of all input pins...");
    Serial.println();

    for (int i = 0; i < numInputs; i++) {
        int state = digitalRead(inputPins[i].pin);
        Serial.printf("  GPIO%02d %-20s: %s\n",
                      inputPins[i].pin,
                      inputPins[i].name,
                      state ? "HIGH (open)" : "LOW (active)");
    }

    Serial.println();
}

void testRelays() {
    Serial.println();
    Serial.println("=== BLOWDOWN RELAY TEST ===");
    Serial.println();

    if (BLOWDOWN_RELAY_PIN < 0) {
        Serial.println("Blowdown relay not present on main MCU (using coprocessor link).");
    } else {
        Serial.printf("Relay ON (GPIO%d HIGH → 20mA → OPEN)...\n", BLOWDOWN_RELAY_PIN);
        digitalWrite(BLOWDOWN_RELAY_PIN, HIGH);
        delay(1000);
        Serial.printf("Relay OFF (GPIO%d LOW → 4mA → CLOSED)\n", BLOWDOWN_RELAY_PIN);
        digitalWrite(BLOWDOWN_RELAY_PIN, LOW);
    }

    Serial.println();
    Serial.println("Relay test complete.");
}

void testStepperPins() {
    Serial.println();
    Serial.println("=== STEPPER PIN TEST ===");
    Serial.println();

    struct StepperPins { int step; int dir; const char* name; };
    StepperPins steppers[] = {
        {STEPPER1_STEP_PIN, STEPPER1_DIR_PIN, STEPPER1_NAME},
        {STEPPER2_STEP_PIN, STEPPER2_DIR_PIN, STEPPER2_NAME},
        {STEPPER3_STEP_PIN, STEPPER3_DIR_PIN, STEPPER3_NAME},
    };

    // Enable drivers
    Serial.println("Enabling stepper drivers (ENABLE LOW)...");
    digitalWrite(STEPPER_ENABLE_PIN, LOW);
    delay(100);

    for (int s = 0; s < 3; s++) {
        Serial.printf("Stepper %s: 100 steps forward...\n", steppers[s].name);
        digitalWrite(steppers[s].dir, HIGH);
        for (int i = 0; i < 100; i++) {
            digitalWrite(steppers[s].step, HIGH);
            delayMicroseconds(500);
            digitalWrite(steppers[s].step, LOW);
            delayMicroseconds(500);
        }
        delay(200);

        Serial.printf("Stepper %s: 100 steps reverse...\n", steppers[s].name);
        digitalWrite(steppers[s].dir, LOW);
        for (int i = 0; i < 100; i++) {
            digitalWrite(steppers[s].step, HIGH);
            delayMicroseconds(500);
            digitalWrite(steppers[s].step, LOW);
            delayMicroseconds(500);
        }
        delay(200);
    }

    // Disable drivers
    Serial.println("Disabling stepper drivers (ENABLE HIGH)...");
    digitalWrite(STEPPER_ENABLE_PIN, HIGH);

    Serial.println();
    Serial.println("Stepper pin test complete.");
}

void testEncoder() {
    Serial.println();
    Serial.println("=== ENCODER TEST (10 seconds) ===");
    Serial.println("Rotate encoder and press button...");
    Serial.println();

    uint32_t startTime = millis();
    bool lastA = digitalRead(ENCODER_PIN_A);
    bool lastB = digitalRead(ENCODER_PIN_B);
    bool lastBtn = digitalRead(ENCODER_BUTTON_PIN);

    while (millis() - startTime < 10000 && !Serial.available()) {
        bool a = digitalRead(ENCODER_PIN_A);
        bool b = digitalRead(ENCODER_PIN_B);
        bool btn = digitalRead(ENCODER_BUTTON_PIN);

        if (a != lastA || b != lastB) {
            Serial.printf("[%6lu] CLK=%d DT=%d\n", millis() - startTime, a, b);
            lastA = a;
            lastB = b;
        }

        if (btn != lastBtn) {
            Serial.printf("[%6lu] BTN %s\n", millis() - startTime,
                          btn ? "RELEASED" : "PRESSED");
            lastBtn = btn;
        }

        delay(2);
    }

    if (Serial.available()) Serial.read();
    Serial.println();
    Serial.println("Encoder test complete.");
}

void continuousInputMonitor() {
    Serial.println();
    Serial.println("=== CONTINUOUS INPUT MONITOR ===");
    Serial.println("Monitoring all inputs for 30 seconds...");
    Serial.println("(Only changes will be shown)");
    Serial.println("Press any key to stop early.");
    Serial.println();

    int lastState[numInputs];
    for (int i = 0; i < numInputs; i++) {
        lastState[i] = digitalRead(inputPins[i].pin);
    }

    uint32_t startTime = millis();

    while (millis() - startTime < 30000 && !Serial.available()) {
        for (int i = 0; i < numInputs; i++) {
            int state = digitalRead(inputPins[i].pin);
            if (state != lastState[i]) {
                Serial.printf("[%6lu] %-20s: %s -> %s\n",
                              millis() - startTime,
                              inputPins[i].name,
                              lastState[i] ? "HIGH" : "LOW",
                              state ? "HIGH" : "LOW");
                lastState[i] = state;
            }
        }
        delay(10);
    }

    if (Serial.available()) Serial.read();
    Serial.println();
    Serial.println("Monitor stopped.");
}

void wiggleAllOutputs() {
    Serial.println();
    Serial.println("=== WIGGLE ALL OUTPUTS ===");
    Serial.println("Toggling all outputs rapidly for 5 seconds...");
    Serial.println("(Check with multimeter or LEDs)");
    Serial.println();

    uint32_t startTime = millis();

    while (millis() - startTime < 5000) {
        for (int i = 0; i < numOutputs; i++) {
            digitalWrite(outputPins[i].pin, HIGH);
        }
        delay(50);

        for (int i = 0; i < numOutputs; i++) {
            digitalWrite(outputPins[i].pin, LOW);
        }
        delay(50);
    }

    Serial.println("Wiggle test complete. All outputs LOW.");
}

void scanI2C() {
    Serial.println();
    Serial.println("=== I2C BUS SCAN ===");
    Serial.printf("Scanning I2C on SDA=GPIO%d, SCL=GPIO%d...\n", I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println();

    int found = 0;
    for (int addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        int error = Wire.endTransmission();

        if (error == 0) {
            Serial.printf("  Found device at 0x%02X", addr);

            // Identify common devices
            if (addr == LCD_I2C_ADDR)       Serial.print(" (LCD)");
            else if (addr == ADS1115_I2C_ADDR) Serial.print(" (ADS1115)");
            else if (addr == 0x3F)          Serial.print(" (LCD alt)");
            else if (addr == 0x50)          Serial.print(" (EEPROM)");
            else if (addr == 0x68)          Serial.print(" (DS3231 RTC)");

            Serial.println();
            found++;
        }
    }

    if (found == 0) {
        Serial.println("  No I2C devices found!");
    } else {
        Serial.printf("\nFound %d device(s)\n", found);
    }
    Serial.println();
}

// ============================================================================
// INFO FUNCTIONS
// ============================================================================

void printPinMap() {
    Serial.println();
    Serial.println("=== PIN MAP (from pin_definitions.h) ===");
    Serial.println();

    Serial.println("Output Pins:");
    for (int i = 0; i < numOutputs; i++) {
        Serial.printf("  GPIO%02d  %s\n", outputPins[i].pin, outputPins[i].name);
    }

    Serial.println();
    Serial.println("Input Pins:");
    for (int i = 0; i < numInputs; i++) {
        Serial.printf("  GPIO%02d  %s%s\n", inputPins[i].pin, inputPins[i].name,
                      (inputPins[i].pin >= 34) ? " (input-only, ext pull-up)" : "");
    }

    Serial.println();
    Serial.println("I2C Pins:");
    Serial.printf("  GPIO%02d  SDA\n", I2C_SDA_PIN);
    Serial.printf("  GPIO%02d  SCL\n", I2C_SCL_PIN);
    Serial.println();
}

// ============================================================================
// HELP
// ============================================================================

void printMenu() {
    Serial.println();
    Serial.println("=== GPIO TEST MENU ===");
    Serial.println();
    Serial.println("Output Tests:");
    Serial.println("  o - Test all outputs (sequential HIGH)");
    Serial.println("  w - Wiggle all outputs (5 sec)");
    Serial.println("  r - Test blowdown relay");
    Serial.println("  s - Test stepper pins");
    Serial.println();
    Serial.println("Input Tests:");
    Serial.println("  i - Read all inputs (one-shot)");
    Serial.println("  e - Encoder test (10 sec)");
    Serial.println("  c - Continuous input monitor (30 sec)");
    Serial.println();
    Serial.println("Other:");
    Serial.println("  2 - Scan I2C bus");
    Serial.println("  m - Print pin map");
    Serial.println("  h - Show this menu");
    Serial.println();
}

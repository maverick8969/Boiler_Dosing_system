/**
 * @file test_gpio_pins.ino
 * @brief Test program for all GPIO pins and peripherals
 *
 * Tests:
 * - All digital outputs (relays, LEDs)
 * - All digital inputs (buttons, switches)
 * - All analog inputs (ADC)
 * - PWM outputs
 * - I2C bus scan
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Follow menu prompts to test each pin
 */

#include <Arduino.h>
#include <Wire.h>

// ============================================================================
// PIN DEFINITIONS - MATCH YOUR HARDWARE
// ============================================================================

// Stepper motor pins
#define STEPPER_ENABLE_PIN  13
#define STEPPER1_STEP_PIN   27
#define STEPPER1_DIR_PIN    26
#define STEPPER2_STEP_PIN   25
#define STEPPER2_DIR_PIN    33
#define STEPPER3_STEP_PIN   32
#define STEPPER3_DIR_PIN    14

// Relay outputs
#define BLOWDOWN_RELAY_PIN  15
#define ALARM_RELAY_PIN     2

// Sensor inputs (ADC)
#define COND_EXCITE_PIN     4
#define COND_SENSE_PIN      34  // ADC1_CH6
#define TEMP_SENSE_PIN      35  // ADC1_CH7

// Water meter inputs
#define WATER_METER1_PIN    36  // ADC1_CH0
#define WATER_METER2_PIN    39  // ADC1_CH3

// Flow switch
#define FLOW_SWITCH_PIN     5

// Auxiliary inputs
#define AUX_INPUT1_PIN      16
#define AUX_INPUT2_PIN      17

// Button inputs
#define BTN_UP_PIN          18
#define BTN_DOWN_PIN        19
#define BTN_ENTER_PIN       23
#define BTN_MENU_PIN        0   // Boot button

// I2C
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22

// WS2812 LED
#define WS2812_PIN          12

// ============================================================================
// PIN ARRAYS FOR ITERATION
// ============================================================================

struct PinInfo {
    int pin;
    const char* name;
    const char* type;  // "OUT", "IN", "ADC", "PWM"
};

PinInfo outputPins[] = {
    {STEPPER_ENABLE_PIN, "STEPPER_ENABLE", "OUT"},
    {STEPPER1_STEP_PIN, "STEPPER1_STEP", "OUT"},
    {STEPPER1_DIR_PIN, "STEPPER1_DIR", "OUT"},
    {STEPPER2_STEP_PIN, "STEPPER2_STEP", "OUT"},
    {STEPPER2_DIR_PIN, "STEPPER2_DIR", "OUT"},
    {STEPPER3_STEP_PIN, "STEPPER3_STEP", "OUT"},
    {STEPPER3_DIR_PIN, "STEPPER3_DIR", "OUT"},
    {BLOWDOWN_RELAY_PIN, "BLOWDOWN_RELAY", "OUT"},
    {ALARM_RELAY_PIN, "ALARM_RELAY", "OUT"},
    {COND_EXCITE_PIN, "COND_EXCITE", "OUT"},
    {WS2812_PIN, "WS2812_LED", "OUT"},
};
const int numOutputs = sizeof(outputPins) / sizeof(outputPins[0]);

PinInfo inputPins[] = {
    {FLOW_SWITCH_PIN, "FLOW_SWITCH", "IN"},
    {AUX_INPUT1_PIN, "AUX_INPUT1", "IN"},
    {AUX_INPUT2_PIN, "AUX_INPUT2", "IN"},
    {BTN_UP_PIN, "BTN_UP", "IN"},
    {BTN_DOWN_PIN, "BTN_DOWN", "IN"},
    {BTN_ENTER_PIN, "BTN_ENTER", "IN"},
    {BTN_MENU_PIN, "BTN_MENU", "IN"},
};
const int numInputs = sizeof(inputPins) / sizeof(inputPins[0]);

PinInfo adcPins[] = {
    {COND_SENSE_PIN, "COND_SENSE", "ADC"},
    {TEMP_SENSE_PIN, "TEMP_SENSE", "ADC"},
    {WATER_METER1_PIN, "WATER_METER1", "ADC"},
    {WATER_METER2_PIN, "WATER_METER2", "ADC"},
};
const int numADC = sizeof(adcPins) / sizeof(adcPins[0]);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  GPIO PIN TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();

    // Configure I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Configure outputs
    for (int i = 0; i < numOutputs; i++) {
        pinMode(outputPins[i].pin, OUTPUT);
        digitalWrite(outputPins[i].pin, LOW);
    }

    // Configure inputs with pull-up
    for (int i = 0; i < numInputs; i++) {
        pinMode(inputPins[i].pin, INPUT_PULLUP);
    }

    // Configure ADC
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

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

        case 'a':
            testAllADC();
            break;

        case 'r':
            testRelays();
            break;

        case 's':
            testStepperPins();
            break;

        case 'b':
            testButtons();
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
        Serial.printf("  GPIO%02d %-15s: %s\n",
                      inputPins[i].pin,
                      inputPins[i].name,
                      state ? "HIGH (open)" : "LOW (active)");
    }

    Serial.println();
}

void testAllADC() {
    Serial.println();
    Serial.println("=== ADC PIN TEST ===");
    Serial.println("Reading all ADC pins 5 times...");
    Serial.println();

    for (int sample = 0; sample < 5; sample++) {
        Serial.printf("Sample %d:\n", sample + 1);
        for (int i = 0; i < numADC; i++) {
            int value = analogRead(adcPins[i].pin);
            float voltage = value * 3.3 / 4095.0;
            Serial.printf("  GPIO%02d %-15s: %4d (%.3fV)\n",
                          adcPins[i].pin,
                          adcPins[i].name,
                          value, voltage);
        }
        Serial.println();
        delay(500);
    }
}

void testRelays() {
    Serial.println();
    Serial.println("=== RELAY TEST ===");
    Serial.println();

    Serial.println("Blowdown relay ON...");
    digitalWrite(BLOWDOWN_RELAY_PIN, HIGH);
    delay(1000);
    Serial.println("Blowdown relay OFF");
    digitalWrite(BLOWDOWN_RELAY_PIN, LOW);
    delay(500);

    Serial.println("Alarm relay ON...");
    digitalWrite(ALARM_RELAY_PIN, HIGH);
    delay(1000);
    Serial.println("Alarm relay OFF");
    digitalWrite(ALARM_RELAY_PIN, LOW);

    Serial.println();
    Serial.println("Relay test complete.");
}

void testStepperPins() {
    Serial.println();
    Serial.println("=== STEPPER PIN TEST ===");
    Serial.println();

    // Enable drivers
    Serial.println("Enabling stepper drivers (ENABLE LOW)...");
    digitalWrite(STEPPER_ENABLE_PIN, LOW);
    delay(100);

    // Test each stepper
    for (int stepper = 1; stepper <= 3; stepper++) {
        int stepPin, dirPin;
        switch (stepper) {
            case 1: stepPin = STEPPER1_STEP_PIN; dirPin = STEPPER1_DIR_PIN; break;
            case 2: stepPin = STEPPER2_STEP_PIN; dirPin = STEPPER2_DIR_PIN; break;
            case 3: stepPin = STEPPER3_STEP_PIN; dirPin = STEPPER3_DIR_PIN; break;
        }

        Serial.printf("Stepper %d: 100 steps forward...\n", stepper);
        digitalWrite(dirPin, HIGH);
        for (int i = 0; i < 100; i++) {
            digitalWrite(stepPin, HIGH);
            delayMicroseconds(500);
            digitalWrite(stepPin, LOW);
            delayMicroseconds(500);
        }
        delay(200);

        Serial.printf("Stepper %d: 100 steps reverse...\n", stepper);
        digitalWrite(dirPin, LOW);
        for (int i = 0; i < 100; i++) {
            digitalWrite(stepPin, HIGH);
            delayMicroseconds(500);
            digitalWrite(stepPin, LOW);
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

void testButtons() {
    Serial.println();
    Serial.println("=== BUTTON TEST ===");
    Serial.println("Press each button within 10 seconds...");
    Serial.println("(Buttons are active LOW with pull-up)");
    Serial.println();

    bool buttonPressed[numInputs] = {false};
    uint32_t startTime = millis();

    while (millis() - startTime < 10000) {
        for (int i = 0; i < numInputs; i++) {
            if (!buttonPressed[i] && digitalRead(inputPins[i].pin) == LOW) {
                buttonPressed[i] = true;
                Serial.printf("  %s pressed!\n", inputPins[i].name);
            }
        }
        delay(50);
    }

    Serial.println();
    Serial.println("Button test complete. Summary:");
    for (int i = 0; i < numInputs; i++) {
        Serial.printf("  %-15s: %s\n", inputPins[i].name,
                      buttonPressed[i] ? "PRESSED" : "not pressed");
    }
    Serial.println();
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
                Serial.printf("[%6lu] %-15s: %s -> %s\n",
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
            if (addr == 0x27 || addr == 0x3F) Serial.print(" (LCD)");
            else if (addr == 0x48) Serial.print(" (ADS1115/TMP102)");
            else if (addr == 0x50) Serial.print(" (EEPROM)");
            else if (addr == 0x68) Serial.print(" (DS3231 RTC)");
            else if (addr == 0x76 || addr == 0x77) Serial.print(" (BME280)");

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
    Serial.println("=== PIN MAP ===");
    Serial.println();

    Serial.println("Output Pins:");
    for (int i = 0; i < numOutputs; i++) {
        Serial.printf("  GPIO%02d  %s\n", outputPins[i].pin, outputPins[i].name);
    }

    Serial.println();
    Serial.println("Input Pins (with pull-up):");
    for (int i = 0; i < numInputs; i++) {
        Serial.printf("  GPIO%02d  %s\n", inputPins[i].pin, inputPins[i].name);
    }

    Serial.println();
    Serial.println("ADC Pins:");
    for (int i = 0; i < numADC; i++) {
        Serial.printf("  GPIO%02d  %s\n", adcPins[i].pin, adcPins[i].name);
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
    Serial.println("  r - Test relays only");
    Serial.println("  s - Test stepper pins");
    Serial.println();
    Serial.println("Input Tests:");
    Serial.println("  i - Read all inputs (one-shot)");
    Serial.println("  b - Button test (10 sec)");
    Serial.println("  c - Continuous input monitor (30 sec)");
    Serial.println();
    Serial.println("ADC Tests:");
    Serial.println("  a - Read all ADC pins");
    Serial.println();
    Serial.println("Other:");
    Serial.println("  2 - Scan I2C bus");
    Serial.println("  m - Print pin map");
    Serial.println("  h - Show this menu");
    Serial.println();
}

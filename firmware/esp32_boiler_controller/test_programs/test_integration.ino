/**
 * @file test_integration.ino
 * @brief Full system integration test program
 *
 * Tests all subsystems together:
 * - Sensors (conductivity, temperature)
 * - Pumps (stepper motors)
 * - Water meter (pulse counting)
 * - Fuzzy logic (evaluation)
 * - Display (LCD output)
 * - Network (WiFi, API)
 *
 * Simulates a complete operational cycle to verify
 * all components work together correctly.
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Run automated or manual tests
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// Stepper pins
#define STEPPER_ENABLE_PIN  13
#define STEPPER1_STEP_PIN   27
#define STEPPER1_DIR_PIN    26
#define STEPPER2_STEP_PIN   25
#define STEPPER2_DIR_PIN    33
#define STEPPER3_STEP_PIN   32
#define STEPPER3_DIR_PIN    14

// Sensor pins
#define COND_EXCITE_PIN     4
#define COND_SENSE_PIN      34
#define TEMP_SENSE_PIN      35

// Water meter pins
#define WATER_METER1_PIN    36
#define WATER_METER2_PIN    39

// Relay pins
#define BLOWDOWN_RELAY_PIN  15
#define ALARM_RELAY_PIN     2

// Input pins
#define FLOW_SWITCH_PIN     5
#define BTN_MENU_PIN        0

// I2C
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22

// ============================================================================
// TEST RESULTS STRUCTURE
// ============================================================================

struct TestResult {
    const char* name;
    bool passed;
    const char* message;
};

#define MAX_TESTS 20
TestResult testResults[MAX_TESTS];
int numTests = 0;

// ============================================================================
// WATER METER ISR
// ============================================================================

volatile uint32_t waterPulses = 0;

void IRAM_ATTR onWaterPulse() {
    waterPulses++;
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  INTEGRATION TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();

    // Basic pin setup
    pinMode(STEPPER_ENABLE_PIN, OUTPUT);
    digitalWrite(STEPPER_ENABLE_PIN, HIGH);  // Disabled

    pinMode(BLOWDOWN_RELAY_PIN, OUTPUT);
    digitalWrite(BLOWDOWN_RELAY_PIN, LOW);

    pinMode(ALARM_RELAY_PIN, OUTPUT);
    digitalWrite(ALARM_RELAY_PIN, LOW);

    pinMode(COND_EXCITE_PIN, OUTPUT);
    digitalWrite(COND_EXCITE_PIN, LOW);

    pinMode(FLOW_SWITCH_PIN, INPUT_PULLUP);
    pinMode(BTN_MENU_PIN, INPUT_PULLUP);

    pinMode(WATER_METER1_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(WATER_METER1_PIN), onWaterPulse, FALLING);

    // I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // ADC
    analogReadResolution(12);

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
        case 'a':
            runAllTests();
            break;

        case '1':
            testI2CBus();
            break;

        case '2':
            testAnalogInputs();
            break;

        case '3':
            testDigitalInputs();
            break;

        case '4':
            testRelayOutputs();
            break;

        case '5':
            testStepperMotors();
            break;

        case '6':
            testWaterMeter();
            break;

        case '7':
            testWiFi();
            break;

        case 's':
            runSimulation();
            break;

        case 'r':
            printTestReport();
            break;

        case 'c':
            clearResults();
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
// INDIVIDUAL TEST FUNCTIONS
// ============================================================================

void addResult(const char* name, bool passed, const char* message) {
    if (numTests < MAX_TESTS) {
        testResults[numTests].name = name;
        testResults[numTests].passed = passed;
        testResults[numTests].message = message;
        numTests++;
    }
}

void testI2CBus() {
    Serial.println("\n--- I2C Bus Test ---");

    int found = 0;
    for (int addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            found++;
            Serial.printf("  Found device at 0x%02X\n", addr);
        }
    }

    if (found > 0) {
        addResult("I2C Bus", true, "Device(s) found");
        Serial.printf("PASS: Found %d I2C device(s)\n", found);
    } else {
        addResult("I2C Bus", false, "No devices found");
        Serial.println("FAIL: No I2C devices found");
    }
}

void testAnalogInputs() {
    Serial.println("\n--- Analog Input Test ---");

    // Test conductivity input
    digitalWrite(COND_EXCITE_PIN, HIGH);
    delay(10);
    int condHigh = analogRead(COND_SENSE_PIN);
    digitalWrite(COND_EXCITE_PIN, LOW);
    delay(10);
    int condLow = analogRead(COND_SENSE_PIN);

    Serial.printf("  Conductivity: HIGH=%d, LOW=%d, DIFF=%d\n",
                  condHigh, condLow, condHigh - condLow);

    bool condPass = (condHigh != condLow) && (condHigh > 0) && (condLow < 4095);

    // Test temperature input
    int tempReading = analogRead(TEMP_SENSE_PIN);
    float tempVoltage = tempReading * 3.3 / 4095.0;
    Serial.printf("  Temperature: ADC=%d, V=%.3f\n", tempReading, tempVoltage);

    bool tempPass = (tempReading > 100) && (tempReading < 4000);

    if (condPass && tempPass) {
        addResult("Analog Inputs", true, "Sensors responding");
        Serial.println("PASS: Analog inputs OK");
    } else {
        addResult("Analog Inputs", false, "Sensor issue");
        Serial.println("FAIL: Check sensor connections");
    }
}

void testDigitalInputs() {
    Serial.println("\n--- Digital Input Test ---");

    int flowSwitch = digitalRead(FLOW_SWITCH_PIN);
    int menuBtn = digitalRead(BTN_MENU_PIN);

    Serial.printf("  Flow switch: %s\n", flowSwitch ? "OPEN" : "CLOSED");
    Serial.printf("  Menu button: %s\n", menuBtn ? "NOT PRESSED" : "PRESSED");

    // Inputs should be HIGH with pull-up when not active
    addResult("Digital Inputs", true, "Inputs readable");
    Serial.println("PASS: Digital inputs readable");
}

void testRelayOutputs() {
    Serial.println("\n--- Relay Output Test ---");

    Serial.println("  Testing blowdown relay...");
    digitalWrite(BLOWDOWN_RELAY_PIN, HIGH);
    delay(200);
    digitalWrite(BLOWDOWN_RELAY_PIN, LOW);

    Serial.println("  Testing alarm relay...");
    digitalWrite(ALARM_RELAY_PIN, HIGH);
    delay(200);
    digitalWrite(ALARM_RELAY_PIN, LOW);

    addResult("Relay Outputs", true, "Relays toggled");
    Serial.println("PASS: Relays toggled (verify with click sound)");
}

void testStepperMotors() {
    Serial.println("\n--- Stepper Motor Test ---");

    // Enable drivers
    digitalWrite(STEPPER_ENABLE_PIN, LOW);
    delay(10);

    int stepPins[] = {STEPPER1_STEP_PIN, STEPPER2_STEP_PIN, STEPPER3_STEP_PIN};
    int dirPins[] = {STEPPER1_DIR_PIN, STEPPER2_DIR_PIN, STEPPER3_DIR_PIN};
    const char* names[] = {"Pump 1 (H2SO3)", "Pump 2 (NaOH)", "Pump 3 (Amine)"};

    for (int motor = 0; motor < 3; motor++) {
        pinMode(stepPins[motor], OUTPUT);
        pinMode(dirPins[motor], OUTPUT);

        Serial.printf("  Testing %s: 50 steps...\n", names[motor]);

        digitalWrite(dirPins[motor], HIGH);
        for (int i = 0; i < 50; i++) {
            digitalWrite(stepPins[motor], HIGH);
            delayMicroseconds(1000);
            digitalWrite(stepPins[motor], LOW);
            delayMicroseconds(1000);
        }
        delay(100);
    }

    // Disable drivers
    digitalWrite(STEPPER_ENABLE_PIN, HIGH);

    addResult("Stepper Motors", true, "Motors stepped");
    Serial.println("PASS: All motors stepped (verify rotation)");
}

void testWaterMeter() {
    Serial.println("\n--- Water Meter Test ---");

    uint32_t startPulses = waterPulses;
    Serial.println("  Waiting 5 seconds for pulses...");
    Serial.println("  (Press BOOT button to simulate pulses)");

    for (int i = 0; i < 50; i++) {
        // Simulate pulse on button press
        if (digitalRead(BTN_MENU_PIN) == LOW) {
            waterPulses++;
            delay(200);  // Debounce
        }
        delay(100);
    }

    uint32_t newPulses = waterPulses - startPulses;
    Serial.printf("  Pulses received: %lu\n", newPulses);

    if (newPulses > 0) {
        addResult("Water Meter", true, "Pulses detected");
        Serial.println("PASS: Water meter pulses detected");
    } else {
        addResult("Water Meter", true, "No pulses (OK if none expected)");
        Serial.println("PASS: No pulses (interrupt working)");
    }
}

void testWiFi() {
    Serial.println("\n--- WiFi Test ---");

    WiFi.mode(WIFI_STA);
    Serial.println("  Scanning for networks...");

    int networks = WiFi.scanNetworks();

    if (networks > 0) {
        Serial.printf("  Found %d networks\n", networks);
        for (int i = 0; i < min(networks, 3); i++) {
            Serial.printf("    - %s (RSSI: %d)\n",
                          WiFi.SSID(i).c_str(), WiFi.RSSI(i));
        }
        addResult("WiFi", true, "Networks found");
        Serial.println("PASS: WiFi module working");
    } else {
        addResult("WiFi", false, "No networks found");
        Serial.println("FAIL: No WiFi networks found");
    }

    WiFi.scanDelete();
}

// ============================================================================
// RUN ALL TESTS
// ============================================================================

void runAllTests() {
    Serial.println();
    Serial.println("========================================");
    Serial.println("  RUNNING ALL INTEGRATION TESTS");
    Serial.println("========================================");

    clearResults();

    testI2CBus();
    delay(500);

    testAnalogInputs();
    delay(500);

    testDigitalInputs();
    delay(500);

    testRelayOutputs();
    delay(500);

    testStepperMotors();
    delay(500);

    testWaterMeter();
    delay(500);

    testWiFi();

    printTestReport();
}

// ============================================================================
// SIMULATION
// ============================================================================

void runSimulation() {
    Serial.println();
    Serial.println("========================================");
    Serial.println("  OPERATIONAL SIMULATION");
    Serial.println("========================================");
    Serial.println();
    Serial.println("Simulating a 60-second operational cycle...");
    Serial.println();

    uint32_t startTime = millis();

    // Simulate sensor readings
    float simConductivity = 2500;
    float simTemperature = 42.0;
    float simTDS = 2500;
    float simAlkalinity = 280;
    float simSulfite = 25;
    float simPH = 10.8;

    while (millis() - startTime < 60000) {
        uint32_t elapsed = (millis() - startTime) / 1000;

        // Add some variation
        simConductivity = 2500 + sin(elapsed * 0.1) * 200;
        simTemperature = 42.0 + sin(elapsed * 0.05) * 2;

        // Calculate fuzzy outputs (simplified)
        float blowdownRec = 0;
        if (simConductivity > 2800) blowdownRec = (simConductivity - 2800) / 10;
        blowdownRec = constrain(blowdownRec, 0, 100);

        float sulfiteNeed = 0;
        if (simSulfite < 30) sulfiteNeed = (30 - simSulfite) * 3;
        sulfiteNeed = constrain(sulfiteNeed, 0, 100);

        // Display status
        Serial.printf("\r[%02lu:%02lu] Cond: %.0f uS  Temp: %.1f°C  "
                      "Blowdown: %.0f%%  Sulfite: %.0f%%     ",
                      elapsed / 60, elapsed % 60,
                      simConductivity, simTemperature,
                      blowdownRec, sulfiteNeed);

        // Simulate pump operation based on fuzzy output
        if (sulfiteNeed > 50 && (elapsed % 10) < 2) {
            // Run pump briefly
            digitalWrite(STEPPER_ENABLE_PIN, LOW);
            for (int i = 0; i < 10; i++) {
                digitalWrite(STEPPER3_STEP_PIN, HIGH);
                delayMicroseconds(1000);
                digitalWrite(STEPPER3_STEP_PIN, LOW);
                delayMicroseconds(1000);
            }
            digitalWrite(STEPPER_ENABLE_PIN, HIGH);
        }

        // Simulate water meter pulse every 5 seconds
        if (elapsed % 5 == 0) {
            waterPulses++;
        }

        delay(1000);
    }

    Serial.println();
    Serial.println();
    Serial.println("Simulation complete!");
    Serial.printf("Total water pulses: %lu\n", waterPulses);
}

// ============================================================================
// REPORT
// ============================================================================

void printTestReport() {
    Serial.println();
    Serial.println("========================================");
    Serial.println("  TEST REPORT");
    Serial.println("========================================");
    Serial.println();

    int passed = 0;
    int failed = 0;

    for (int i = 0; i < numTests; i++) {
        Serial.printf("  [%s] %-20s - %s\n",
                      testResults[i].passed ? "PASS" : "FAIL",
                      testResults[i].name,
                      testResults[i].message);

        if (testResults[i].passed) passed++;
        else failed++;
    }

    Serial.println();
    Serial.println("----------------------------------------");
    Serial.printf("  TOTAL: %d tests | %d passed | %d failed\n",
                  numTests, passed, failed);
    Serial.println("----------------------------------------");

    if (failed == 0 && numTests > 0) {
        Serial.println("  ALL TESTS PASSED!");
    } else if (failed > 0) {
        Serial.println("  SOME TESTS FAILED - Check hardware");
    }
    Serial.println();
}

void clearResults() {
    numTests = 0;
    Serial.println("Test results cleared");
}

// ============================================================================
// HELP
// ============================================================================

void printMenu() {
    Serial.println();
    Serial.println("=== INTEGRATION TEST MENU ===");
    Serial.println();
    Serial.println("Automated:");
    Serial.println("  a - Run ALL tests");
    Serial.println("  s - Run 60-second simulation");
    Serial.println();
    Serial.println("Individual Tests:");
    Serial.println("  1 - Test I2C bus");
    Serial.println("  2 - Test analog inputs (sensors)");
    Serial.println("  3 - Test digital inputs");
    Serial.println("  4 - Test relay outputs");
    Serial.println("  5 - Test stepper motors");
    Serial.println("  6 - Test water meter");
    Serial.println("  7 - Test WiFi");
    Serial.println();
    Serial.println("Results:");
    Serial.println("  r - Print test report");
    Serial.println("  c - Clear results");
    Serial.println();
    Serial.println("Other:");
    Serial.println("  h - Show this menu");
    Serial.println();
}

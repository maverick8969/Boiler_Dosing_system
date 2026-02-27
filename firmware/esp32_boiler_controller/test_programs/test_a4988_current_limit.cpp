/**
 * @file test_a4988_current_limit.cpp
 * @brief Test program for setting and verifying A4988 stepper driver current limit
 *
 * Purpose:
 * - Guide the user through setting the Vref potentiometer on each A4988 driver
 * - Calculate the required Vref for a desired motor current
 * - Run slow stepping for multimeter Vref measurement
 * - Test motor operation at increasing speeds to detect missed steps / stalling
 *
 * Hardware:
 * - 3x A4988 stepper drivers (Pololu-style breakout boards)
 * - 3x Nema17 stepper motors (1.8 deg, rated ~1.5-1.7A typical)
 * - Multimeter for measuring Vref on the A4988 potentiometer
 *
 * A4988 Current Limit Formula:
 *   I_trip = Vref / (8 x Rsense)
 *
 *   Rsense values by board manufacturer:
 *     Pololu:     0.068 ohm  -> Vref = I_trip x 0.544
 *     StepStick:  0.100 ohm  -> Vref = I_trip x 0.800
 *     FYSETC:     0.100 ohm  -> Vref = I_trip x 0.800
 *     Generic:    0.050 ohm  -> Vref = I_trip x 0.400
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Follow menu prompts to measure and set current limit
 *
 * Build:
 *   pio run -e test_a4988_current_limit -t upload -t monitor
 */

#include <Arduino.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void printMenu();
void processCommand(char cmd);
void toggleEnable();
void printCurrentCalc();
void promptVrefCalc();
void runSlowStep(int pumpIndex);
void runStallTest(int pumpIndex);
void runAllStallTest();
void stopMotor();
void selectRsense();
void printStatus();
void readSerialLine(char* buf, int maxLen);

// ============================================================================
// PIN DEFINITIONS (Match your hardware)
// ============================================================================

#define STEPPER_ENABLE_PIN  13  // Shared enable (active LOW)

#define STEPPER1_STEP_PIN   12  // Pump 1 (H2SO3)
#define STEPPER1_DIR_PIN    14

#define STEPPER2_STEP_PIN   27  // Pump 2 (NaOH)
#define STEPPER2_DIR_PIN    26

#define STEPPER3_STEP_PIN   33  // Pump 3 (Amine)
#define STEPPER3_DIR_PIN    32

// ============================================================================
// CONFIGURATION
// ============================================================================

#define STEPS_PER_REV       200     // Full steps per revolution (1.8 deg motor)
#define MICROSTEPPING       16      // A4988 microstepping setting
#define TOTAL_STEPS_REV     (STEPS_PER_REV * MICROSTEPPING)  // 3200 steps/rev

// Slow step timing for Vref measurement (motor barely turns)
#define SLOW_STEP_DELAY_MS  50      // 50ms between steps = 20 steps/sec

// Stall test speed levels (step delay in microseconds)
#define STALL_TEST_STEPS    200     // Steps per speed level (full steps)
#define STALL_TEST_LEVELS   6

// ============================================================================
// GLOBAL STATE
// ============================================================================

const int stepPins[]  = {STEPPER1_STEP_PIN, STEPPER2_STEP_PIN, STEPPER3_STEP_PIN};
const int dirPins[]   = {STEPPER1_DIR_PIN,  STEPPER2_DIR_PIN,  STEPPER3_DIR_PIN};
const char* pumpNames[] = {"H2SO3 (Pump 1)", "NaOH (Pump 2)", "Amine (Pump 3)"};

bool driversEnabled = false;
bool slowStepRunning = false;
int  slowStepPump = -1;

// Rsense value (default to Pololu)
float rsenseOhm = 0.068;
const char* boardName = "Pololu";

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  A4988 CURRENT LIMIT TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();

    // Configure enable pin - start disabled
    pinMode(STEPPER_ENABLE_PIN, OUTPUT);
    digitalWrite(STEPPER_ENABLE_PIN, HIGH);  // Disabled (active LOW)

    // Configure step and direction pins
    for (int i = 0; i < 3; i++) {
        pinMode(stepPins[i], OUTPUT);
        pinMode(dirPins[i], OUTPUT);
        digitalWrite(stepPins[i], LOW);
        digitalWrite(dirPins[i], HIGH);  // Default direction
    }

    Serial.println("Stepper drivers initialized (DISABLED).");
    Serial.println();
    Serial.printf("Board type: %s (Rsense = %.3f ohm)\n", boardName, rsenseOhm);
    Serial.println();

    printCurrentCalc();
    printMenu();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    // Process serial commands
    if (Serial.available()) {
        char cmd = Serial.read();

        // If slow stepping is running, any key stops it
        if (slowStepRunning) {
            slowStepRunning = false;
            Serial.println("\nSlow stepping STOPPED.");
            Serial.println();
            // Flush remaining serial input
            while (Serial.available()) Serial.read();
            return;
        }

        processCommand(cmd);
    }

    // Run slow stepping if active
    if (slowStepRunning && slowStepPump >= 0 && slowStepPump < 3) {
        digitalWrite(stepPins[slowStepPump], HIGH);
        delay(1);
        digitalWrite(stepPins[slowStepPump], LOW);
        delay(SLOW_STEP_DELAY_MS);
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(char cmd) {
    switch (cmd) {
        case 'e':
        case 'E':
            toggleEnable();
            break;

        case 'v':
            printCurrentCalc();
            break;

        case 'V':
            promptVrefCalc();
            break;

        case '1':
            runSlowStep(0);
            break;
        case '2':
            runSlowStep(1);
            break;
        case '3':
            runSlowStep(2);
            break;

        case '4':
            runStallTest(0);
            break;
        case '5':
            runStallTest(1);
            break;
        case '6':
            runStallTest(2);
            break;
        case '7':
            runAllStallTest();
            break;

        case 'r':
            selectRsense();
            break;

        case 'p':
            printStatus();
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
// DRIVER ENABLE/DISABLE
// ============================================================================

void toggleEnable() {
    driversEnabled = !driversEnabled;
    digitalWrite(STEPPER_ENABLE_PIN, driversEnabled ? LOW : HIGH);
    Serial.printf("Drivers %s\n", driversEnabled ? "ENABLED" : "DISABLED");

    if (driversEnabled) {
        Serial.println("Motors are now energized and holding position.");
        Serial.println("You can measure Vref on the A4988 potentiometer.");
    }
}

// ============================================================================
// CURRENT / VREF CALCULATIONS
// ============================================================================

void printCurrentCalc() {
    Serial.println("=== A4988 CURRENT LIMIT REFERENCE ===");
    Serial.println();
    Serial.println("Formula: I_trip = Vref / (8 x Rsense)");
    Serial.println("         Vref   = I_trip x 8 x Rsense");
    Serial.println();
    Serial.printf("Current board: %s (Rsense = %.3f ohm)\n", boardName, rsenseOhm);
    Serial.println();
    Serial.println("  Desired Current  |  Required Vref");
    Serial.println("  -----------------+---------------");

    float currents[] = {0.5, 0.7, 0.8, 1.0, 1.2, 1.5, 1.7, 2.0};
    for (int i = 0; i < 8; i++) {
        float vref = currents[i] * 8.0 * rsenseOhm;
        Serial.printf("     %4.1f A         |    %.3f V\n", currents[i], vref);
    }

    Serial.println();
    Serial.println("Typical Nema17 rated current: 1.5 - 1.7 A");
    Serial.println("Recommended: Start at 70%% of rated current.");
    Serial.println();
}

void promptVrefCalc() {
    Serial.println();
    Serial.println("Enter desired motor current in amps (e.g. 1.2):");

    char buf[16];
    readSerialLine(buf, sizeof(buf));

    float desiredCurrent = atof(buf);
    if (desiredCurrent <= 0.0 || desiredCurrent > 3.0) {
        Serial.println("Invalid current. Enter a value between 0.1 and 3.0 A.");
        return;
    }

    float vref = desiredCurrent * 8.0 * rsenseOhm;

    Serial.println();
    Serial.printf("Board:    %s (Rsense = %.3f ohm)\n", boardName, rsenseOhm);
    Serial.printf("Desired:  %.2f A\n", desiredCurrent);
    Serial.printf("Required: Vref = %.3f V\n", vref);
    Serial.println();
    Serial.println("Adjust the potentiometer on the A4988 until your");
    Serial.println("multimeter reads this voltage between the pot wiper");
    Serial.println("and logic GND.");
    Serial.println();

    if (vref > 1.5) {
        Serial.println("WARNING: Vref > 1.5V is very high. Ensure your motor");
        Serial.println("and driver can handle this current. Add heatsink/fan.");
    }
}

// ============================================================================
// SLOW STEP MODE (for Vref measurement)
// ============================================================================

void runSlowStep(int pumpIndex) {
    if (!driversEnabled) {
        Serial.println("ERROR: Enable drivers first (press 'e')");
        return;
    }

    Serial.println();
    Serial.printf("=== SLOW STEPPING: %s ===\n", pumpNames[pumpIndex]);
    Serial.println("Motor is stepping slowly. Measure Vref now.");
    Serial.println();
    Serial.println("Vref measurement points:");
    Serial.println("  (+) Multimeter probe on potentiometer wiper (small metal pad)");
    Serial.println("  (-) Multimeter probe on driver GND");
    Serial.println();
    Serial.println("Press any key to stop...");
    Serial.println();

    slowStepPump = pumpIndex;
    slowStepRunning = true;
}

// ============================================================================
// STALL TEST (increasing speed to detect current limit issues)
// ============================================================================

void runStallTest(int pumpIndex) {
    if (!driversEnabled) {
        Serial.println("ERROR: Enable drivers first (press 'e')");
        return;
    }

    Serial.println();
    Serial.printf("=== STALL TEST: %s ===\n", pumpNames[pumpIndex]);
    Serial.println("Running motor at increasing speeds.");
    Serial.println("Watch/listen for missed steps or stalling.");
    Serial.println("If motor stalls, current limit is too low.");
    Serial.println();

    // Speed levels: slow to fast (step delay in microseconds)
    int delays_us[] = {5000, 2000, 1000, 500, 300, 200};
    const char* speedLabels[] = {"Very Slow", "Slow", "Medium", "Fast", "Very Fast", "Maximum"};
    float stepsPerSec[] = {200, 500, 1000, 2000, 3333, 5000};

    for (int level = 0; level < STALL_TEST_LEVELS; level++) {
        Serial.printf("  Level %d/%d: %s (~%.0f steps/sec)... ",
                      level + 1, STALL_TEST_LEVELS, speedLabels[level], stepsPerSec[level]);

        // Run steps at this speed
        for (int step = 0; step < STALL_TEST_STEPS; step++) {
            digitalWrite(stepPins[pumpIndex], HIGH);
            delayMicroseconds(10);
            digitalWrite(stepPins[pumpIndex], LOW);
            delayMicroseconds(delays_us[level]);

            // Check for abort
            if (Serial.available()) {
                Serial.read();
                Serial.println("ABORTED");
                Serial.println("Stall test cancelled.");
                return;
            }
        }

        Serial.println("OK");

        // Pause between levels
        delay(500);
    }

    // Reverse direction and return to start
    Serial.println();
    Serial.println("  Reversing to start position...");
    digitalWrite(dirPins[pumpIndex], LOW);
    int totalSteps = STALL_TEST_STEPS * STALL_TEST_LEVELS;
    for (int step = 0; step < totalSteps; step++) {
        digitalWrite(stepPins[pumpIndex], HIGH);
        delayMicroseconds(10);
        digitalWrite(stepPins[pumpIndex], LOW);
        delayMicroseconds(1000);
    }
    digitalWrite(dirPins[pumpIndex], HIGH);  // Restore default direction

    Serial.println("  Done.");
    Serial.println();
    Serial.println("RESULTS:");
    Serial.println("  - If motor ran smoothly at all speeds: current limit is OK");
    Serial.println("  - If motor stalled or vibrated: increase Vref (turn pot CW)");
    Serial.println("  - If driver or motor got very hot: decrease Vref (turn pot CCW)");
    Serial.println();
}

void runAllStallTest() {
    for (int i = 0; i < 3; i++) {
        runStallTest(i);
        if (Serial.available()) {
            Serial.read();
            Serial.println("Remaining tests skipped.");
            return;
        }
    }
}

// ============================================================================
// RSENSE BOARD SELECTION
// ============================================================================

void selectRsense() {
    Serial.println();
    Serial.println("=== SELECT A4988 BOARD TYPE ===");
    Serial.println();
    Serial.println("  1 - Pololu A4988    (Rsense = 0.068 ohm)");
    Serial.println("  2 - StepStick       (Rsense = 0.100 ohm)");
    Serial.println("  3 - FYSETC          (Rsense = 0.100 ohm)");
    Serial.println("  4 - Generic/Clone   (Rsense = 0.050 ohm)");
    Serial.println("  5 - Enter custom Rsense value");
    Serial.println();
    Serial.println("Select (1-5):");

    char buf[16];
    readSerialLine(buf, sizeof(buf));

    switch (buf[0]) {
        case '1':
            rsenseOhm = 0.068;
            boardName = "Pololu";
            break;
        case '2':
            rsenseOhm = 0.100;
            boardName = "StepStick";
            break;
        case '3':
            rsenseOhm = 0.100;
            boardName = "FYSETC";
            break;
        case '4':
            rsenseOhm = 0.050;
            boardName = "Generic";
            break;
        case '5': {
            Serial.println("Enter Rsense in ohms (e.g. 0.068):");
            readSerialLine(buf, sizeof(buf));
            float val = atof(buf);
            if (val > 0.01 && val < 1.0) {
                rsenseOhm = val;
                boardName = "Custom";
            } else {
                Serial.println("Invalid value. Keeping current setting.");
                return;
            }
            break;
        }
        default:
            Serial.println("Invalid selection. Keeping current setting.");
            return;
    }

    Serial.printf("Board set to: %s (Rsense = %.3f ohm)\n", boardName, rsenseOhm);
    Serial.println();
}

// ============================================================================
// STATUS AND HELP
// ============================================================================

void printStatus() {
    Serial.println();
    Serial.println("=== STATUS ===");
    Serial.printf("Drivers: %s\n", driversEnabled ? "ENABLED" : "DISABLED");
    Serial.printf("Board type: %s (Rsense = %.3f ohm)\n", boardName, rsenseOhm);
    Serial.printf("Microstepping: %dx (%d steps/rev)\n", MICROSTEPPING, TOTAL_STEPS_REV);
    Serial.println();

    Serial.println("Pin Assignments:");
    for (int i = 0; i < 3; i++) {
        Serial.printf("  %s: STEP=GPIO%d, DIR=GPIO%d\n",
                      pumpNames[i], stepPins[i], dirPins[i]);
    }
    Serial.printf("  Enable: GPIO%d (active LOW)\n", STEPPER_ENABLE_PIN);
    Serial.println();
}

void printMenu() {
    Serial.println();
    Serial.println("=== A4988 CURRENT LIMIT TEST MENU ===");
    Serial.println();
    Serial.println("Driver Control:");
    Serial.println("  e - Toggle driver enable/disable");
    Serial.println();
    Serial.println("Vref / Current Calculation:");
    Serial.println("  v - Show current-to-Vref reference table");
    Serial.println("  V - Calculate Vref for a specific current");
    Serial.println("  r - Select A4988 board type (changes Rsense)");
    Serial.println();
    Serial.println("Slow Step (for Vref measurement with multimeter):");
    Serial.println("  1 - Slow step Pump 1 (H2SO3)");
    Serial.println("  2 - Slow step Pump 2 (NaOH)");
    Serial.println("  3 - Slow step Pump 3 (Amine)");
    Serial.println("     (press any key to stop slow stepping)");
    Serial.println();
    Serial.println("Stall Test (increasing speed to detect low current):");
    Serial.println("  4 - Stall test Pump 1 (H2SO3)");
    Serial.println("  5 - Stall test Pump 2 (NaOH)");
    Serial.println("  6 - Stall test Pump 3 (Amine)");
    Serial.println("  7 - Stall test ALL pumps");
    Serial.println();
    Serial.println("Other:");
    Serial.println("  p - Print status");
    Serial.println("  h - Show this menu");
    Serial.println();
    Serial.println("PROCEDURE:");
    Serial.println("  1. Press 'r' to select your A4988 board type");
    Serial.println("  2. Press 'V' to calculate Vref for your motor's rated current");
    Serial.println("  3. Press 'e' to enable drivers");
    Serial.println("  4. Press '1'/'2'/'3' to slow step while measuring Vref");
    Serial.println("  5. Adjust potentiometer until Vref matches target");
    Serial.println("  6. Press '4'/'5'/'6' to run stall test to verify");
    Serial.println();
}

// ============================================================================
// UTILITY
// ============================================================================

void readSerialLine(char* buf, int maxLen) {
    int idx = 0;
    unsigned long timeout = millis() + 30000;  // 30 second timeout

    while (millis() < timeout) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (idx > 0) break;  // Accept line if we have data
                continue;            // Skip leading newlines
            }
            if (idx < maxLen - 1) {
                buf[idx++] = c;
            }
        }
        delay(1);
    }
    buf[idx] = '\0';
}

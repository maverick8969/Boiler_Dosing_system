/**
 * @file test_stepper_pumps.cpp
 * @brief Test program for stepper motor chemical pumps
 *
 * Tests:
 * - A4988 driver enable/disable
 * - Individual pump motor operation
 * - Direction control
 * - Speed settings
 * - Steps per ml calibration
 *
 * Hardware:
 * - 3x Nema17 stepper motors
 * - 3x A4988 stepper drivers
 * - Shared enable pin
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Follow menu prompts to test each pump
 */

#include <Arduino.h>
#include <AccelStepper.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void printMenu();
void processCommand(char cmd);
void toggleEnable();
void runPump(int pumpIndex, long steps);
void runAllPumps(long steps);
void runRevolution(int pumpIndex);
void runAllRevolutions();
void runCalibration();
void adjustSpeed(int delta);
void stopAll();
void printStatus();

// ============================================================================
// PIN DEFINITIONS (Match your hardware)
// ============================================================================

// Stepper driver pins
#define STEPPER_ENABLE_PIN  13  // Shared enable (active LOW)

#define STEPPER1_STEP_PIN   27  // Pump 1 (H2SO3)
#define STEPPER1_DIR_PIN    26

#define STEPPER2_STEP_PIN   25  // Pump 2 (NaOH)
#define STEPPER2_DIR_PIN    33

#define STEPPER3_STEP_PIN   32  // Pump 3 (Amine)
#define STEPPER3_DIR_PIN    14

// ============================================================================
// CONFIGURATION
// ============================================================================

#define DEFAULT_SPEED       500     // steps/sec
#define DEFAULT_ACCEL       200     // steps/sec^2
#define STEPS_PER_REV       200     // Full steps per revolution
#define MICROSTEPPING       16      // A4988 microstepping setting
#define TOTAL_STEPS_REV     (STEPS_PER_REV * MICROSTEPPING)  // 3200 steps/rev

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

AccelStepper pump1(AccelStepper::DRIVER, STEPPER1_STEP_PIN, STEPPER1_DIR_PIN);
AccelStepper pump2(AccelStepper::DRIVER, STEPPER2_STEP_PIN, STEPPER2_DIR_PIN);
AccelStepper pump3(AccelStepper::DRIVER, STEPPER3_STEP_PIN, STEPPER3_DIR_PIN);

AccelStepper* pumps[] = {&pump1, &pump2, &pump3};
const char* pumpNames[] = {"H2SO3 (Pump 1)", "NaOH (Pump 2)", "Amine (Pump 3)"};

bool driversEnabled = false;
int currentSpeed = DEFAULT_SPEED;

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  STEPPER PUMP TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();

    // Configure enable pin
    pinMode(STEPPER_ENABLE_PIN, OUTPUT);
    digitalWrite(STEPPER_ENABLE_PIN, HIGH);  // Disabled (active LOW)

    // Configure all steppers
    for (int i = 0; i < 3; i++) {
        pumps[i]->setMaxSpeed(DEFAULT_SPEED);
        pumps[i]->setAcceleration(DEFAULT_ACCEL);
        pumps[i]->setCurrentPosition(0);
    }

    Serial.println("Stepper drivers initialized.");
    Serial.println("Drivers currently DISABLED (enable pin HIGH)");
    Serial.println();

    printMenu();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    // Process serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        processCommand(cmd);
    }

    // Run steppers (non-blocking)
    for (int i = 0; i < 3; i++) {
        pumps[i]->run();
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

        case '1':
            runPump(0, 1000);  // Run pump 1, 1000 steps
            break;
        case '2':
            runPump(1, 1000);
            break;
        case '3':
            runPump(2, 1000);
            break;

        case '!':
            runPump(0, -1000);  // Reverse direction
            break;
        case '@':
            runPump(1, -1000);
            break;
        case '#':
            runPump(2, -1000);
            break;

        case 'a':
            runAllPumps(1000);
            break;
        case 'A':
            runAllPumps(-1000);
            break;

        case 'r':
            runRevolution(0);
            break;
        case 'R':
            runAllRevolutions();
            break;

        case 'c':
            runCalibration();
            break;

        case '+':
            adjustSpeed(100);
            break;
        case '-':
            adjustSpeed(-100);
            break;

        case 's':
            stopAll();
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
            // Ignore newlines
            break;

        default:
            Serial.printf("Unknown command: '%c'\n", cmd);
            break;
    }
}

// ============================================================================
// PUMP CONTROL FUNCTIONS
// ============================================================================

void toggleEnable() {
    driversEnabled = !driversEnabled;
    digitalWrite(STEPPER_ENABLE_PIN, driversEnabled ? LOW : HIGH);

    Serial.printf("Drivers %s\n", driversEnabled ? "ENABLED" : "DISABLED");

    if (!driversEnabled) {
        Serial.println("WARNING: Motors will not hold position when disabled");
    }
}

void runPump(int pumpIndex, long steps) {
    if (!driversEnabled) {
        Serial.println("ERROR: Enable drivers first (press 'e')");
        return;
    }

    if (pumpIndex < 0 || pumpIndex > 2) {
        Serial.println("ERROR: Invalid pump index");
        return;
    }

    Serial.printf("Running %s: %ld steps at %d steps/sec\n",
                  pumpNames[pumpIndex], steps, currentSpeed);

    pumps[pumpIndex]->setMaxSpeed(currentSpeed);
    pumps[pumpIndex]->move(steps);
}

void runAllPumps(long steps) {
    if (!driversEnabled) {
        Serial.println("ERROR: Enable drivers first (press 'e')");
        return;
    }

    Serial.printf("Running ALL pumps: %ld steps\n", steps);

    for (int i = 0; i < 3; i++) {
        pumps[i]->setMaxSpeed(currentSpeed);
        pumps[i]->move(steps);
    }
}

void runRevolution(int pumpIndex) {
    if (!driversEnabled) {
        Serial.println("ERROR: Enable drivers first (press 'e')");
        return;
    }

    Serial.printf("Running %s: 1 full revolution (%d steps)\n",
                  pumpNames[pumpIndex], TOTAL_STEPS_REV);

    pumps[pumpIndex]->move(TOTAL_STEPS_REV);
}

void runAllRevolutions() {
    if (!driversEnabled) {
        Serial.println("ERROR: Enable drivers first (press 'e')");
        return;
    }

    Serial.println("Running ALL pumps: 1 revolution each");

    for (int i = 0; i < 3; i++) {
        pumps[i]->move(TOTAL_STEPS_REV);
    }
}

void runCalibration() {
    Serial.println();
    Serial.println("=== PUMP CALIBRATION MODE ===");
    Serial.println("This will run each pump to measure ml per revolution.");
    Serial.println();
    Serial.println("Instructions:");
    Serial.println("1. Place a graduated cylinder under the pump outlet");
    Serial.println("2. Prime the pump tubing first");
    Serial.println("3. Note the starting level in the cylinder");
    Serial.println("4. Run the specified number of revolutions");
    Serial.println("5. Measure the volume dispensed");
    Serial.println("6. Calculate: steps_per_ml = total_steps / ml_dispensed");
    Serial.println();
    Serial.println("Press '1', '2', or '3' to run 10 revolutions on that pump");
    Serial.printf("(10 rev = %d steps)\n", TOTAL_STEPS_REV * 10);
    Serial.println();

    // Wait for pump selection
    while (!Serial.available()) {
        delay(10);
    }

    char sel = Serial.read();
    int pumpIdx = sel - '1';

    if (pumpIdx < 0 || pumpIdx > 2) {
        Serial.println("Calibration cancelled");
        return;
    }

    if (!driversEnabled) {
        driversEnabled = true;
        digitalWrite(STEPPER_ENABLE_PIN, LOW);
        Serial.println("Drivers enabled");
    }

    long calSteps = TOTAL_STEPS_REV * 10;
    Serial.printf("\nRunning %s: 10 revolutions (%ld steps)...\n",
                  pumpNames[pumpIdx], calSteps);

    pumps[pumpIdx]->setMaxSpeed(currentSpeed);
    pumps[pumpIdx]->move(calSteps);

    // Wait for completion
    while (pumps[pumpIdx]->distanceToGo() != 0) {
        pumps[pumpIdx]->run();
    }

    Serial.println("Done!");
    Serial.println();
    Serial.println("Measure the volume dispensed and calculate:");
    Serial.printf("  steps_per_ml = %ld / ml_dispensed\n", calSteps);
    Serial.println();
}

void adjustSpeed(int delta) {
    currentSpeed += delta;
    currentSpeed = constrain(currentSpeed, 100, 2000);

    Serial.printf("Speed adjusted to %d steps/sec\n", currentSpeed);

    for (int i = 0; i < 3; i++) {
        pumps[i]->setMaxSpeed(currentSpeed);
    }
}

void stopAll() {
    Serial.println("STOPPING all pumps!");

    for (int i = 0; i < 3; i++) {
        pumps[i]->stop();
        pumps[i]->setCurrentPosition(pumps[i]->currentPosition());
    }
}

// ============================================================================
// STATUS AND HELP
// ============================================================================

void printStatus() {
    Serial.println();
    Serial.println("=== PUMP STATUS ===");
    Serial.printf("Drivers: %s\n", driversEnabled ? "ENABLED" : "DISABLED");
    Serial.printf("Speed: %d steps/sec\n", currentSpeed);
    Serial.printf("Steps per revolution: %d\n", TOTAL_STEPS_REV);
    Serial.println();

    for (int i = 0; i < 3; i++) {
        Serial.printf("%s:\n", pumpNames[i]);
        Serial.printf("  Position: %ld steps\n", pumps[i]->currentPosition());
        Serial.printf("  To go: %ld steps\n", pumps[i]->distanceToGo());
        Serial.printf("  Running: %s\n", pumps[i]->isRunning() ? "YES" : "NO");
    }
    Serial.println();
}

void printMenu() {
    Serial.println();
    Serial.println("=== STEPPER PUMP TEST MENU ===");
    Serial.println();
    Serial.println("Enable/Disable:");
    Serial.println("  e - Toggle driver enable");
    Serial.println();
    Serial.println("Run Forward (1000 steps):");
    Serial.println("  1 - Run Pump 1 (H2SO3)");
    Serial.println("  2 - Run Pump 2 (NaOH)");
    Serial.println("  3 - Run Pump 3 (Amine)");
    Serial.println("  a - Run ALL pumps");
    Serial.println();
    Serial.println("Run Reverse (1000 steps):");
    Serial.println("  ! - Reverse Pump 1");
    Serial.println("  @ - Reverse Pump 2");
    Serial.println("  # - Reverse Pump 3");
    Serial.println("  A - Reverse ALL");
    Serial.println();
    Serial.println("Full Revolutions:");
    Serial.println("  r - Run Pump 1 one revolution");
    Serial.println("  R - Run ALL one revolution");
    Serial.println();
    Serial.println("Calibration:");
    Serial.println("  c - Start calibration mode");
    Serial.println();
    Serial.println("Speed Control:");
    Serial.println("  + - Increase speed by 100");
    Serial.println("  - - Decrease speed by 100");
    Serial.println();
    Serial.println("Other:");
    Serial.println("  s - STOP all pumps");
    Serial.println("  p - Print status");
    Serial.println("  h - Show this menu");
    Serial.println();
}

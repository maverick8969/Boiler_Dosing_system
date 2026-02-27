/**
 * @file test_stepper_pumps.cpp
 * @brief Stepper pump test - Sulfite & Amine dosing pumps
 *
 * Hardware:
 * - 2x Nema17 stepper motors (Sulfite, Amine)
 * - 2x A4988 stepper drivers
 * - 1 shared DIR pin wired to both drivers (clockwise only, no reversing needed)
 * - Individual ENABLE pins per pump for UI-level on/off control
 *
 * Pulse Timer:
 * - Fires once every PULSE_INTERVAL_MS
 * - Each pulse commands ROTATIONS_PER_PULSE full rotations on each enabled pump
 * - Default: 1 pulse per 5 seconds → 2 rotations per pump
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Pumps start enabled; pulse timer fires automatically
 * - Use serial commands to toggle individual pumps, pause timer, or fire manually
 */

#include <Arduino.h>
#include <AccelStepper.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void printMenu();
void processCommand(char cmd);
void triggerPulse();
void stopAll();
void printStatus();

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// Shared DIR pin - wired to both A4988 DIR inputs; clockwise only
#define STEPPER_DIR_PIN         26

// Individual STEP pins
#define SULFITE_STEP_PIN        27  // Pump 1: Sulfite (oxygen scavenger)
#define AMINE_STEP_PIN          32  // Pump 2: Amine (condensate treatment)

// Individual ENABLE pins (active LOW) - wire each to a UI output for remote control
#define SULFITE_ENABLE_PIN      13  // Pump 1 enable
#define AMINE_ENABLE_PIN        14  // Pump 2 enable

// ============================================================================
// PULSE TIMER CONFIGURATION  <-- edit these to tune dosing cadence
// ============================================================================

#define PULSE_INTERVAL_MS       5000UL  // Time between pulses in milliseconds
#define ROTATIONS_PER_PULSE     2       // Full motor rotations per pulse

// ============================================================================
// MOTOR CONFIGURATION
// ============================================================================

#define DEFAULT_SPEED           500     // steps/sec
#define DEFAULT_ACCEL           200     // steps/sec^2
#define STEPS_PER_REV           200     // Full steps per revolution (Nema17)
#define MICROSTEPPING           16      // A4988 MS1/MS2/MS3 setting
#define TOTAL_STEPS_REV         (STEPS_PER_REV * MICROSTEPPING)  // 3200 steps/rev

// ============================================================================
// GLOBALS
// ============================================================================

// Both pumps share the DIR pin - AccelStepper controls it but we always move
// positive steps, so it stays HIGH (clockwise) throughout normal operation
AccelStepper sulfitePump(AccelStepper::DRIVER, SULFITE_STEP_PIN, STEPPER_DIR_PIN);
AccelStepper aminePump(AccelStepper::DRIVER, AMINE_STEP_PIN,   STEPPER_DIR_PIN);

AccelStepper* pumps[]     = { &sulfitePump,      &aminePump       };
const char*   pumpNames[] = { "Sulfite (Pump 1)", "Amine (Pump 2)" };
const int     enablePins[] = { SULFITE_ENABLE_PIN, AMINE_ENABLE_PIN };

bool          pumpEnabled[2]  = { true, true };  // per-pump enable state
bool          pulseTimerActive = true;
unsigned long lastPulseTime    = 0;
int           currentSpeed     = DEFAULT_SPEED;

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  STEPPER PUMP TEST - SULFITE & AMINE");
    Serial.println("========================================");
    Serial.println();

    // DIR pin - set HIGH for clockwise; AccelStepper will keep it HIGH as long
    // as we always command positive (forward) steps
    pinMode(STEPPER_DIR_PIN, OUTPUT);
    digitalWrite(STEPPER_DIR_PIN, HIGH);

    // Individual enable pins - start enabled (LOW = enabled on A4988)
    for (int i = 0; i < 2; i++) {
        pinMode(enablePins[i], OUTPUT);
        digitalWrite(enablePins[i], LOW);
    }

    // Configure AccelStepper instances
    for (int i = 0; i < 2; i++) {
        pumps[i]->setMaxSpeed(DEFAULT_SPEED);
        pumps[i]->setAcceleration(DEFAULT_ACCEL);
        pumps[i]->setCurrentPosition(0);
    }

    long stepsPerPulse = (long)ROTATIONS_PER_PULSE * TOTAL_STEPS_REV;
    Serial.printf("Pulse interval : %lu ms\n", PULSE_INTERVAL_MS);
    Serial.printf("Rotations/pulse: %d  (%ld steps)\n", ROTATIONS_PER_PULSE, stepsPerPulse);
    Serial.printf("Motor speed    : %d steps/sec\n", DEFAULT_SPEED);
    Serial.println("Both pumps ENABLED. Pulse timer ACTIVE.");
    Serial.println();

    printMenu();

    lastPulseTime = millis();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    // Handle serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd != '\n' && cmd != '\r') {
            processCommand(cmd);
        }
    }

    // Pulse timer - triggers both pumps on schedule
    if (pulseTimerActive && (millis() - lastPulseTime >= PULSE_INTERVAL_MS)) {
        lastPulseTime = millis();
        triggerPulse();
    }

    // Non-blocking stepper execution
    for (int i = 0; i < 2; i++) {
        pumps[i]->run();
    }
}

// ============================================================================
// PULSE TIMER
// ============================================================================

void triggerPulse() {
    long steps = (long)ROTATIONS_PER_PULSE * TOTAL_STEPS_REV;

    Serial.printf("[PULSE] %d rotation(s) = %ld steps per enabled pump\n",
                  ROTATIONS_PER_PULSE, steps);

    for (int i = 0; i < 2; i++) {
        if (pumpEnabled[i]) {
            pumps[i]->setMaxSpeed(currentSpeed);
            pumps[i]->move(steps);
            Serial.printf("  -> %-18s queued\n", pumpNames[i]);
        } else {
            Serial.printf("  -> %-18s DISABLED, skipped\n", pumpNames[i]);
        }
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(char cmd) {
    switch (cmd) {

        // ---- Per-pump enable toggle (tie enable pins to UI outputs) ----------
        case '1':
            pumpEnabled[0] = !pumpEnabled[0];
            digitalWrite(SULFITE_ENABLE_PIN, pumpEnabled[0] ? LOW : HIGH);
            Serial.printf("Sulfite pump: %s\n", pumpEnabled[0] ? "ENABLED" : "DISABLED");
            if (!pumpEnabled[0]) pumps[0]->stop();
            break;

        case '2':
            pumpEnabled[1] = !pumpEnabled[1];
            digitalWrite(AMINE_ENABLE_PIN, pumpEnabled[1] ? LOW : HIGH);
            Serial.printf("Amine pump  : %s\n", pumpEnabled[1] ? "ENABLED" : "DISABLED");
            if (!pumpEnabled[1]) pumps[1]->stop();
            break;

        // ---- Pulse timer controls -------------------------------------------
        case 't':
        case 'T':
            pulseTimerActive = !pulseTimerActive;
            Serial.printf("Pulse timer : %s\n", pulseTimerActive ? "ACTIVE" : "PAUSED");
            if (pulseTimerActive) lastPulseTime = millis();  // reset on resume
            break;

        case 'f':
        case 'F':
            Serial.println("Manual pulse fired:");
            triggerPulse();
            lastPulseTime = millis();  // restart interval from now
            break;

        // ---- Utility --------------------------------------------------------
        case 's':
        case 'S':
            stopAll();
            break;

        case 'p':
        case 'P':
            printStatus();
            break;

        case 'h':
        case '?':
            printMenu();
            break;

        default:
            Serial.printf("Unknown command: '%c'\n", cmd);
            break;
    }
}

// ============================================================================
// PUMP CONTROL
// ============================================================================

void stopAll() {
    Serial.println("STOP - clearing all pump queues");
    for (int i = 0; i < 2; i++) {
        pumps[i]->stop();
        pumps[i]->setCurrentPosition(pumps[i]->currentPosition());
    }
}

// ============================================================================
// STATUS & HELP
// ============================================================================

void printStatus() {
    unsigned long elapsed = millis() - lastPulseTime;
    unsigned long remaining = (elapsed >= PULSE_INTERVAL_MS) ? 0 : PULSE_INTERVAL_MS - elapsed;

    Serial.println();
    Serial.println("=== STATUS ===");
    Serial.printf("Pulse timer   : %s\n", pulseTimerActive ? "ACTIVE" : "PAUSED");
    Serial.printf("Interval      : %lu ms\n", PULSE_INTERVAL_MS);
    Serial.printf("Next pulse in : %lu ms\n", remaining);
    Serial.printf("Rotations/pls : %d  (%ld steps)\n",
                  ROTATIONS_PER_PULSE, (long)ROTATIONS_PER_PULSE * TOTAL_STEPS_REV);
    Serial.printf("Motor speed   : %d steps/sec\n", currentSpeed);
    Serial.println();

    for (int i = 0; i < 2; i++) {
        Serial.printf("%s\n", pumpNames[i]);
        Serial.printf("  Enabled : %s\n", pumpEnabled[i] ? "YES" : "NO");
        Serial.printf("  Running : %s\n", pumps[i]->isRunning() ? "YES" : "NO");
        Serial.printf("  Steps remaining: %ld\n", pumps[i]->distanceToGo());
        Serial.printf("  Total steps run: %ld\n", pumps[i]->currentPosition());
    }
    Serial.println();
}

void printMenu() {
    Serial.println();
    Serial.println("=== MENU ===");
    Serial.println();
    Serial.println("Pump enable (toggles ENABLE pin - connect to UI output):");
    Serial.println("  1  Toggle Sulfite pump ON/OFF");
    Serial.println("  2  Toggle Amine pump ON/OFF");
    Serial.println();
    Serial.println("Pulse timer:");
    Serial.println("  t  Toggle pulse timer ACTIVE/PAUSED");
    Serial.println("  f  Fire one pulse immediately (resets timer)");
    Serial.println();
    Serial.println("Utility:");
    Serial.println("  s  STOP all pumps (clear step queue)");
    Serial.println("  p  Print status");
    Serial.println("  h  Show this menu");
    Serial.println();
    Serial.printf("[ Pulse every %lu ms | %d rotations | %d steps/rev ]\n",
                  PULSE_INTERVAL_MS, ROTATIONS_PER_PULSE, TOTAL_STEPS_REV);
    Serial.println();
}

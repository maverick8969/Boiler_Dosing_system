/**
 * @file test_single_stepper_basic.cpp
 * @brief Bare-minimum single stepper motor test (no library, raw pulses)
 *
 * Purpose:
 *   Isolate hardware/wiring issues by removing AccelStepper as a variable.
 *   If the motor turns with this sketch but not the main test, the problem
 *   is in the higher-level code.  If it still doesn't turn, check wiring,
 *   power, and driver current-limit pot.
 *
 * What it does:
 *   1. Pulls ENABLE low (enables the A4988 driver)
 *   2. Pulses STEP at a fixed rate forever
 *   3. Prints a heartbeat to Serial every second so you can confirm the
 *      ESP32 is running even if the motor is not
 *
 * Wiring checklist before uploading:
 *   - VMOT & GND on A4988 connected to motor power supply (8-35 V)
 *   - VDD & GND connected to 3.3 V / 5 V logic
 *   - RESET and SLEEP pins tied together (or pulled HIGH)
 *   - MS1/MS2/MS3 set for desired microstepping (all LOW = full step)
 *   - Motor coil pairs wired to 1A/1B and 2A/2B
 *   - Current-limit pot set BEFORE powering motor (adjust with no load)
 *
 * To switch to a different pump, change the three pin defines below.
 */

#include <Arduino.h>

// ============================================================================
// CHANGE THESE TO MATCH THE PUMP YOU ARE TESTING
// ============================================================================

#define TEST_STEP_PIN       27      // STEP signal to A4988
#define TEST_DIR_PIN        26      // DIR  signal to A4988
#define TEST_ENABLE_PIN     13      // EN   signal to A4988 (active LOW)

// ============================================================================
// TIMING  (adjust if motor stalls or skips steps)
// ============================================================================

// Pulse width: A4988 minimum is 1 µs; 10 µs gives plenty of margin
#define STEP_PULSE_US       10      // microseconds HIGH during each step pulse

// Step interval: total time per step (HIGH + LOW).
// 2000 µs = 500 steps/sec.  At full-step (200 steps/rev) that is 2.5 rev/sec.
// At 16x microstepping (3200 steps/rev) it is 0.16 rev/sec — slow but visible.
// Increase this number if the motor hums but doesn't move (too fast).
#define STEP_INTERVAL_US    2000    // microseconds between step pulses

// ============================================================================
// GLOBALS
// ============================================================================

unsigned long stepCount      = 0;
unsigned long lastReportTime = 0;

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("======================================");
    Serial.println("  BASIC SINGLE STEPPER TEST");
    Serial.println("======================================");
    Serial.printf("STEP  -> GPIO %d\n", TEST_STEP_PIN);
    Serial.printf("DIR   -> GPIO %d\n", TEST_DIR_PIN);
    Serial.printf("EN    -> GPIO %d\n", TEST_ENABLE_PIN);
    Serial.printf("Pulse : %d us HIGH, %d us cycle\n", STEP_PULSE_US, STEP_INTERVAL_US);
    Serial.println();

    // Configure pins
    pinMode(TEST_STEP_PIN,   OUTPUT);
    pinMode(TEST_DIR_PIN,    OUTPUT);
    pinMode(TEST_ENABLE_PIN, OUTPUT);

    // Set direction: HIGH = clockwise (swap if motor runs backwards)
    digitalWrite(TEST_DIR_PIN,    HIGH);

    // Enable driver (active LOW on A4988)
    digitalWrite(TEST_ENABLE_PIN, LOW);

    // Step pin idle LOW
    digitalWrite(TEST_STEP_PIN,   LOW);

    delay(5);  // let driver wake up after enable

    Serial.println("Driver ENABLED. Motor should start turning now.");
    Serial.println("Heartbeat prints every 1 second.");
    Serial.println();

    lastReportTime = millis();
}

// ============================================================================
// LOOP  -  one raw step pulse per iteration
// ============================================================================

void loop() {
    // --- Generate one STEP pulse ---
    digitalWrite(TEST_STEP_PIN, HIGH);
    delayMicroseconds(STEP_PULSE_US);
    digitalWrite(TEST_STEP_PIN, LOW);
    delayMicroseconds(STEP_INTERVAL_US - STEP_PULSE_US);

    stepCount++;

    // --- Heartbeat every ~1 second ---
    if (millis() - lastReportTime >= 1000) {
        lastReportTime = millis();
        Serial.printf("[OK] Running - steps sent: %lu  (~%.1f rev)\n",
                      stepCount,
                      (float)stepCount / (200.0f * 16.0f));  // assumes 16x microstep
    }
}

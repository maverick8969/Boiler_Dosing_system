/**
 * @file test_blowdown_valve.cpp
 * @brief Test program for blowdown valve relay control and 4-20mA position feedback
 *
 * Tests:
 * - SPDT relay output on GPIO4 (4mA closed / 20mA open)
 * - ADS1115 I2C external ADC for 4-20mA position feedback
 * - Ball valve open/close cycle timing
 * - Position confirmation via feedback current
 * - Valve fault detection (wiring/stuck)
 *
 * Hardware:
 * - Assured Automation E26NRXS4UV-EP420C ball valve
 *   - S4UV actuator (14-30 sec per 90 degrees)
 *   - DPS 4-20mA positioner (fail-closed)
 * - SPDT relay on GPIO4 (via MOSFET)
 *   - De-energized (LOW)  = NC = R_close (3.3k ohm) = ~4mA  = CLOSED
 *   - Energized    (HIGH) = NO = R_open  (680 ohm)  = ~20mA = OPEN
 * - ADS1115 16-bit ADC on I2C (0x48)
 *   - Channel 0: 150 ohm sense resistor reads actuator 4-20mA feedback
 *   - 4mA = 0.6V (closed), 20mA = 3.0V (open)
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Follow menu prompts to test valve operation
 *
 * Build:
 *   pio run -e test_blowdown_valve -t upload -t monitor
 */

#include <Arduino.h>
#include <Wire.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void printMenu();
void processCommand(char cmd);
void openValve();
void closeValve();
void toggleValve();
void runCycleTest();
void readFeedback();
void startContinuousMonitor();
void scanI2C();
void printStatus();

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

#define BLOWDOWN_RELAY_PIN      4       // SPDT relay coil (via MOSFET)
#define I2C_SDA_PIN             21
#define I2C_SCL_PIN             22

// ============================================================================
// ADS1115 CONFIGURATION
// ============================================================================

#define ADS1115_I2C_ADDR        0x48
#define ADS1115_REG_CONVERSION  0x00
#define ADS1115_REG_CONFIG      0x01

// Config: OS=1, PGA=001(+/-4.096V), MODE=1(single-shot), DR=100(128SPS), COMP_QUE=11(disable)
#define ADS1115_CONFIG_BASE     0x4183

#define FEEDBACK_ADC_CHANNEL    0       // ADS1115 channel for 4-20mA feedback
#define FEEDBACK_R_SENSE        150.0   // Sense resistor (ohms)

// ============================================================================
// 4-20mA THRESHOLDS
// ============================================================================

#define MA_CLOSED_MAX           5.0     // Below this = confirmed closed
#define MA_OPEN_MIN             19.0    // Above this = confirmed open
#define MA_FAULT_LOW            3.0     // Below this = wiring fault
#define MA_FAULT_HIGH           22.0    // Above this = sensor fault

// ============================================================================
// BALL VALVE TIMING
// ============================================================================

#define VALVE_DELAY_DEFAULT_SEC 20      // S4 actuator: 14-30 sec per 90 degrees
#define VALVE_TIMEOUT_SEC       45      // Max time to wait for position confirm

// ============================================================================
// GLOBAL STATE
// ============================================================================

bool relayEnergized = false;
bool ads1115Available = false;
bool continuousMode = false;
float lastFeedbackmA = 0.0;
uint32_t lastFeedbackTime = 0;

// ============================================================================
// ADS1115 LOW-LEVEL FUNCTIONS
// ============================================================================

int16_t ads1115ReadChannel(uint8_t channel) {
    if (channel > 3) return 0;

    // Build config: single-ended input on specified channel
    uint16_t config = ADS1115_CONFIG_BASE;
    config &= ~(0x7000);
    config |= ((0x04 + channel) << 12);

    // Write config to start conversion
    Wire.beginTransmission(ADS1115_I2C_ADDR);
    Wire.write(ADS1115_REG_CONFIG);
    Wire.write((uint8_t)(config >> 8));
    Wire.write((uint8_t)(config & 0xFF));
    Wire.endTransmission();

    // Wait for conversion (128 SPS = ~8ms)
    delay(10);

    // Read result
    Wire.beginTransmission(ADS1115_I2C_ADDR);
    Wire.write(ADS1115_REG_CONVERSION);
    Wire.endTransmission();

    Wire.requestFrom((uint8_t)ADS1115_I2C_ADDR, (uint8_t)2);
    if (Wire.available() == 2) {
        int16_t result = (Wire.read() << 8) | Wire.read();
        return result;
    }

    return 0;
}

float readFeedbackmA() {
    if (!ads1115Available) return -1.0;

    int16_t raw = ads1115ReadChannel(FEEDBACK_ADC_CHANNEL);

    // ADS1115 at +/-4.096V: 1 LSB = 0.125 mV
    float voltage = raw * 0.000125;

    // I = V / R, convert to mA
    float current_mA = (voltage / FEEDBACK_R_SENSE) * 1000.0;

    if (current_mA < 0.0) current_mA = 0.0;
    if (current_mA > 25.0) current_mA = 25.0;

    return current_mA;
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  BLOWDOWN VALVE TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();
    Serial.println("Valve: Assured Automation E26NRXS4UV-EP420C");
    Serial.println("Control: SPDT relay -> 4-20mA resistor select");
    Serial.println("Feedback: ADS1115 -> 150 ohm sense resistor");
    Serial.println();

    // Configure relay output - start de-energized (valve closed)
    pinMode(BLOWDOWN_RELAY_PIN, OUTPUT);
    digitalWrite(BLOWDOWN_RELAY_PIN, LOW);
    Serial.printf("Relay pin: GPIO%d (LOW = closed, HIGH = open)\n", BLOWDOWN_RELAY_PIN);

    // Initialize I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    // Check for ADS1115
    Wire.beginTransmission(ADS1115_I2C_ADDR);
    ads1115Available = (Wire.endTransmission() == 0);

    if (ads1115Available) {
        Serial.printf("ADS1115 found at 0x%02X - feedback ENABLED\n", ADS1115_I2C_ADDR);
        float mA = readFeedbackmA();
        Serial.printf("Initial feedback: %.2f mA\n", mA);
    } else {
        Serial.printf("ADS1115 NOT found at 0x%02X - feedback DISABLED\n", ADS1115_I2C_ADDR);
        Serial.println("Relay control will still work, but no position confirmation.");
    }

    Serial.println();
    printMenu();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();

        if (continuousMode) {
            continuousMode = false;
            Serial.println("\nContinuous monitoring STOPPED.");
            Serial.println();
            while (Serial.available()) Serial.read();
            return;
        }

        processCommand(cmd);
    }

    // Continuous feedback display
    if (continuousMode && ads1115Available) {
        if (millis() - lastFeedbackTime >= 500) {
            lastFeedbackTime = millis();
            float mA = readFeedbackmA();
            lastFeedbackmA = mA;

            const char* position;
            if (mA < MA_FAULT_LOW)       position = "FAULT (low)";
            else if (mA < MA_CLOSED_MAX) position = "CLOSED";
            else if (mA < MA_OPEN_MIN)   position = "MOVING/PARTIAL";
            else if (mA <= MA_FAULT_HIGH) position = "OPEN";
            else                          position = "FAULT (high)";

            // Calculate approximate position percentage (4mA=0%, 20mA=100%)
            float pct = (mA - 4.0) / 16.0 * 100.0;
            if (pct < 0.0) pct = 0.0;
            if (pct > 100.0) pct = 100.0;

            Serial.printf("  Feedback: %6.2f mA | %5.1f%% open | %s | Relay: %s\n",
                          mA, pct, position,
                          relayEnergized ? "ENERGIZED (open)" : "DE-ENERGIZED (closed)");
        }
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(char cmd) {
    switch (cmd) {
        case 'o':
        case 'O':
            openValve();
            break;

        case 'c':
        case 'C':
            closeValve();
            break;

        case 't':
            toggleValve();
            break;

        case 'T':
            runCycleTest();
            break;

        case 'r':
            readFeedback();
            break;

        case 'm':
            startContinuousMonitor();
            break;

        case 'i':
            scanI2C();
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
// VALVE CONTROL
// ============================================================================

void openValve() {
    Serial.println();
    Serial.println(">>> OPENING valve (relay ENERGIZED, ~20mA)");
    digitalWrite(BLOWDOWN_RELAY_PIN, HIGH);
    relayEnergized = true;

    if (ads1115Available) {
        Serial.println("Waiting for position feedback...");
        uint32_t start = millis();
        float mA;

        while (millis() - start < VALVE_TIMEOUT_SEC * 1000) {
            mA = readFeedbackmA();
            float pct = (mA - 4.0) / 16.0 * 100.0;
            if (pct < 0.0) pct = 0.0;
            if (pct > 100.0) pct = 100.0;

            Serial.printf("  %.1f sec: %.2f mA (%.1f%% open)\n",
                          (millis() - start) / 1000.0, mA, pct);

            if (mA >= MA_OPEN_MIN) {
                float elapsed = (millis() - start) / 1000.0;
                Serial.printf("OPEN confirmed at %.2f mA (%.1f sec)\n", mA, elapsed);
                return;
            }

            delay(1000);
        }

        Serial.printf("TIMEOUT: Feedback %.2f mA after %d sec (expected >%.1f mA)\n",
                      mA, VALVE_TIMEOUT_SEC, MA_OPEN_MIN);
    } else {
        Serial.printf("No ADS1115 - waiting %d sec (default valve delay)...\n",
                      VALVE_DELAY_DEFAULT_SEC);
        delay(VALVE_DELAY_DEFAULT_SEC * 1000);
        Serial.println("Done (no feedback confirmation).");
    }
    Serial.println();
}

void closeValve() {
    Serial.println();
    Serial.println(">>> CLOSING valve (relay DE-ENERGIZED, ~4mA)");
    digitalWrite(BLOWDOWN_RELAY_PIN, LOW);
    relayEnergized = false;

    if (ads1115Available) {
        Serial.println("Waiting for position feedback...");
        uint32_t start = millis();
        float mA;

        while (millis() - start < VALVE_TIMEOUT_SEC * 1000) {
            mA = readFeedbackmA();
            float pct = (mA - 4.0) / 16.0 * 100.0;
            if (pct < 0.0) pct = 0.0;
            if (pct > 100.0) pct = 100.0;

            Serial.printf("  %.1f sec: %.2f mA (%.1f%% open)\n",
                          (millis() - start) / 1000.0, mA, pct);

            if (mA <= MA_CLOSED_MAX) {
                float elapsed = (millis() - start) / 1000.0;
                Serial.printf("CLOSED confirmed at %.2f mA (%.1f sec)\n", mA, elapsed);
                return;
            }

            delay(1000);
        }

        Serial.printf("TIMEOUT: Feedback %.2f mA after %d sec (expected <%.1f mA)\n",
                      mA, VALVE_TIMEOUT_SEC, MA_CLOSED_MAX);
    } else {
        Serial.printf("No ADS1115 - waiting %d sec (default valve delay)...\n",
                      VALVE_DELAY_DEFAULT_SEC);
        delay(VALVE_DELAY_DEFAULT_SEC * 1000);
        Serial.println("Done (no feedback confirmation).");
    }
    Serial.println();
}

void toggleValve() {
    if (relayEnergized) {
        closeValve();
    } else {
        openValve();
    }
}

// ============================================================================
// CYCLE TEST (open -> measure -> close -> measure)
// ============================================================================

void runCycleTest() {
    Serial.println();
    Serial.println("=== FULL CYCLE TEST ===");
    Serial.println("This will open and close the valve, measuring timing.");
    Serial.println();

    // Ensure valve starts closed
    if (relayEnergized) {
        Serial.println("Valve is currently open - closing first...");
        closeValve();
        delay(2000);
    }

    // Read baseline
    float baseline_mA = -1;
    if (ads1115Available) {
        baseline_mA = readFeedbackmA();
        Serial.printf("Baseline feedback: %.2f mA\n", baseline_mA);
    }
    Serial.println();

    // OPEN cycle
    Serial.println("--- OPEN CYCLE ---");
    uint32_t openStart = millis();
    digitalWrite(BLOWDOWN_RELAY_PIN, HIGH);
    relayEnergized = true;

    float openConfirmTime = -1;
    if (ads1115Available) {
        while (millis() - openStart < VALVE_TIMEOUT_SEC * 1000) {
            float mA = readFeedbackmA();
            float pct = (mA - 4.0) / 16.0 * 100.0;
            if (pct < 0.0) pct = 0.0;
            if (pct > 100.0) pct = 100.0;

            Serial.printf("  %.1f sec: %.2f mA (%.1f%%)\n",
                          (millis() - openStart) / 1000.0, mA, pct);

            if (mA >= MA_OPEN_MIN && openConfirmTime < 0) {
                openConfirmTime = (millis() - openStart) / 1000.0;
            }

            if (openConfirmTime >= 0 && (millis() - openStart) / 1000.0 > openConfirmTime + 2) {
                break;  // Confirmed + 2 extra seconds of reading
            }

            delay(1000);
        }
    } else {
        delay(VALVE_DELAY_DEFAULT_SEC * 1000);
    }

    float openSteadymA = ads1115Available ? readFeedbackmA() : -1;

    // Hold open for 5 seconds
    Serial.println("\nHolding OPEN for 5 seconds...");
    delay(5000);

    // CLOSE cycle
    Serial.println("\n--- CLOSE CYCLE ---");
    uint32_t closeStart = millis();
    digitalWrite(BLOWDOWN_RELAY_PIN, LOW);
    relayEnergized = false;

    float closeConfirmTime = -1;
    if (ads1115Available) {
        while (millis() - closeStart < VALVE_TIMEOUT_SEC * 1000) {
            float mA = readFeedbackmA();
            float pct = (mA - 4.0) / 16.0 * 100.0;
            if (pct < 0.0) pct = 0.0;
            if (pct > 100.0) pct = 100.0;

            Serial.printf("  %.1f sec: %.2f mA (%.1f%%)\n",
                          (millis() - closeStart) / 1000.0, mA, pct);

            if (mA <= MA_CLOSED_MAX && closeConfirmTime < 0) {
                closeConfirmTime = (millis() - closeStart) / 1000.0;
            }

            if (closeConfirmTime >= 0 && (millis() - closeStart) / 1000.0 > closeConfirmTime + 2) {
                break;
            }

            delay(1000);
        }
    } else {
        delay(VALVE_DELAY_DEFAULT_SEC * 1000);
    }

    float closeSteadymA = ads1115Available ? readFeedbackmA() : -1;

    // Results
    Serial.println();
    Serial.println("=== CYCLE TEST RESULTS ===");
    Serial.println();

    if (ads1115Available) {
        Serial.printf("Baseline (closed): %.2f mA\n", baseline_mA);
        Serial.printf("Steady-state open: %.2f mA\n", openSteadymA);
        Serial.printf("Steady-state closed: %.2f mA\n", closeSteadymA);
        Serial.println();

        if (openConfirmTime >= 0) {
            Serial.printf("Open time:  %.1f sec (feedback > %.1f mA)\n",
                          openConfirmTime, MA_OPEN_MIN);
        } else {
            Serial.println("Open time:  NOT CONFIRMED (feedback never reached threshold)");
        }

        if (closeConfirmTime >= 0) {
            Serial.printf("Close time: %.1f sec (feedback < %.1f mA)\n",
                          closeConfirmTime, MA_CLOSED_MAX);
        } else {
            Serial.println("Close time: NOT CONFIRMED (feedback never reached threshold)");
        }

        Serial.println();

        // Diagnose
        if (baseline_mA < MA_FAULT_LOW) {
            Serial.println("WARNING: Baseline feedback < 3 mA - check wiring!");
        }
        if (openSteadymA < MA_OPEN_MIN) {
            Serial.println("WARNING: Open feedback < 19 mA - valve may not be fully opening");
        }
        if (closeSteadymA > MA_CLOSED_MAX) {
            Serial.println("WARNING: Closed feedback > 5 mA - valve may not be fully closing");
        }
        if (openConfirmTime >= 0 && closeConfirmTime >= 0) {
            Serial.println("Valve cycle OK.");
        }
    } else {
        Serial.println("No ADS1115 - feedback not available.");
        Serial.printf("Used default %d sec delay for open/close.\n", VALVE_DELAY_DEFAULT_SEC);
    }

    Serial.println();
}

// ============================================================================
// FEEDBACK READING
// ============================================================================

void readFeedback() {
    Serial.println();

    if (!ads1115Available) {
        Serial.println("ADS1115 not available. Run I2C scan (press 'i') to check.");
        return;
    }

    Serial.println("=== FEEDBACK READING (10 samples) ===");
    Serial.println();

    float sum = 0;
    float minVal = 999;
    float maxVal = 0;

    for (int i = 0; i < 10; i++) {
        float mA = readFeedbackmA();
        sum += mA;
        if (mA < minVal) minVal = mA;
        if (mA > maxVal) maxVal = mA;

        int16_t raw = ads1115ReadChannel(FEEDBACK_ADC_CHANNEL);
        float voltage = raw * 0.000125;

        Serial.printf("  Sample %2d: %6.2f mA  (raw: %d, voltage: %.4f V)\n",
                      i + 1, mA, raw, voltage);
        delay(100);
    }

    float avg = sum / 10.0;
    float pct = (avg - 4.0) / 16.0 * 100.0;
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;

    Serial.println();
    Serial.printf("Average: %.2f mA (%.1f%% open)\n", avg, pct);
    Serial.printf("Range:   %.2f - %.2f mA (spread: %.2f mA)\n",
                  minVal, maxVal, maxVal - minVal);
    Serial.printf("Relay:   %s\n", relayEnergized ? "ENERGIZED (open cmd)" : "DE-ENERGIZED (close cmd)");
    Serial.println();

    // Interpret
    if (avg < MA_FAULT_LOW) {
        Serial.println("STATUS: FAULT - feedback below 3 mA (check wiring)");
    } else if (avg < MA_CLOSED_MAX) {
        Serial.println("STATUS: CLOSED");
    } else if (avg < MA_OPEN_MIN) {
        Serial.printf("STATUS: INTERMEDIATE (%.1f%% open)\n", pct);
    } else if (avg <= MA_FAULT_HIGH) {
        Serial.println("STATUS: OPEN");
    } else {
        Serial.println("STATUS: FAULT - feedback above 22 mA (sensor error)");
    }

    // Check mismatch
    if (relayEnergized && avg < MA_OPEN_MIN) {
        Serial.println("WARNING: Relay is energized (open) but feedback shows not fully open.");
        Serial.println("  - Valve may still be moving");
        Serial.println("  - Actuator may be stuck");
        Serial.println("  - Wiring issue between relay and actuator");
    } else if (!relayEnergized && avg > MA_CLOSED_MAX) {
        Serial.println("WARNING: Relay is de-energized (closed) but feedback shows not fully closed.");
        Serial.println("  - Valve may still be moving");
        Serial.println("  - Actuator may be stuck");
        Serial.println("  - Relay may be stuck energized");
    }
    Serial.println();
}

// ============================================================================
// CONTINUOUS MONITOR
// ============================================================================

void startContinuousMonitor() {
    if (!ads1115Available) {
        Serial.println("ADS1115 not available for continuous monitoring.");
        return;
    }

    Serial.println();
    Serial.println("=== CONTINUOUS FEEDBACK MONITOR ===");
    Serial.println("Reading every 500ms. Press any key to stop.");
    Serial.println();
    Serial.println("     mA     |  Open%  |  Status          | Relay");
    Serial.println("  ----------+---------+------------------+------------------");

    continuousMode = true;
    lastFeedbackTime = 0;  // Trigger immediate first read
}

// ============================================================================
// I2C SCAN
// ============================================================================

void scanI2C() {
    Serial.println();
    Serial.println("=== I2C BUS SCAN ===");
    Serial.printf("SDA: GPIO%d, SCL: GPIO%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println();

    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  Device found at 0x%02X", addr);
            if (addr == 0x48) Serial.print(" (ADS1115 default)");
            else if (addr == 0x49) Serial.print(" (ADS1115 ADDR->VDD)");
            else if (addr == 0x4A) Serial.print(" (ADS1115 ADDR->SDA)");
            else if (addr == 0x4B) Serial.print(" (ADS1115 ADDR->SCL)");
            else if (addr == 0x27 || addr == 0x3F) Serial.print(" (LCD)");
            Serial.println();
            found++;
        }
    }

    if (found == 0) {
        Serial.println("  No I2C devices found!");
        Serial.println("  Check SDA/SCL wiring and pull-up resistors (4.7k ohm).");
    } else {
        Serial.printf("\n%d device(s) found.\n", found);
    }

    // Update ADS1115 availability
    Wire.beginTransmission(ADS1115_I2C_ADDR);
    ads1115Available = (Wire.endTransmission() == 0);
    Serial.printf("\nADS1115 (0x%02X): %s\n", ADS1115_I2C_ADDR,
                  ads1115Available ? "AVAILABLE" : "NOT FOUND");
    Serial.println();
}

// ============================================================================
// STATUS AND HELP
// ============================================================================

void printStatus() {
    Serial.println();
    Serial.println("=== BLOWDOWN VALVE STATUS ===");
    Serial.printf("Relay pin:  GPIO%d\n", BLOWDOWN_RELAY_PIN);
    Serial.printf("Relay state: %s\n", relayEnergized ? "ENERGIZED (open cmd)" : "DE-ENERGIZED (close cmd)");
    Serial.printf("ADS1115:    %s\n", ads1115Available ? "Available" : "Not found");

    if (ads1115Available) {
        float mA = readFeedbackmA();
        float pct = (mA - 4.0) / 16.0 * 100.0;
        if (pct < 0.0) pct = 0.0;
        if (pct > 100.0) pct = 100.0;

        Serial.printf("Feedback:   %.2f mA (%.1f%% open)\n", mA, pct);

        const char* position;
        if (mA < MA_FAULT_LOW)       position = "FAULT (wiring)";
        else if (mA < MA_CLOSED_MAX) position = "CLOSED";
        else if (mA < MA_OPEN_MIN)   position = "INTERMEDIATE";
        else if (mA <= MA_FAULT_HIGH) position = "OPEN";
        else                          position = "FAULT (sensor)";
        Serial.printf("Position:   %s\n", position);
    }

    Serial.println();
    Serial.println("4-20mA Thresholds:");
    Serial.printf("  < %.1f mA: Wiring fault\n", MA_FAULT_LOW);
    Serial.printf("  < %.1f mA: Confirmed closed\n", MA_CLOSED_MAX);
    Serial.printf("  > %.1f mA: Confirmed open\n", MA_OPEN_MIN);
    Serial.printf("  > %.1f mA: Sensor fault\n", MA_FAULT_HIGH);
    Serial.println();
}

void printMenu() {
    Serial.println();
    Serial.println("=== BLOWDOWN VALVE TEST MENU ===");
    Serial.println();
    Serial.println("Valve Control:");
    Serial.println("  o - OPEN valve (energize relay, ~20mA)");
    Serial.println("  c - CLOSE valve (de-energize relay, ~4mA)");
    Serial.println("  t - Toggle valve (open <-> close)");
    Serial.println();
    Serial.println("Testing:");
    Serial.println("  T - Full cycle test (close -> open -> close with timing)");
    Serial.println("  r - Read feedback (10 samples with statistics)");
    Serial.println("  m - Continuous feedback monitor (press any key to stop)");
    Serial.println();
    Serial.println("Diagnostics:");
    Serial.println("  i - I2C bus scan (find ADS1115)");
    Serial.println("  p - Print status");
    Serial.println("  h - Show this menu");
    Serial.println();
    Serial.println("WIRING CHECK:");
    Serial.println("  1. GPIO4 -> MOSFET gate -> relay coil");
    Serial.println("  2. Relay NC -> 3.3k ohm -> 24VDC (4mA closed)");
    Serial.println("  3. Relay NO -> 680 ohm  -> 24VDC (20mA open)");
    Serial.println("  4. Actuator 4-20mA out -> 150 ohm -> ADS1115 CH0");
    Serial.println("  5. ADS1115 SDA/SCL -> GPIO21/GPIO22 (4.7k pull-ups)");
    Serial.println();
}

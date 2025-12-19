/**
 * @file test_water_meter.cpp
 * @brief Test program for water meter pulse input
 *
 * Tests:
 * - Pulse counting via interrupt
 * - Flow rate calculation
 * - Totalizer accumulation
 * - Debounce handling
 *
 * Hardware:
 * - Water meter with pulse output (1 pulse per gallon)
 * - Dry contact or open collector output
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Simulate pulses or connect to water meter
 */

#include <Arduino.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void printMenu();
void processCommand(char cmd);
void calculateFlowRates();
void displayStatus();
void simulatePulses(int meter, int count);
void testFlowSimulation();
void resetCounters();
void saveToTotalizer();
void testTotalizer();
void testInputState();

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

#define WATER_METER1_PIN    36  // Water meter 1 input (ADC1 - interrupt capable)
#define WATER_METER2_PIN    39  // Water meter 2 input (ADC1 - interrupt capable)
#define TEST_BUTTON_PIN     0   // Boot button for manual pulse simulation

// ============================================================================
// CONFIGURATION
// ============================================================================

#define PULSES_PER_GALLON   1       // Your meter: 1 pulse = 1 gallon
#define DEBOUNCE_MS         50      // Debounce time in milliseconds
#define FLOW_CALC_INTERVAL  5000    // Calculate flow rate every 5 seconds

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

volatile uint32_t pulseCount1 = 0;
volatile uint32_t pulseCount2 = 0;
volatile uint32_t lastPulseTime1 = 0;
volatile uint32_t lastPulseTime2 = 0;

uint32_t totalizer1 = 0;
uint32_t totalizer2 = 0;
uint32_t lastFlowCalcTime = 0;
uint32_t lastPulseCount1 = 0;
uint32_t lastPulseCount2 = 0;

float flowRate1 = 0;  // GPM
float flowRate2 = 0;

bool continuousMode = false;

// ============================================================================
// INTERRUPT SERVICE ROUTINES
// ============================================================================

void IRAM_ATTR onMeter1Pulse() {
    uint32_t now = millis();
    if (now - lastPulseTime1 > DEBOUNCE_MS) {
        pulseCount1++;
        lastPulseTime1 = now;
    }
}

void IRAM_ATTR onMeter2Pulse() {
    uint32_t now = millis();
    if (now - lastPulseTime2 > DEBOUNCE_MS) {
        pulseCount2++;
        lastPulseTime2 = now;
    }
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  WATER METER TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();

    // Configure water meter inputs with pull-up
    pinMode(WATER_METER1_PIN, INPUT_PULLUP);
    pinMode(WATER_METER2_PIN, INPUT_PULLUP);
    pinMode(TEST_BUTTON_PIN, INPUT_PULLUP);

    // Attach interrupts
    attachInterrupt(digitalPinToInterrupt(WATER_METER1_PIN), onMeter1Pulse, FALLING);
    attachInterrupt(digitalPinToInterrupt(WATER_METER2_PIN), onMeter2Pulse, FALLING);

    Serial.println("Water meter inputs configured:");
    Serial.printf("  Meter 1: GPIO%d\n", WATER_METER1_PIN);
    Serial.printf("  Meter 2: GPIO%d\n", WATER_METER2_PIN);
    Serial.printf("  Pulses per gallon: %d\n", PULSES_PER_GALLON);
    Serial.printf("  Debounce: %d ms\n", DEBOUNCE_MS);
    Serial.println();
    Serial.println("Tip: Press BOOT button (GPIO0) to simulate a pulse on Meter 1");
    Serial.println();

    printMenu();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    static bool lastButtonState = HIGH;

    // Process serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        processCommand(cmd);
    }

    // Check boot button for pulse simulation
    bool buttonState = digitalRead(TEST_BUTTON_PIN);
    if (buttonState == LOW && lastButtonState == HIGH) {
        // Button pressed - simulate pulse
        pulseCount1++;
        Serial.println("* Manual pulse simulated on Meter 1");
    }
    lastButtonState = buttonState;

    // Calculate flow rates periodically
    uint32_t now = millis();
    if (now - lastFlowCalcTime >= FLOW_CALC_INTERVAL) {
        calculateFlowRates();
        lastFlowCalcTime = now;

        if (continuousMode) {
            displayStatus();
        }
    }

    // Small delay
    delay(10);
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(char cmd) {
    switch (cmd) {
        case 'r':
        case 'R':
            displayStatus();
            break;

        case 'c':
            continuousMode = !continuousMode;
            Serial.printf("Continuous mode: %s\n", continuousMode ? "ON" : "OFF");
            break;

        case '1':
            simulatePulses(1, 10);  // 10 pulses on meter 1
            break;

        case '2':
            simulatePulses(2, 10);  // 10 pulses on meter 2
            break;

        case 'f':
            testFlowSimulation();
            break;

        case 'z':
            resetCounters();
            break;

        case 't':
            testTotalizer();
            break;

        case 'i':
            testInputState();
            break;

        case 's':
            saveToTotalizer();
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
// METER FUNCTIONS
// ============================================================================

void calculateFlowRates() {
    // Get current counts
    noInterrupts();
    uint32_t count1 = pulseCount1;
    uint32_t count2 = pulseCount2;
    interrupts();

    // Calculate pulses since last check
    uint32_t delta1 = count1 - lastPulseCount1;
    uint32_t delta2 = count2 - lastPulseCount2;

    // Calculate flow rate in GPM
    float interval_min = FLOW_CALC_INTERVAL / 60000.0;
    flowRate1 = (delta1 / (float)PULSES_PER_GALLON) / interval_min;
    flowRate2 = (delta2 / (float)PULSES_PER_GALLON) / interval_min;

    // Update last counts
    lastPulseCount1 = count1;
    lastPulseCount2 = count2;
}

void displayStatus() {
    // Get current counts atomically
    noInterrupts();
    uint32_t count1 = pulseCount1;
    uint32_t count2 = pulseCount2;
    interrupts();

    float volume1 = count1 / (float)PULSES_PER_GALLON;
    float volume2 = count2 / (float)PULSES_PER_GALLON;

    Serial.println();
    Serial.println("=== WATER METER STATUS ===");
    Serial.println();
    Serial.println("Meter 1 (Makeup Water):");
    Serial.printf("  Pulses:     %lu\n", count1);
    Serial.printf("  Volume:     %.2f gallons\n", volume1);
    Serial.printf("  Flow rate:  %.2f GPM\n", flowRate1);
    Serial.printf("  Totalizer:  %lu gallons\n", totalizer1 + (uint32_t)volume1);
    Serial.println();
    Serial.println("Meter 2:");
    Serial.printf("  Pulses:     %lu\n", count2);
    Serial.printf("  Volume:     %.2f gallons\n", volume2);
    Serial.printf("  Flow rate:  %.2f GPM\n", flowRate2);
    Serial.printf("  Totalizer:  %lu gallons\n", totalizer2 + (uint32_t)volume2);
    Serial.println();
    Serial.printf("Combined flow: %.2f GPM\n", flowRate1 + flowRate2);
    Serial.println();
}

void simulatePulses(int meter, int count) {
    Serial.printf("Simulating %d pulses on Meter %d...\n", count, meter);

    for (int i = 0; i < count; i++) {
        if (meter == 1) {
            pulseCount1++;
        } else {
            pulseCount2++;
        }
        Serial.printf("  Pulse %d\n", i + 1);
        delay(100);  // 100ms between pulses
    }

    Serial.println("Done!");
    displayStatus();
}

void testFlowSimulation() {
    Serial.println();
    Serial.println("=== FLOW SIMULATION TEST ===");
    Serial.println("Simulating 2 GPM flow for 30 seconds...");
    Serial.println("(40 pulses at 1 pulse/gal = 2 GPM)");
    Serial.println();

    // 2 GPM = 2 gallons per minute = 2 pulses per minute at 1 pulse/gal
    // 30 seconds = 1 pulse every 1.5 seconds for 2 GPM
    // Let's simulate 2 GPM for 30 seconds = 1 gallon = 1 pulse every 0.75 sec

    uint32_t startTime = millis();
    int pulsesGenerated = 0;

    while (millis() - startTime < 30000) {
        pulseCount1++;
        pulsesGenerated++;

        Serial.printf("\rPulses: %d  Elapsed: %.1f sec",
                      pulsesGenerated, (millis() - startTime) / 1000.0);

        delay(750);  // ~2 GPM simulation
    }

    Serial.println();
    Serial.println("Simulation complete!");
    calculateFlowRates();
    displayStatus();
}

void resetCounters() {
    noInterrupts();
    pulseCount1 = 0;
    pulseCount2 = 0;
    lastPulseCount1 = 0;
    lastPulseCount2 = 0;
    interrupts();

    flowRate1 = 0;
    flowRate2 = 0;

    Serial.println("Pulse counters reset to zero");
}

void saveToTotalizer() {
    noInterrupts();
    uint32_t count1 = pulseCount1;
    uint32_t count2 = pulseCount2;
    pulseCount1 = 0;
    pulseCount2 = 0;
    interrupts();

    totalizer1 += count1 / PULSES_PER_GALLON;
    totalizer2 += count2 / PULSES_PER_GALLON;

    Serial.println("Counters saved to totalizer and reset");
    Serial.printf("  Totalizer 1: %lu gallons\n", totalizer1);
    Serial.printf("  Totalizer 2: %lu gallons\n", totalizer2);
}

void testTotalizer() {
    Serial.println();
    Serial.println("=== TOTALIZER TEST ===");
    Serial.printf("Current totalizer 1: %lu gallons\n", totalizer1);
    Serial.printf("Current totalizer 2: %lu gallons\n", totalizer2);
    Serial.println();
    Serial.println("Adding 100 gallons to each totalizer...");

    totalizer1 += 100;
    totalizer2 += 100;

    Serial.printf("New totalizer 1: %lu gallons\n", totalizer1);
    Serial.printf("New totalizer 2: %lu gallons\n", totalizer2);
    Serial.println();
}

void testInputState() {
    Serial.println();
    Serial.println("=== INPUT STATE TEST ===");
    Serial.println("Reading input pins for 10 seconds...");
    Serial.println("(Connect/disconnect meter to see changes)");
    Serial.println();

    uint32_t startTime = millis();
    int lastState1 = -1, lastState2 = -1;

    while (millis() - startTime < 10000) {
        int state1 = digitalRead(WATER_METER1_PIN);
        int state2 = digitalRead(WATER_METER2_PIN);

        if (state1 != lastState1 || state2 != lastState2) {
            Serial.printf("  Meter 1: %s    Meter 2: %s\n",
                          state1 ? "HIGH" : "LOW",
                          state2 ? "HIGH" : "LOW");
            lastState1 = state1;
            lastState2 = state2;
        }

        delay(10);
    }

    Serial.println();
    Serial.println("Test complete");
    Serial.println();
}

// ============================================================================
// HELP
// ============================================================================

void printMenu() {
    Serial.println();
    Serial.println("=== WATER METER TEST MENU ===");
    Serial.println();
    Serial.println("Status:");
    Serial.println("  r - Read current status");
    Serial.println("  c - Toggle continuous mode");
    Serial.println();
    Serial.println("Simulation:");
    Serial.println("  1 - Simulate 10 pulses on Meter 1");
    Serial.println("  2 - Simulate 10 pulses on Meter 2");
    Serial.println("  f - Flow simulation (2 GPM for 30 sec)");
    Serial.println("  (BOOT button also simulates 1 pulse)");
    Serial.println();
    Serial.println("Totalizer:");
    Serial.println("  z - Reset pulse counters to zero");
    Serial.println("  s - Save counters to totalizer");
    Serial.println("  t - Test totalizer add");
    Serial.println();
    Serial.println("Diagnostics:");
    Serial.println("  i - Test input pin states");
    Serial.println();
    Serial.println("Other:");
    Serial.println("  h - Show this menu");
    Serial.println();
}

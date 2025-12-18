/**
 * @file test_conductivity_sensor.ino
 * @brief Test program for conductivity and temperature sensors
 *
 * Tests:
 * - Sensorex CS675HTTC conductivity probe
 * - Pt1000 RTD temperature sensor
 * - AC excitation for conductivity measurement
 * - Temperature compensation
 * - Calibration routines
 *
 * Hardware:
 * - Conductivity probe connected to excitation/sense pins
 * - Pt1000 RTD with voltage divider circuit
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Submerge probe in test solution
 * - Follow menu prompts
 */

#include <Arduino.h>

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

#define COND_EXCITE_PIN     4   // Conductivity excitation output
#define COND_SENSE_PIN      34  // Conductivity sense input (ADC)
#define TEMP_SENSE_PIN      35  // Temperature sense input (ADC)

// ============================================================================
// CONFIGURATION
// ============================================================================

// ADC settings
#define ADC_RESOLUTION      12
#define ADC_MAX_VALUE       4095
#define ADC_VREF            3.3

// Conductivity settings
#define EXCITE_FREQUENCY_HZ 1000    // AC excitation frequency
#define SAMPLES_PER_READ    100     // ADC samples to average
#define CELL_CONSTANT       1.0     // K factor (adjust per probe)
#define RANGE_MAX_US        10000   // Maximum measurement range

// Temperature settings (Pt1000)
#define TEMP_R_REF          1000.0  // Reference resistance at 0°C
#define TEMP_R_DIVIDER      1000.0  // Voltage divider resistor value
#define TEMP_ALPHA          0.00385 // Temperature coefficient

// Temperature compensation
#define TEMP_COMP_COEFF     0.02    // 2% per °C
#define TEMP_REF_C          25.0    // Reference temperature

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

float lastConductivity = 0;
float lastTemperature = 0;
float calibrationFactor = 1.0;
bool continuousMode = false;

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  CONDUCTIVITY SENSOR TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();

    // Configure pins
    pinMode(COND_EXCITE_PIN, OUTPUT);
    digitalWrite(COND_EXCITE_PIN, LOW);

    // Configure ADC
    analogReadResolution(ADC_RESOLUTION);
    analogSetAttenuation(ADC_11db);  // Full scale 0-3.3V

    Serial.println("Sensor pins configured:");
    Serial.printf("  Excitation pin: GPIO%d\n", COND_EXCITE_PIN);
    Serial.printf("  Conductivity sense: GPIO%d (ADC)\n", COND_SENSE_PIN);
    Serial.printf("  Temperature sense: GPIO%d (ADC)\n", TEMP_SENSE_PIN);
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

    // Continuous mode
    if (continuousMode) {
        readAndDisplaySensors();
        delay(1000);
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(char cmd) {
    switch (cmd) {
        case 'r':
        case 'R':
            readAndDisplaySensors();
            break;

        case 'c':
            continuousMode = !continuousMode;
            Serial.printf("Continuous mode: %s\n", continuousMode ? "ON" : "OFF");
            break;

        case 't':
            testTemperatureSensor();
            break;

        case 'e':
            testExcitation();
            break;

        case 'a':
            testADCRaw();
            break;

        case '1':
            calibrateWithStandard(1413);  // 1413 µS/cm standard
            break;

        case '2':
            calibrateWithStandard(2764);  // 2764 µS/cm standard
            break;

        case '3':
            calibrateWithStandard(12880); // 12880 µS/cm standard
            break;

        case 'f':
            resetCalibration();
            break;

        case 's':
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
// SENSOR READING FUNCTIONS
// ============================================================================

void readAndDisplaySensors() {
    // Read temperature first (for compensation)
    float temperature = readTemperature();
    lastTemperature = temperature;

    // Read conductivity
    float rawConductivity = readConductivityRaw();

    // Apply temperature compensation
    float compensated = temperatureCompensate(rawConductivity, temperature);

    // Apply calibration
    float calibrated = compensated * calibrationFactor;
    lastConductivity = calibrated;

    // Display results
    Serial.println();
    Serial.println("--- Sensor Reading ---");
    Serial.printf("Temperature:    %.2f °C\n", temperature);
    Serial.printf("Conductivity:\n");
    Serial.printf("  Raw:          %.1f µS/cm\n", rawConductivity);
    Serial.printf("  Compensated:  %.1f µS/cm (@ 25°C)\n", compensated);
    Serial.printf("  Calibrated:   %.1f µS/cm\n", calibrated);
    Serial.printf("  TDS (approx): %.0f ppm\n", calibrated * 0.64);
    Serial.println();
}

float readConductivityRaw() {
    long sumHigh = 0;
    long sumLow = 0;

    // AC excitation - take readings on both polarities
    for (int i = 0; i < SAMPLES_PER_READ / 2; i++) {
        // Positive excitation
        digitalWrite(COND_EXCITE_PIN, HIGH);
        delayMicroseconds(500000 / EXCITE_FREQUENCY_HZ);  // Half period
        sumHigh += analogRead(COND_SENSE_PIN);

        // Negative excitation
        digitalWrite(COND_EXCITE_PIN, LOW);
        delayMicroseconds(500000 / EXCITE_FREQUENCY_HZ);
        sumLow += analogRead(COND_SENSE_PIN);
    }

    // Turn off excitation
    digitalWrite(COND_EXCITE_PIN, LOW);

    // Calculate average differential
    float avgHigh = (float)sumHigh / (SAMPLES_PER_READ / 2);
    float avgLow = (float)sumLow / (SAMPLES_PER_READ / 2);
    float adcDiff = avgHigh - avgLow;

    // Convert to voltage
    float voltage = (adcDiff / ADC_MAX_VALUE) * ADC_VREF;

    // Convert to conductivity (simplified - adjust formula per your circuit)
    // This formula assumes a specific probe circuit design
    float conductivity = voltage * RANGE_MAX_US / ADC_VREF * CELL_CONSTANT;

    return conductivity;
}

float readTemperature() {
    long sum = 0;

    // Average multiple readings
    for (int i = 0; i < SAMPLES_PER_READ; i++) {
        sum += analogRead(TEMP_SENSE_PIN);
        delayMicroseconds(100);
    }

    float adcAvg = (float)sum / SAMPLES_PER_READ;

    // Convert to voltage
    float voltage = (adcAvg / ADC_MAX_VALUE) * ADC_VREF;

    // Calculate resistance (voltage divider)
    // Vout = Vin * Rpt / (Rpt + Rdiv)
    // Rpt = Vout * Rdiv / (Vin - Vout)
    float resistance = voltage * TEMP_R_DIVIDER / (ADC_VREF - voltage);

    // Convert resistance to temperature (Pt1000)
    // R = R0 * (1 + alpha * T)
    // T = (R/R0 - 1) / alpha
    float temperature = (resistance / TEMP_R_REF - 1.0) / TEMP_ALPHA;

    return temperature;
}

float temperatureCompensate(float conductivity, float temperature) {
    // Compensate to reference temperature (25°C)
    float compensation = 1.0 + TEMP_COMP_COEFF * (temperature - TEMP_REF_C);
    return conductivity / compensation;
}

// ============================================================================
// TEST FUNCTIONS
// ============================================================================

void testTemperatureSensor() {
    Serial.println();
    Serial.println("=== TEMPERATURE SENSOR TEST ===");
    Serial.println("Reading temperature sensor 10 times...");
    Serial.println();

    for (int i = 0; i < 10; i++) {
        int rawADC = analogRead(TEMP_SENSE_PIN);
        float voltage = (rawADC / (float)ADC_MAX_VALUE) * ADC_VREF;
        float resistance = voltage * TEMP_R_DIVIDER / (ADC_VREF - voltage);
        float temperature = (resistance / TEMP_R_REF - 1.0) / TEMP_ALPHA;

        Serial.printf("  %2d: ADC=%4d  V=%.3f  R=%.1fΩ  T=%.2f°C\n",
                      i + 1, rawADC, voltage, resistance, temperature);
        delay(500);
    }

    Serial.println();
    Serial.println("Expected values:");
    Serial.println("  Room temp (20-25°C): R ≈ 1077-1097Ω");
    Serial.println("  Boiler temp (40-50°C): R ≈ 1155-1194Ω");
    Serial.println();
}

void testExcitation() {
    Serial.println();
    Serial.println("=== EXCITATION TEST ===");
    Serial.println("Testing AC excitation signal...");
    Serial.println();

    Serial.println("Excitation HIGH for 2 seconds...");
    digitalWrite(COND_EXCITE_PIN, HIGH);
    delay(2000);

    Serial.printf("  ADC reading (HIGH): %d\n", analogRead(COND_SENSE_PIN));

    Serial.println("Excitation LOW for 2 seconds...");
    digitalWrite(COND_EXCITE_PIN, LOW);
    delay(2000);

    Serial.printf("  ADC reading (LOW): %d\n", analogRead(COND_SENSE_PIN));

    Serial.println();
    Serial.println("Toggling excitation rapidly (check with scope)...");
    for (int i = 0; i < 1000; i++) {
        digitalWrite(COND_EXCITE_PIN, HIGH);
        delayMicroseconds(500);
        digitalWrite(COND_EXCITE_PIN, LOW);
        delayMicroseconds(500);
    }

    Serial.println("Done. Excitation OFF.");
    Serial.println();
}

void testADCRaw() {
    Serial.println();
    Serial.println("=== RAW ADC TEST ===");
    Serial.println("Reading raw ADC values...");
    Serial.println();

    for (int i = 0; i < 10; i++) {
        int condADC = analogRead(COND_SENSE_PIN);
        int tempADC = analogRead(TEMP_SENSE_PIN);

        Serial.printf("  Conductivity ADC: %4d (%.3fV)    Temperature ADC: %4d (%.3fV)\n",
                      condADC, condADC * ADC_VREF / ADC_MAX_VALUE,
                      tempADC, tempADC * ADC_VREF / ADC_MAX_VALUE);
        delay(500);
    }

    Serial.println();
}

// ============================================================================
// CALIBRATION FUNCTIONS
// ============================================================================

void calibrateWithStandard(int standardValue) {
    Serial.println();
    Serial.printf("=== CALIBRATION WITH %d µS/cm STANDARD ===\n", standardValue);
    Serial.println();
    Serial.println("Instructions:");
    Serial.println("1. Rinse probe with distilled water");
    Serial.println("2. Submerge probe in calibration solution");
    Serial.println("3. Wait for reading to stabilize (30 seconds)");
    Serial.println("4. Press any key to calibrate...");
    Serial.println();

    // Wait for keypress
    while (!Serial.available()) {
        // Show live readings while waiting
        float raw = readConductivityRaw();
        float temp = readTemperature();
        float comp = temperatureCompensate(raw, temp);

        Serial.printf("\r  Current reading: %.1f µS/cm (raw)  %.1f µS/cm (compensated)  %.1f°C   ",
                      raw, comp, temp);
        delay(500);
    }
    Serial.read();  // Clear the keypress

    Serial.println();
    Serial.println("Calibrating...");

    // Take average of multiple readings
    float sumComp = 0;
    for (int i = 0; i < 10; i++) {
        float raw = readConductivityRaw();
        float temp = readTemperature();
        sumComp += temperatureCompensate(raw, temp);
        delay(200);
    }

    float avgReading = sumComp / 10;

    // Calculate calibration factor
    calibrationFactor = (float)standardValue / avgReading;

    Serial.println();
    Serial.printf("Average reading: %.1f µS/cm\n", avgReading);
    Serial.printf("Standard value:  %d µS/cm\n", standardValue);
    Serial.printf("Calibration factor: %.4f\n", calibrationFactor);
    Serial.println();
    Serial.println("Calibration complete!");
    Serial.println("Test by taking a new reading (press 'r')");
    Serial.println();
}

void resetCalibration() {
    calibrationFactor = 1.0;
    Serial.println("Calibration reset to factory default (1.0)");
}

// ============================================================================
// STATUS AND HELP
// ============================================================================

void printStatus() {
    Serial.println();
    Serial.println("=== SENSOR STATUS ===");
    Serial.printf("Last conductivity: %.1f µS/cm\n", lastConductivity);
    Serial.printf("Last temperature:  %.2f °C\n", lastTemperature);
    Serial.printf("Calibration factor: %.4f\n", calibrationFactor);
    Serial.printf("Continuous mode: %s\n", continuousMode ? "ON" : "OFF");
    Serial.println();
    Serial.println("Configuration:");
    Serial.printf("  Cell constant:   %.2f\n", CELL_CONSTANT);
    Serial.printf("  Range max:       %d µS/cm\n", RANGE_MAX_US);
    Serial.printf("  Temp comp coeff: %.2f %%/°C\n", TEMP_COMP_COEFF * 100);
    Serial.printf("  Reference temp:  %.1f °C\n", TEMP_REF_C);
    Serial.println();
}

void printMenu() {
    Serial.println();
    Serial.println("=== CONDUCTIVITY SENSOR TEST MENU ===");
    Serial.println();
    Serial.println("Readings:");
    Serial.println("  r - Read sensors (single reading)");
    Serial.println("  c - Toggle continuous mode");
    Serial.println();
    Serial.println("Diagnostics:");
    Serial.println("  t - Test temperature sensor");
    Serial.println("  e - Test excitation circuit");
    Serial.println("  a - Test raw ADC values");
    Serial.println();
    Serial.println("Calibration:");
    Serial.println("  1 - Calibrate with 1413 µS/cm standard");
    Serial.println("  2 - Calibrate with 2764 µS/cm standard");
    Serial.println("  3 - Calibrate with 12880 µS/cm standard");
    Serial.println("  f - Reset calibration to factory");
    Serial.println();
    Serial.println("Other:");
    Serial.println("  s - Print status");
    Serial.println("  h - Show this menu");
    Serial.println();
}

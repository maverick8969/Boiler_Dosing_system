/**
 * @file test_fuzzy_logic.cpp
 * @brief Test program for fuzzy logic controller
 *
 * Tests:
 * - Membership function calculations
 * - Rule evaluation
 * - Defuzzification
 * - Input/output relationships
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Enter test values to see fuzzy outputs
 */

#include <Arduino.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void printMenu();
void printConfig();
void processCommand(String cmd);
void setTDS(float value);
void setAlkalinity(float value);
void setSulfite(float value);
void setPH(float value);
void clearInputs();
void evaluateAndDisplay();
void evaluateFuzzy();
void printInputs();
void printOutputs();
void runTestScenarios();
void testMembership();

// ============================================================================
// SIMPLIFIED FUZZY LOGIC IMPLEMENTATION FOR TESTING
// ============================================================================

// Linguistic terms
enum Term { VERY_LOW = 0, LOW, MEDIUM, HIGH, VERY_HIGH, TERM_COUNT };
const char* termNames[] = {"VERY_LOW", "LOW", "MEDIUM", "HIGH", "VERY_HIGH"};

// Input ranges and setpoints
struct FuzzyConfig {
    float tds_setpoint = 2500;      // ppm
    float tds_range = 1500;         // +/- from setpoint

    float alk_setpoint = 300;       // ppm CaCO3
    float alk_range = 150;

    float sulfite_setpoint = 30;    // ppm SO3
    float sulfite_range = 20;

    float ph_setpoint = 11.0;
    float ph_range = 1.0;
};

struct FuzzyInputs {
    float tds = 0;
    float alkalinity = 0;
    float sulfite = 0;
    float ph = 0;
    bool tds_valid = false;
    bool alk_valid = false;
    bool sulfite_valid = false;
    bool ph_valid = false;
};

struct FuzzyOutputs {
    float blowdown = 0;     // 0-100%
    float caustic = 0;      // 0-100%
    float sulfite_dose = 0; // 0-100%
    float acid = 0;         // 0-100%
    int active_rules = 0;
    String confidence = "LOW";
};

FuzzyConfig config;
FuzzyInputs inputs;
FuzzyOutputs outputs;

// ============================================================================
// MEMBERSHIP FUNCTIONS
// ============================================================================

// Triangular membership function
float triangular(float x, float a, float b, float c) {
    if (x <= a || x >= c) return 0.0;
    if (x <= b) return (x - a) / (b - a);
    return (c - x) / (c - b);
}

// Trapezoidal membership function (for edges)
float trapezoidal(float x, float a, float b, float c, float d) {
    if (x <= a || x >= d) return 0.0;
    if (x >= b && x <= c) return 1.0;
    if (x < b) return (x - a) / (b - a);
    return (d - x) / (d - c);
}

// Calculate membership for a value given setpoint and range
void calculateMembership(float value, float setpoint, float range, float* memberships) {
    float veryLowCenter = setpoint - range;
    float lowCenter = setpoint - range * 0.5;
    float medCenter = setpoint;
    float highCenter = setpoint + range * 0.5;
    float veryHighCenter = setpoint + range;

    float spread = range * 0.4;

    // Very Low - left shoulder
    memberships[VERY_LOW] = trapezoidal(value,
        veryLowCenter - spread * 2, veryLowCenter - spread,
        veryLowCenter, veryLowCenter + spread);

    // Low
    memberships[LOW] = triangular(value,
        lowCenter - spread, lowCenter, lowCenter + spread);

    // Medium
    memberships[MEDIUM] = triangular(value,
        medCenter - spread, medCenter, medCenter + spread);

    // High
    memberships[HIGH] = triangular(value,
        highCenter - spread, highCenter, highCenter + spread);

    // Very High - right shoulder
    memberships[VERY_HIGH] = trapezoidal(value,
        veryHighCenter - spread, veryHighCenter,
        veryHighCenter + spread, veryHighCenter + spread * 2);
}

// ============================================================================
// FUZZY INFERENCE
// ============================================================================

void evaluateFuzzy() {
    // Calculate memberships for each input
    float tds_mem[TERM_COUNT] = {0};
    float alk_mem[TERM_COUNT] = {0};
    float sulf_mem[TERM_COUNT] = {0};
    float ph_mem[TERM_COUNT] = {0};

    if (inputs.tds_valid) {
        calculateMembership(inputs.tds, config.tds_setpoint, config.tds_range, tds_mem);
    }
    if (inputs.alk_valid) {
        calculateMembership(inputs.alkalinity, config.alk_setpoint, config.alk_range, alk_mem);
    }
    if (inputs.sulfite_valid) {
        calculateMembership(inputs.sulfite, config.sulfite_setpoint, config.sulfite_range, sulf_mem);
    }
    if (inputs.ph_valid) {
        calculateMembership(inputs.ph, config.ph_setpoint, config.ph_range, ph_mem);
    }

    // Initialize outputs
    float blowdown_accum = 0;
    float caustic_accum = 0;
    float sulfite_accum = 0;
    float acid_accum = 0;
    float weight_sum = 0;
    int rulesActivated = 0;

    // Rule evaluation (simplified rule base)

    // Rule 1: IF TDS is VERY_HIGH THEN Blowdown is HIGH
    if (inputs.tds_valid && tds_mem[VERY_HIGH] > 0) {
        float strength = tds_mem[VERY_HIGH];
        blowdown_accum += strength * 90;
        weight_sum += strength;
        rulesActivated++;
    }

    // Rule 2: IF TDS is HIGH THEN Blowdown is MEDIUM
    if (inputs.tds_valid && tds_mem[HIGH] > 0) {
        float strength = tds_mem[HIGH];
        blowdown_accum += strength * 60;
        weight_sum += strength;
        rulesActivated++;
    }

    // Rule 3: IF TDS is MEDIUM THEN Blowdown is LOW
    if (inputs.tds_valid && tds_mem[MEDIUM] > 0) {
        float strength = tds_mem[MEDIUM];
        blowdown_accum += strength * 20;
        weight_sum += strength;
        rulesActivated++;
    }

    // Rule 4: IF Alkalinity is LOW THEN Caustic is HIGH
    if (inputs.alk_valid && alk_mem[LOW] > 0) {
        float strength = alk_mem[LOW];
        caustic_accum += strength * 80;
        weight_sum += strength;
        rulesActivated++;
    }

    // Rule 5: IF Alkalinity is VERY_LOW THEN Caustic is VERY_HIGH
    if (inputs.alk_valid && alk_mem[VERY_LOW] > 0) {
        float strength = alk_mem[VERY_LOW];
        caustic_accum += strength * 100;
        weight_sum += strength;
        rulesActivated++;
    }

    // Rule 6: IF Alkalinity is HIGH THEN Acid is MEDIUM
    if (inputs.alk_valid && alk_mem[HIGH] > 0) {
        float strength = alk_mem[HIGH];
        acid_accum += strength * 50;
        weight_sum += strength;
        rulesActivated++;
    }

    // Rule 7: IF Sulfite is LOW THEN SulfiteDose is HIGH
    if (inputs.sulfite_valid && sulf_mem[LOW] > 0) {
        float strength = sulf_mem[LOW];
        sulfite_accum += strength * 80;
        weight_sum += strength;
        rulesActivated++;
    }

    // Rule 8: IF Sulfite is VERY_LOW THEN SulfiteDose is VERY_HIGH
    if (inputs.sulfite_valid && sulf_mem[VERY_LOW] > 0) {
        float strength = sulf_mem[VERY_LOW];
        sulfite_accum += strength * 100;
        weight_sum += strength;
        rulesActivated++;
    }

    // Rule 9: IF Sulfite is HIGH THEN SulfiteDose is LOW
    if (inputs.sulfite_valid && sulf_mem[HIGH] > 0) {
        float strength = sulf_mem[HIGH];
        sulfite_accum += strength * 20;
        weight_sum += strength;
        rulesActivated++;
    }

    // Rule 10: IF pH is LOW THEN Caustic is HIGH
    if (inputs.ph_valid && ph_mem[LOW] > 0) {
        float strength = ph_mem[LOW];
        caustic_accum += strength * 70;
        weight_sum += strength;
        rulesActivated++;
    }

    // Rule 11: IF pH is HIGH THEN Acid is MEDIUM
    if (inputs.ph_valid && ph_mem[HIGH] > 0) {
        float strength = ph_mem[HIGH];
        acid_accum += strength * 40;
        weight_sum += strength;
        rulesActivated++;
    }

    // Defuzzification (weighted average)
    if (weight_sum > 0) {
        outputs.blowdown = constrain(blowdown_accum / weight_sum, 0, 100);
        outputs.caustic = constrain(caustic_accum / weight_sum, 0, 100);
        outputs.sulfite_dose = constrain(sulfite_accum / weight_sum, 0, 100);
        outputs.acid = constrain(acid_accum / weight_sum, 0, 100);
    } else {
        outputs.blowdown = 0;
        outputs.caustic = 0;
        outputs.sulfite_dose = 0;
        outputs.acid = 0;
    }

    outputs.active_rules = rulesActivated;

    // Determine confidence
    int validInputs = (inputs.tds_valid ? 1 : 0) +
                      (inputs.alk_valid ? 1 : 0) +
                      (inputs.sulfite_valid ? 1 : 0) +
                      (inputs.ph_valid ? 1 : 0);

    if (validInputs == 4) outputs.confidence = "HIGH";
    else if (validInputs >= 2) outputs.confidence = "MEDIUM";
    else outputs.confidence = "LOW";
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  FUZZY LOGIC TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();

    printConfig();
    printMenu();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        processCommand(cmd);
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(String cmd) {
    if (cmd.length() == 0) return;

    char c = cmd.charAt(0);

    switch (c) {
        case 't':
        case 'T':
            setTDS(cmd.substring(1).toFloat());
            break;

        case 'a':
        case 'A':
            setAlkalinity(cmd.substring(1).toFloat());
            break;

        case 's':
        case 'S':
            setSulfite(cmd.substring(1).toFloat());
            break;

        case 'p':
        case 'P':
            setPH(cmd.substring(1).toFloat());
            break;

        case 'e':
        case 'E':
            evaluateAndDisplay();
            break;

        case 'c':
            clearInputs();
            break;

        case 'r':
            runTestScenarios();
            break;

        case 'm':
            testMembership();
            break;

        case 'i':
            printInputs();
            break;

        case 'o':
            printOutputs();
            break;

        case 'h':
        case '?':
            printMenu();
            break;

        default:
            Serial.println("Unknown command. Type 'h' for help.");
            break;
    }
}

// ============================================================================
// INPUT FUNCTIONS
// ============================================================================

void setTDS(float value) {
    if (value > 0) {
        inputs.tds = value;
        inputs.tds_valid = true;
        Serial.printf("TDS set to %.0f ppm\n", value);
    } else {
        inputs.tds_valid = false;
        Serial.println("TDS cleared");
    }
}

void setAlkalinity(float value) {
    if (value > 0) {
        inputs.alkalinity = value;
        inputs.alk_valid = true;
        Serial.printf("Alkalinity set to %.0f ppm\n", value);
    } else {
        inputs.alk_valid = false;
        Serial.println("Alkalinity cleared");
    }
}

void setSulfite(float value) {
    if (value > 0) {
        inputs.sulfite = value;
        inputs.sulfite_valid = true;
        Serial.printf("Sulfite set to %.0f ppm\n", value);
    } else {
        inputs.sulfite_valid = false;
        Serial.println("Sulfite cleared");
    }
}

void setPH(float value) {
    if (value > 0) {
        inputs.ph = value;
        inputs.ph_valid = true;
        Serial.printf("pH set to %.1f\n", value);
    } else {
        inputs.ph_valid = false;
        Serial.println("pH cleared");
    }
}

void clearInputs() {
    inputs.tds_valid = false;
    inputs.alk_valid = false;
    inputs.sulfite_valid = false;
    inputs.ph_valid = false;
    Serial.println("All inputs cleared");
}

// ============================================================================
// OUTPUT FUNCTIONS
// ============================================================================

void evaluateAndDisplay() {
    Serial.println();
    Serial.println("=== FUZZY EVALUATION ===");

    printInputs();

    evaluateFuzzy();

    printOutputs();
}

void printInputs() {
    Serial.println();
    Serial.println("--- Current Inputs ---");
    Serial.printf("TDS:        %s", inputs.tds_valid ? String(inputs.tds, 0).c_str() : "--");
    Serial.printf(" ppm (target: %.0f)\n", config.tds_setpoint);

    Serial.printf("Alkalinity: %s", inputs.alk_valid ? String(inputs.alkalinity, 0).c_str() : "--");
    Serial.printf(" ppm (target: %.0f)\n", config.alk_setpoint);

    Serial.printf("Sulfite:    %s", inputs.sulfite_valid ? String(inputs.sulfite, 0).c_str() : "--");
    Serial.printf(" ppm (target: %.0f)\n", config.sulfite_setpoint);

    Serial.printf("pH:         %s", inputs.ph_valid ? String(inputs.ph, 1).c_str() : "--");
    Serial.printf(" (target: %.1f)\n", config.ph_setpoint);
}

void printOutputs() {
    Serial.println();
    Serial.println("--- Fuzzy Outputs ---");
    Serial.printf("Blowdown:     %.1f%% (recommendation)\n", outputs.blowdown);
    Serial.printf("Caustic:      %.1f%%\n", outputs.caustic);
    Serial.printf("Sulfite Dose: %.1f%%\n", outputs.sulfite_dose);
    Serial.printf("Acid:         %.1f%%\n", outputs.acid);
    Serial.println();
    Serial.printf("Active Rules: %d\n", outputs.active_rules);
    Serial.printf("Confidence:   %s\n", outputs.confidence.c_str());
    Serial.println();
}

// ============================================================================
// TEST FUNCTIONS
// ============================================================================

void runTestScenarios() {
    Serial.println();
    Serial.println("=== RUNNING TEST SCENARIOS ===");
    Serial.println();

    // Scenario 1: Normal conditions
    Serial.println("SCENARIO 1: Normal Conditions");
    inputs.tds = 2500; inputs.tds_valid = true;
    inputs.alkalinity = 300; inputs.alk_valid = true;
    inputs.sulfite = 30; inputs.sulfite_valid = true;
    inputs.ph = 11.0; inputs.ph_valid = true;
    evaluateFuzzy();
    printOutputs();

    delay(1000);

    // Scenario 2: High TDS
    Serial.println("SCENARIO 2: High TDS (3500 ppm)");
    inputs.tds = 3500;
    evaluateFuzzy();
    printOutputs();

    delay(1000);

    // Scenario 3: Low Alkalinity
    Serial.println("SCENARIO 3: Low Alkalinity (150 ppm)");
    inputs.tds = 2500;
    inputs.alkalinity = 150;
    evaluateFuzzy();
    printOutputs();

    delay(1000);

    // Scenario 4: Low Sulfite
    Serial.println("SCENARIO 4: Low Sulfite (10 ppm)");
    inputs.alkalinity = 300;
    inputs.sulfite = 10;
    evaluateFuzzy();
    printOutputs();

    delay(1000);

    // Scenario 5: Low pH
    Serial.println("SCENARIO 5: Low pH (10.0)");
    inputs.sulfite = 30;
    inputs.ph = 10.0;
    evaluateFuzzy();
    printOutputs();

    delay(1000);

    // Scenario 6: Multiple issues
    Serial.println("SCENARIO 6: Multiple Issues (High TDS, Low Alk, Low Sulfite)");
    inputs.tds = 3200;
    inputs.alkalinity = 180;
    inputs.sulfite = 15;
    inputs.ph = 10.5;
    evaluateFuzzy();
    printOutputs();

    Serial.println("=== TEST SCENARIOS COMPLETE ===");
}

void testMembership() {
    Serial.println();
    Serial.println("=== MEMBERSHIP FUNCTION TEST ===");
    Serial.println("Testing TDS membership across range:");
    Serial.println();

    float memberships[TERM_COUNT];

    Serial.println("TDS(ppm)  VL     LOW    MED    HIGH   VH");
    Serial.println("----------------------------------------------");

    for (float tds = 1000; tds <= 4000; tds += 250) {
        calculateMembership(tds, config.tds_setpoint, config.tds_range, memberships);

        Serial.printf("%7.0f   ", tds);
        for (int i = 0; i < TERM_COUNT; i++) {
            Serial.printf("%.2f   ", memberships[i]);
        }
        Serial.println();
    }

    Serial.println();
}

void printConfig() {
    Serial.println("=== CONFIGURATION ===");
    Serial.printf("TDS Setpoint:        %.0f ppm (range: ±%.0f)\n",
                  config.tds_setpoint, config.tds_range);
    Serial.printf("Alkalinity Setpoint: %.0f ppm (range: ±%.0f)\n",
                  config.alk_setpoint, config.alk_range);
    Serial.printf("Sulfite Setpoint:    %.0f ppm (range: ±%.0f)\n",
                  config.sulfite_setpoint, config.sulfite_range);
    Serial.printf("pH Setpoint:         %.1f (range: ±%.1f)\n",
                  config.ph_setpoint, config.ph_range);
    Serial.println();
}

// ============================================================================
// HELP
// ============================================================================

void printMenu() {
    Serial.println();
    Serial.println("=== FUZZY LOGIC TEST MENU ===");
    Serial.println();
    Serial.println("Set Inputs (type letter + value, e.g., 't2500'):");
    Serial.println("  t<value> - Set TDS (ppm), e.g., t2500");
    Serial.println("  a<value> - Set Alkalinity (ppm), e.g., a300");
    Serial.println("  s<value> - Set Sulfite (ppm), e.g., s30");
    Serial.println("  p<value> - Set pH, e.g., p11.0");
    Serial.println("  c        - Clear all inputs");
    Serial.println();
    Serial.println("Evaluate:");
    Serial.println("  e - Evaluate fuzzy logic and display results");
    Serial.println("  i - Print current inputs");
    Serial.println("  o - Print last outputs");
    Serial.println();
    Serial.println("Tests:");
    Serial.println("  r - Run all test scenarios");
    Serial.println("  m - Test membership functions");
    Serial.println();
    Serial.println("Other:");
    Serial.println("  h - Show this menu");
    Serial.println();
    Serial.println("Example: Type 't3000' then 'e' to test high TDS");
    Serial.println();
}

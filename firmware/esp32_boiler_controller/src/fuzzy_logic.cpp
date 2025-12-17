/**
 * @file fuzzy_logic.cpp
 * @brief Fuzzy Logic Controller Implementation
 *
 * Mamdani-type fuzzy inference for boiler water chemistry control.
 * Based on industry best practices for low-pressure steam boilers.
 */

#include "fuzzy_logic.h"
#include <math.h>

// Global instance
FuzzyController fuzzyController;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

FuzzyController::FuzzyController()
    : _config(nullptr)
    , _num_rules(0)
{
    memset(_input_membership, 0, sizeof(_input_membership));
    memset(_manual_values, 0, sizeof(_manual_values));
    memset(_manual_valid, 0, sizeof(_manual_valid));
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool FuzzyController::begin(fuzzy_config_t* config) {
    if (!config) return false;

    _config = config;

    // Initialize linguistic variables
    initInputVariables();
    initOutputVariables();

    // Update membership functions based on config setpoints
    updateMembershipFunctions();

    // Load default rule base
    loadDefaultRules();

    Serial.println("FuzzyController initialized");
    Serial.printf("  Rules: %d\n", _num_rules);

    return true;
}

// ============================================================================
// INPUT VARIABLE INITIALIZATION
// ============================================================================

void FuzzyController::initInputVariables() {
    // TDS (ppm) - manual entry, typical range 500-3000 for boilers
    _inputs[FUZZY_IN_TDS].name = "TDS";
    _inputs[FUZZY_IN_TDS].min_value = 0;
    _inputs[FUZZY_IN_TDS].max_value = 5000;
    _inputs[FUZZY_IN_TDS].num_sets = 5;

    // ALKALINITY (ppm as CaCO3) - typical range 100-700
    _inputs[FUZZY_IN_ALKALINITY].name = "Alkalinity";
    _inputs[FUZZY_IN_ALKALINITY].min_value = 0;
    _inputs[FUZZY_IN_ALKALINITY].max_value = 1000;
    _inputs[FUZZY_IN_ALKALINITY].num_sets = 5;

    // SULFITE (ppm SO3) - typical range 20-60
    _inputs[FUZZY_IN_SULFITE].name = "Sulfite";
    _inputs[FUZZY_IN_SULFITE].min_value = 0;
    _inputs[FUZZY_IN_SULFITE].max_value = 100;
    _inputs[FUZZY_IN_SULFITE].num_sets = 5;

    // PH - typical range 10.5-11.5 for boilers
    _inputs[FUZZY_IN_PH].name = "pH";
    _inputs[FUZZY_IN_PH].min_value = 7.0;
    _inputs[FUZZY_IN_PH].max_value = 14.0;
    _inputs[FUZZY_IN_PH].num_sets = 5;

    // TEMPERATURE (°C)
    _inputs[FUZZY_IN_TEMPERATURE].name = "Temperature";
    _inputs[FUZZY_IN_TEMPERATURE].min_value = 0;
    _inputs[FUZZY_IN_TEMPERATURE].max_value = 100;
    _inputs[FUZZY_IN_TEMPERATURE].num_sets = 3;

    // TREND (rate of change)
    _inputs[FUZZY_IN_TREND].name = "Trend";
    _inputs[FUZZY_IN_TREND].min_value = -100;
    _inputs[FUZZY_IN_TREND].max_value = 100;
    _inputs[FUZZY_IN_TREND].num_sets = 5;
}

// ============================================================================
// OUTPUT VARIABLE INITIALIZATION
// ============================================================================

void FuzzyController::initOutputVariables() {
    // BLOWDOWN (0-100%)
    _outputs[FUZZY_OUT_BLOWDOWN].name = "Blowdown";
    _outputs[FUZZY_OUT_BLOWDOWN].min_value = 0;
    _outputs[FUZZY_OUT_BLOWDOWN].max_value = 100;
    _outputs[FUZZY_OUT_BLOWDOWN].num_sets = 5;

    // Triangular membership functions for output
    _outputs[FUZZY_OUT_BLOWDOWN].sets[0] = {MF_TRIANGULAR, {0, 0, 25}, "Zero"};
    _outputs[FUZZY_OUT_BLOWDOWN].sets[1] = {MF_TRIANGULAR, {0, 25, 50}, "Low"};
    _outputs[FUZZY_OUT_BLOWDOWN].sets[2] = {MF_TRIANGULAR, {25, 50, 75}, "Medium"};
    _outputs[FUZZY_OUT_BLOWDOWN].sets[3] = {MF_TRIANGULAR, {50, 75, 100}, "High"};
    _outputs[FUZZY_OUT_BLOWDOWN].sets[4] = {MF_TRIANGULAR, {75, 100, 100}, "VeryHigh"};

    // CAUSTIC/NaOH (0-100%)
    _outputs[FUZZY_OUT_CAUSTIC].name = "Caustic";
    _outputs[FUZZY_OUT_CAUSTIC].min_value = 0;
    _outputs[FUZZY_OUT_CAUSTIC].max_value = 100;
    _outputs[FUZZY_OUT_CAUSTIC].num_sets = 5;

    _outputs[FUZZY_OUT_CAUSTIC].sets[0] = {MF_TRIANGULAR, {0, 0, 25}, "Zero"};
    _outputs[FUZZY_OUT_CAUSTIC].sets[1] = {MF_TRIANGULAR, {0, 25, 50}, "Low"};
    _outputs[FUZZY_OUT_CAUSTIC].sets[2] = {MF_TRIANGULAR, {25, 50, 75}, "Medium"};
    _outputs[FUZZY_OUT_CAUSTIC].sets[3] = {MF_TRIANGULAR, {50, 75, 100}, "High"};
    _outputs[FUZZY_OUT_CAUSTIC].sets[4] = {MF_TRIANGULAR, {75, 100, 100}, "VeryHigh"};

    // SULFITE (0-100%)
    _outputs[FUZZY_OUT_SULFITE].name = "Sulfite";
    _outputs[FUZZY_OUT_SULFITE].min_value = 0;
    _outputs[FUZZY_OUT_SULFITE].max_value = 100;
    _outputs[FUZZY_OUT_SULFITE].num_sets = 5;

    _outputs[FUZZY_OUT_SULFITE].sets[0] = {MF_TRIANGULAR, {0, 0, 25}, "Zero"};
    _outputs[FUZZY_OUT_SULFITE].sets[1] = {MF_TRIANGULAR, {0, 25, 50}, "Low"};
    _outputs[FUZZY_OUT_SULFITE].sets[2] = {MF_TRIANGULAR, {25, 50, 75}, "Medium"};
    _outputs[FUZZY_OUT_SULFITE].sets[3] = {MF_TRIANGULAR, {50, 75, 100}, "High"};
    _outputs[FUZZY_OUT_SULFITE].sets[4] = {MF_TRIANGULAR, {75, 100, 100}, "VeryHigh"};

    // ACID (0-100%)
    _outputs[FUZZY_OUT_ACID].name = "Acid";
    _outputs[FUZZY_OUT_ACID].min_value = 0;
    _outputs[FUZZY_OUT_ACID].max_value = 100;
    _outputs[FUZZY_OUT_ACID].num_sets = 5;

    _outputs[FUZZY_OUT_ACID].sets[0] = {MF_TRIANGULAR, {0, 0, 25}, "Zero"};
    _outputs[FUZZY_OUT_ACID].sets[1] = {MF_TRIANGULAR, {0, 25, 50}, "Low"};
    _outputs[FUZZY_OUT_ACID].sets[2] = {MF_TRIANGULAR, {25, 50, 75}, "Medium"};
    _outputs[FUZZY_OUT_ACID].sets[3] = {MF_TRIANGULAR, {50, 75, 100}, "High"};
    _outputs[FUZZY_OUT_ACID].sets[4] = {MF_TRIANGULAR, {75, 100, 100}, "VeryHigh"};
}

// ============================================================================
// UPDATE MEMBERSHIP FUNCTIONS BASED ON CONFIG
// ============================================================================

void FuzzyController::updateMembershipFunctions() {
    if (!_config) return;

    float sp, db;

    // TDS membership functions (centered on setpoint)
    // Note: Config uses cond_setpoint for TDS target (ppm)
    sp = _config->cond_setpoint;
    db = _config->cond_deadband;

    _inputs[FUZZY_IN_TDS].sets[0] = {MF_TRAPEZOIDAL, {0, 0, sp*0.5f, sp*0.7f}, "VeryLow"};
    _inputs[FUZZY_IN_TDS].sets[1] = {MF_TRIANGULAR, {sp*0.5f, sp*0.75f, sp-db}, "Low"};
    _inputs[FUZZY_IN_TDS].sets[2] = {MF_TRIANGULAR, {sp-db*2, sp, sp+db*2}, "Normal"};
    _inputs[FUZZY_IN_TDS].sets[3] = {MF_TRIANGULAR, {sp+db, sp*1.25f, sp*1.5f}, "High"};
    _inputs[FUZZY_IN_TDS].sets[4] = {MF_TRAPEZOIDAL, {sp*1.3f, sp*1.5f, 5000, 5000}, "VeryHigh"};

    // ALKALINITY membership functions
    sp = _config->alk_setpoint;
    db = _config->alk_deadband;

    _inputs[FUZZY_IN_ALKALINITY].sets[0] = {MF_TRAPEZOIDAL, {0, 0, sp*0.3f, sp*0.5f}, "VeryLow"};
    _inputs[FUZZY_IN_ALKALINITY].sets[1] = {MF_TRIANGULAR, {sp*0.4f, sp*0.6f, sp-db}, "Low"};
    _inputs[FUZZY_IN_ALKALINITY].sets[2] = {MF_TRIANGULAR, {sp-db*2, sp, sp+db*2}, "Normal"};
    _inputs[FUZZY_IN_ALKALINITY].sets[3] = {MF_TRIANGULAR, {sp+db, sp*1.4f, sp*1.8f}, "High"};
    _inputs[FUZZY_IN_ALKALINITY].sets[4] = {MF_TRAPEZOIDAL, {sp*1.5f, sp*2.0f, 1000, 1000}, "VeryHigh"};

    // SULFITE membership functions
    sp = _config->sulfite_setpoint;
    db = _config->sulfite_deadband;

    _inputs[FUZZY_IN_SULFITE].sets[0] = {MF_TRAPEZOIDAL, {0, 0, sp*0.2f, sp*0.4f}, "VeryLow"};
    _inputs[FUZZY_IN_SULFITE].sets[1] = {MF_TRIANGULAR, {sp*0.3f, sp*0.5f, sp-db}, "Low"};
    _inputs[FUZZY_IN_SULFITE].sets[2] = {MF_TRIANGULAR, {sp-db*2, sp, sp+db*2}, "Normal"};
    _inputs[FUZZY_IN_SULFITE].sets[3] = {MF_TRIANGULAR, {sp+db, sp*1.5f, sp*2.0f}, "High"};
    _inputs[FUZZY_IN_SULFITE].sets[4] = {MF_TRAPEZOIDAL, {sp*1.8f, sp*2.5f, 100, 100}, "VeryHigh"};

    // PH membership functions
    sp = _config->ph_setpoint;
    db = _config->ph_deadband;

    _inputs[FUZZY_IN_PH].sets[0] = {MF_TRAPEZOIDAL, {7.0f, 7.0f, 9.0f, 10.0f}, "Low"};
    _inputs[FUZZY_IN_PH].sets[1] = {MF_TRIANGULAR, {9.5f, 10.5f, sp-db}, "SlightlyLow"};
    _inputs[FUZZY_IN_PH].sets[2] = {MF_TRIANGULAR, {sp-db, sp, sp+db}, "Normal"};
    _inputs[FUZZY_IN_PH].sets[3] = {MF_TRIANGULAR, {sp+db, 12.0f, 12.5f}, "SlightlyHigh"};
    _inputs[FUZZY_IN_PH].sets[4] = {MF_TRAPEZOIDAL, {12.0f, 12.5f, 14.0f, 14.0f}, "High"};

    // TEMPERATURE (simple 3-set)
    _inputs[FUZZY_IN_TEMPERATURE].sets[0] = {MF_TRAPEZOIDAL, {0, 0, 20, 40}, "Cold"};
    _inputs[FUZZY_IN_TEMPERATURE].sets[1] = {MF_TRIANGULAR, {30, 50, 70}, "Warm"};
    _inputs[FUZZY_IN_TEMPERATURE].sets[2] = {MF_TRAPEZOIDAL, {60, 80, 100, 100}, "Hot"};

    // TREND (rate of change)
    _inputs[FUZZY_IN_TREND].sets[0] = {MF_TRAPEZOIDAL, {-100, -100, -30, -10}, "Decreasing"};
    _inputs[FUZZY_IN_TREND].sets[1] = {MF_TRIANGULAR, {-20, -5, 0}, "SlightDecrease"};
    _inputs[FUZZY_IN_TREND].sets[2] = {MF_TRIANGULAR, {-5, 0, 5}, "Stable"};
    _inputs[FUZZY_IN_TREND].sets[3] = {MF_TRIANGULAR, {0, 5, 20}, "SlightIncrease"};
    _inputs[FUZZY_IN_TREND].sets[4] = {MF_TRAPEZOIDAL, {10, 30, 100, 100}, "Increasing"};
}

// ============================================================================
// DEFAULT RULE BASE
// ============================================================================

void FuzzyController::loadDefaultRules() {
    _num_rules = 0;

    // Rule format: {antecedent[6], consequent[4], weight, enabled}
    // Antecedent: [Cond, Alk, Sulfite, pH, Temp, Trend]
    // Consequent: [Blowdown, Caustic, Sulfite, Acid]
    // DC = Don't Care (255)

    // ========================================
    // CONDUCTIVITY/TDS RULES (Blowdown control)
    // ========================================

    // Rule 1: IF Conductivity is VeryHigh THEN Blowdown is VeryHigh
    _rules[_num_rules++] = {{VH, DC, DC, DC, DC, DC}, {VH, DC, DC, DC}, 1.0f, true};

    // Rule 2: IF Conductivity is High THEN Blowdown is High
    _rules[_num_rules++] = {{HI, DC, DC, DC, DC, DC}, {HI, DC, DC, DC}, 1.0f, true};

    // Rule 3: IF Conductivity is Normal THEN Blowdown is Zero
    _rules[_num_rules++] = {{MD, DC, DC, DC, DC, DC}, {VL, DC, DC, DC}, 1.0f, true};

    // Rule 4: IF Conductivity is Low THEN Blowdown is Zero
    _rules[_num_rules++] = {{LO, DC, DC, DC, DC, DC}, {VL, DC, DC, DC}, 1.0f, true};

    // Rule 5: IF Conductivity is High AND Trend is Increasing THEN Blowdown is VeryHigh
    _rules[_num_rules++] = {{HI, DC, DC, DC, DC, VH}, {VH, DC, DC, DC}, 1.0f, true};

    // ========================================
    // ALKALINITY RULES (Caustic control)
    // ========================================

    // Rule 6: IF Alkalinity is VeryLow THEN Caustic is VeryHigh
    _rules[_num_rules++] = {{DC, VL, DC, DC, DC, DC}, {DC, VH, DC, DC}, 1.0f, true};

    // Rule 7: IF Alkalinity is Low THEN Caustic is High
    _rules[_num_rules++] = {{DC, LO, DC, DC, DC, DC}, {DC, HI, DC, DC}, 1.0f, true};

    // Rule 8: IF Alkalinity is Normal THEN Caustic is Zero
    _rules[_num_rules++] = {{DC, MD, DC, DC, DC, DC}, {DC, VL, DC, DC}, 1.0f, true};

    // Rule 9: IF Alkalinity is High THEN Caustic is Zero, Blowdown is Medium
    _rules[_num_rules++] = {{DC, HI, DC, DC, DC, DC}, {MD, VL, DC, DC}, 0.8f, true};

    // Rule 10: IF Alkalinity is VeryHigh THEN Caustic is Zero, Blowdown is High
    _rules[_num_rules++] = {{DC, VH, DC, DC, DC, DC}, {HI, VL, DC, DC}, 0.9f, true};

    // ========================================
    // SULFITE RULES (Oxygen scavenger control)
    // ========================================

    // Rule 11: IF Sulfite is VeryLow THEN Sulfite dosing is VeryHigh
    _rules[_num_rules++] = {{DC, DC, VL, DC, DC, DC}, {DC, DC, VH, DC}, 1.0f, true};

    // Rule 12: IF Sulfite is Low THEN Sulfite dosing is High
    _rules[_num_rules++] = {{DC, DC, LO, DC, DC, DC}, {DC, DC, HI, DC}, 1.0f, true};

    // Rule 13: IF Sulfite is Normal THEN Sulfite dosing is Low (maintenance)
    _rules[_num_rules++] = {{DC, DC, MD, DC, DC, DC}, {DC, DC, LO, DC}, 1.0f, true};

    // Rule 14: IF Sulfite is High THEN Sulfite dosing is Zero
    _rules[_num_rules++] = {{DC, DC, HI, DC, DC, DC}, {DC, DC, VL, DC}, 1.0f, true};

    // Rule 15: IF Sulfite is VeryHigh THEN Sulfite dosing is Zero, Blowdown is Low
    _rules[_num_rules++] = {{DC, DC, VH, DC, DC, DC}, {LO, DC, VL, DC}, 0.7f, true};

    // ========================================
    // pH RULES (Acid/Caustic balance)
    // ========================================

    // Rule 16: IF pH is Low THEN Caustic is High, Acid is Zero
    _rules[_num_rules++] = {{DC, DC, DC, VL, DC, DC}, {DC, HI, DC, VL}, 1.0f, true};

    // Rule 17: IF pH is SlightlyLow THEN Caustic is Medium
    _rules[_num_rules++] = {{DC, DC, DC, LO, DC, DC}, {DC, MD, DC, VL}, 0.8f, true};

    // Rule 18: IF pH is Normal THEN maintain (no action)
    _rules[_num_rules++] = {{DC, DC, DC, MD, DC, DC}, {DC, VL, DC, VL}, 0.5f, true};

    // Rule 19: IF pH is SlightlyHigh THEN Acid is Low
    _rules[_num_rules++] = {{DC, DC, DC, HI, DC, DC}, {DC, VL, DC, LO}, 0.7f, true};

    // Rule 20: IF pH is High THEN Acid is Medium, Caustic is Zero
    _rules[_num_rules++] = {{DC, DC, DC, VH, DC, DC}, {DC, VL, DC, MD}, 0.9f, true};

    // ========================================
    // COMBINED RULES (Multi-parameter)
    // ========================================

    // Rule 21: IF Conductivity is High AND Alkalinity is High THEN Blowdown is VeryHigh
    _rules[_num_rules++] = {{HI, HI, DC, DC, DC, DC}, {VH, VL, DC, DC}, 1.0f, true};

    // Rule 22: IF Conductivity is Low AND Alkalinity is Low THEN Caustic is High
    _rules[_num_rules++] = {{LO, LO, DC, DC, DC, DC}, {VL, HI, DC, DC}, 0.9f, true};

    // Rule 23: IF Sulfite is Low AND Temperature is Hot THEN Sulfite is VeryHigh
    // (Hot water consumes sulfite faster)
    _rules[_num_rules++] = {{DC, DC, LO, DC, HI, DC}, {DC, DC, VH, DC}, 1.0f, true};

    // Rule 24: IF Conductivity is Normal AND Alkalinity is Normal AND Sulfite is Normal
    // THEN all dosing minimal (system in balance)
    _rules[_num_rules++] = {{MD, MD, MD, DC, DC, DC}, {VL, VL, LO, VL}, 1.0f, true};

    // Rule 25: IF Trend is Increasing rapidly THEN Blowdown preemptive increase
    _rules[_num_rules++] = {{DC, DC, DC, DC, DC, VH}, {MD, DC, DC, DC}, 0.8f, true};

    Serial.printf("Loaded %d default rules\n", _num_rules);
}

// ============================================================================
// MEMBERSHIP FUNCTION EVALUATION
// ============================================================================

float FuzzyController::evaluateMF(const membership_func_t& mf, float value,
                                   float min_val, float max_val) {
    float a, b, c, d;

    switch (mf.type) {
        case MF_TRIANGULAR:
            a = mf.params[0];
            b = mf.params[1];  // Peak
            c = mf.params[2];

            if (value <= a || value >= c) return 0.0f;
            if (value <= b) return (value - a) / (b - a);
            return (c - value) / (c - b);

        case MF_TRAPEZOIDAL:
            a = mf.params[0];
            b = mf.params[1];  // Flat start
            c = mf.params[2];  // Flat end
            d = mf.params[3];

            if (value <= a || value >= d) return 0.0f;
            if (value >= b && value <= c) return 1.0f;
            if (value < b) return (value - a) / (b - a);
            return (d - value) / (d - c);

        case MF_GAUSSIAN:
            a = mf.params[0];  // Center
            b = mf.params[1];  // Sigma
            return expf(-0.5f * powf((value - a) / b, 2));

        case MF_SIGMOID_LEFT:
            a = mf.params[0];  // Center
            b = mf.params[1];  // Slope
            return 1.0f / (1.0f + expf(b * (value - a)));

        case MF_SIGMOID_RIGHT:
            a = mf.params[0];  // Center
            b = mf.params[1];  // Slope
            return 1.0f / (1.0f + expf(-b * (value - a)));

        case MF_SINGLETON:
            a = mf.params[0];
            return (fabsf(value - a) < 0.001f) ? 1.0f : 0.0f;

        default:
            return 0.0f;
    }
}

float FuzzyController::getMembership(uint8_t var_idx, uint8_t set_idx, float value) {
    if (var_idx >= FUZZY_MAX_INPUTS || set_idx >= FUZZY_MAX_SETS) return 0.0f;

    linguistic_var_t& var = _inputs[var_idx];
    if (set_idx >= var.num_sets) return 0.0f;

    return evaluateMF(var.sets[set_idx], value, var.min_value, var.max_value);
}

void FuzzyController::fuzzify(uint8_t var_idx, float value, float* degrees) {
    if (var_idx >= FUZZY_MAX_INPUTS || !degrees) return;

    linguistic_var_t& var = _inputs[var_idx];

    for (uint8_t i = 0; i < var.num_sets; i++) {
        degrees[i] = evaluateMF(var.sets[i], value, var.min_value, var.max_value);
    }
}

// ============================================================================
// FUZZY INFERENCE
// ============================================================================

fuzzy_result_t FuzzyController::evaluate(const fuzzy_inputs_t& inputs) {
    fuzzy_result_t result = {0};

    if (!_config) return result;

    // Step 1: Fuzzify all inputs (ALL MANUAL except temperature)

    // TDS - manual entry required
    if (_manual_valid[FUZZY_IN_TDS]) {
        fuzzify(FUZZY_IN_TDS, _manual_values[FUZZY_IN_TDS], _input_membership[FUZZY_IN_TDS]);
    } else {
        // Unknown TDS - assume normal (conservative)
        memset(_input_membership[FUZZY_IN_TDS], 0, sizeof(_input_membership[0]));
        _input_membership[FUZZY_IN_TDS][2] = 1.0f;  // Normal
    }

    // Alkalinity - manual entry required
    if (_manual_valid[FUZZY_IN_ALKALINITY]) {
        fuzzify(FUZZY_IN_ALKALINITY, _manual_values[FUZZY_IN_ALKALINITY], _input_membership[FUZZY_IN_ALKALINITY]);
    } else {
        // Unknown alkalinity - assume normal
        memset(_input_membership[FUZZY_IN_ALKALINITY], 0, sizeof(_input_membership[0]));
        _input_membership[FUZZY_IN_ALKALINITY][2] = 1.0f;  // Normal
    }

    // Sulfite - manual entry required
    if (_manual_valid[FUZZY_IN_SULFITE]) {
        fuzzify(FUZZY_IN_SULFITE, _manual_values[FUZZY_IN_SULFITE], _input_membership[FUZZY_IN_SULFITE]);
    } else {
        memset(_input_membership[FUZZY_IN_SULFITE], 0, sizeof(_input_membership[0]));
        _input_membership[FUZZY_IN_SULFITE][2] = 1.0f;
    }

    // pH - manual entry required
    if (_manual_valid[FUZZY_IN_PH]) {
        fuzzify(FUZZY_IN_PH, _manual_values[FUZZY_IN_PH], _input_membership[FUZZY_IN_PH]);
    } else {
        memset(_input_membership[FUZZY_IN_PH], 0, sizeof(_input_membership[0]));
        _input_membership[FUZZY_IN_PH][2] = 1.0f;
    }

    // Temperature - from sensor (for reference only in manual mode)
    fuzzify(FUZZY_IN_TEMPERATURE, inputs.temperature, _input_membership[FUZZY_IN_TEMPERATURE]);

    // Trend - calculated from TDS history (or zero if insufficient data)
    fuzzify(FUZZY_IN_TREND, inputs.cond_trend, _input_membership[FUZZY_IN_TREND]);

    // Step 2: Apply rules and aggregate outputs
    float output_aggregation[FUZZY_MAX_OUTPUTS][FUZZY_RESOLUTION];
    memset(output_aggregation, 0, sizeof(output_aggregation));

    result.active_rules = 0;
    result.max_firing_strength = 0;

    for (uint8_t r = 0; r < _num_rules; r++) {
        if (!_rules[r].enabled) continue;

        // Calculate firing strength (AND = MIN of antecedents)
        float firing_strength = 1.0f;

        for (uint8_t i = 0; i < FUZZY_MAX_INPUTS; i++) {
            uint8_t term = _rules[r].antecedent[i];
            if (term == DONT_CARE || term >= _inputs[i].num_sets) continue;

            firing_strength = tNormMin(firing_strength, _input_membership[i][term]);
        }

        // Apply rule weight
        firing_strength *= _rules[r].weight;

        if (firing_strength < 0.001f) continue;  // Skip weak rules

        result.active_rules++;

        if (firing_strength > result.max_firing_strength) {
            result.max_firing_strength = firing_strength;
            result.dominant_rule = r;
        }

        // Aggregate consequents (Mamdani: clip output MFs at firing strength)
        for (uint8_t o = 0; o < FUZZY_MAX_OUTPUTS; o++) {
            uint8_t term = _rules[r].consequent[o];
            if (term == DONT_CARE || term >= _outputs[o].num_sets) continue;

            linguistic_var_t& out_var = _outputs[o];
            membership_func_t& out_mf = out_var.sets[term];

            // Sample the output MF and clip at firing strength
            for (int x = 0; x < FUZZY_RESOLUTION; x++) {
                float crisp_x = out_var.min_value +
                    (out_var.max_value - out_var.min_value) * x / (FUZZY_RESOLUTION - 1);

                float mf_value = evaluateMF(out_mf, crisp_x, out_var.min_value, out_var.max_value);
                float clipped = min(mf_value, firing_strength);

                // S-norm aggregation (MAX)
                output_aggregation[o][x] = sNormMax(output_aggregation[o][x], clipped);
            }
        }
    }

    // Step 3: Defuzzify outputs (centroid method)
    result.blowdown_rate = defuzzify(FUZZY_OUT_BLOWDOWN, output_aggregation[FUZZY_OUT_BLOWDOWN]);
    result.caustic_rate = defuzzify(FUZZY_OUT_CAUSTIC, output_aggregation[FUZZY_OUT_CAUSTIC]);
    result.sulfite_rate = defuzzify(FUZZY_OUT_SULFITE, output_aggregation[FUZZY_OUT_SULFITE]);
    result.acid_rate = defuzzify(FUZZY_OUT_ACID, output_aggregation[FUZZY_OUT_ACID]);

    return result;
}

// ============================================================================
// DEFUZZIFICATION
// ============================================================================

float FuzzyController::defuzzify(uint8_t output_idx, float* aggregated) {
    if (output_idx >= FUZZY_MAX_OUTPUTS || !aggregated) return 0.0f;

    linguistic_var_t& var = _outputs[output_idx];

    // Centroid method (Center of Gravity)
    float sum_weighted = 0.0f;
    float sum_membership = 0.0f;

    for (int x = 0; x < FUZZY_RESOLUTION; x++) {
        float crisp_x = var.min_value +
            (var.max_value - var.min_value) * x / (FUZZY_RESOLUTION - 1);

        sum_weighted += crisp_x * aggregated[x];
        sum_membership += aggregated[x];
    }

    if (sum_membership < 0.001f) return 0.0f;

    return sum_weighted / sum_membership;
}

// ============================================================================
// CONFIGURATION AND RULES
// ============================================================================

void FuzzyController::updateConfig(fuzzy_config_t* config) {
    _config = config;
    updateMembershipFunctions();
}

bool FuzzyController::setRule(uint8_t rule_idx, const fuzzy_rule_t& rule) {
    if (rule_idx >= FUZZY_MAX_RULES) return false;

    _rules[rule_idx] = rule;

    if (rule_idx >= _num_rules) {
        _num_rules = rule_idx + 1;
    }

    return true;
}

void FuzzyController::enableRule(uint8_t rule_idx, bool enabled) {
    if (rule_idx < FUZZY_MAX_RULES) {
        _rules[rule_idx].enabled = enabled;
    }
}

uint8_t FuzzyController::getActiveRuleCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < _num_rules; i++) {
        if (_rules[i].enabled) count++;
    }
    return count;
}

void FuzzyController::setManualInput(fuzzy_input_t param, float value, bool valid) {
    if (param < FUZZY_MAX_INPUTS) {
        _manual_values[param] = value;
        _manual_valid[param] = valid;
    }
}

// ============================================================================
// DEBUG OUTPUT
// ============================================================================

void FuzzyController::printDebugInfo() {
    Serial.println("\n=== Fuzzy Controller State ===");
    Serial.printf("Rules: %d active of %d\n", getActiveRuleCount(), _num_rules);

    Serial.println("\nInput Membership Degrees:");
    for (uint8_t i = 0; i < FUZZY_INPUT_COUNT; i++) {
        Serial.printf("  %s: ", _inputs[i].name);
        for (uint8_t j = 0; j < _inputs[i].num_sets; j++) {
            Serial.printf("%.2f ", _input_membership[i][j]);
        }
        Serial.println();
    }

    Serial.println("\nManual Inputs:");
    const char* names[] = {"Cond", "Alk", "Sulfite", "pH", "Temp", "Trend"};
    for (uint8_t i = 0; i < FUZZY_INPUT_COUNT; i++) {
        if (_manual_valid[i]) {
            Serial.printf("  %s: %.2f\n", names[i], _manual_values[i]);
        }
    }
}

/**
 * @file fuzzy_logic.h
 * @brief Fuzzy Logic Controller for Boiler Water Chemistry
 *
 * Implements a Mamdani-type fuzzy inference system for intelligent
 * chemical dosing control based on multiple water quality parameters.
 *
 * MANUAL OPERATION MODE:
 * All inputs except temperature are entered manually via Web UI or LCD.
 * Blowdown output is a RECOMMENDATION only - operator controls valve.
 *
 * Inputs:
 *   - TDS (manual entry, ppm)
 *   - Total Alkalinity (manual entry, ppm as CaCO3)
 *   - Sulfite Residual (manual entry, ppm SO3)
 *   - pH (manual entry)
 *   - Temperature (from sensor, for reference)
 *
 * Outputs (RECOMMENDATIONS):
 *   - Blowdown recommendation (0-100%) - MANUAL VALVE CONTROL
 *   - NaOH dosing rate (0-100%)
 *   - Sulfite/Amine dosing rate (0-100%)
 *   - Acid dosing rate (0-100%)
 */

#ifndef FUZZY_LOGIC_H
#define FUZZY_LOGIC_H

#include <Arduino.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define FUZZY_MAX_RULES         64      // Maximum number of rules
#define FUZZY_MAX_INPUTS        6       // Maximum input variables
#define FUZZY_MAX_OUTPUTS       4       // Maximum output variables
#define FUZZY_MAX_SETS          7       // Max membership functions per variable
#define FUZZY_RESOLUTION        101     // Defuzzification resolution (0-100)

// ============================================================================
// LINGUISTIC VARIABLE INDICES
// ============================================================================

// Input variables (ALL MANUAL except temperature)
typedef enum {
    FUZZY_IN_TDS = 0,           // ppm TDS (manual entry)
    FUZZY_IN_ALKALINITY,        // ppm as CaCO3 (manual entry)
    FUZZY_IN_SULFITE,           // ppm SO3 (manual entry)
    FUZZY_IN_PH,                // pH units (manual entry)
    FUZZY_IN_TEMPERATURE,       // °C (from sensor)
    FUZZY_IN_TREND,             // Rate of change (calculated from TDS history)
    FUZZY_INPUT_COUNT
} fuzzy_input_t;

// Backwards compatibility alias
#define FUZZY_IN_CONDUCTIVITY FUZZY_IN_TDS

// Output variables
typedef enum {
    FUZZY_OUT_BLOWDOWN = 0,     // Blowdown intensity 0-100%
    FUZZY_OUT_CAUSTIC,          // NaOH dosing rate 0-100%
    FUZZY_OUT_SULFITE,          // Sulfite/Amine rate 0-100%
    FUZZY_OUT_ACID,             // H2SO3 rate 0-100%
    FUZZY_OUTPUT_COUNT
} fuzzy_output_t;

// ============================================================================
// MEMBERSHIP FUNCTION TYPES
// ============================================================================

typedef enum {
    MF_TRIANGULAR = 0,          // Triangle: a, b, c (peak at b)
    MF_TRAPEZOIDAL,             // Trapezoid: a, b, c, d (flat top b-c)
    MF_GAUSSIAN,                // Gaussian: center, sigma
    MF_SIGMOID_LEFT,            // Left sigmoid: center, slope
    MF_SIGMOID_RIGHT,           // Right sigmoid: center, slope
    MF_SINGLETON                // Single point: value
} mf_type_t;

// ============================================================================
// LINGUISTIC TERM NAMES
// ============================================================================

typedef enum {
    TERM_VERY_LOW = 0,
    TERM_LOW,
    TERM_MEDIUM_LOW,
    TERM_MEDIUM,
    TERM_MEDIUM_HIGH,
    TERM_HIGH,
    TERM_VERY_HIGH,
    TERM_COUNT
} linguistic_term_t;

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * @brief Membership function definition
 */
typedef struct {
    mf_type_t type;
    float params[4];            // Parameters depend on type
    const char* name;           // Term name for debugging
} membership_func_t;

/**
 * @brief Linguistic variable (input or output)
 */
typedef struct {
    const char* name;
    float min_value;
    float max_value;
    uint8_t num_sets;
    membership_func_t sets[FUZZY_MAX_SETS];
} linguistic_var_t;

/**
 * @brief Fuzzy rule (IF-THEN)
 * Antecedent: input[i] = term (255 = don't care)
 * Consequent: output[j] = term
 */
typedef struct {
    uint8_t antecedent[FUZZY_MAX_INPUTS];   // Input term indices (255 = don't care)
    uint8_t consequent[FUZZY_MAX_OUTPUTS];  // Output term indices (255 = don't care)
    float weight;                            // Rule weight (0.0-1.0)
    bool enabled;
} fuzzy_rule_t;

/**
 * @brief Fuzzy inference result
 */
typedef struct {
    float blowdown_rate;        // 0-100%
    float caustic_rate;         // 0-100%
    float sulfite_rate;         // 0-100%
    float acid_rate;            // 0-100%

    // Confidence/diagnostic info
    float max_firing_strength;  // Highest rule activation
    uint8_t active_rules;       // Number of rules that fired
    uint8_t dominant_rule;      // Index of strongest rule
} fuzzy_result_t;

/**
 * @brief Input values for fuzzy controller
 */
typedef struct {
    float conductivity;         // µS/cm
    float alkalinity;           // ppm CaCO3 (0 if unknown)
    float sulfite;              // ppm SO3 (0 if unknown)
    float ph;                   // pH units (0 if unknown)
    float temperature;          // °C
    float cond_trend;           // Rate of change µS/cm per minute

    // Validity flags (false = use default/safe behavior)
    bool alkalinity_valid;
    bool sulfite_valid;
    bool ph_valid;
} fuzzy_inputs_t;

/**
 * @brief Fuzzy controller configuration (stored in NVS)
 */
typedef struct {
    // Setpoints for "MEDIUM" membership function centers
    float cond_setpoint;        // Target conductivity µS/cm
    float alk_setpoint;         // Target alkalinity ppm
    float sulfite_setpoint;     // Target sulfite ppm
    float ph_setpoint;          // Target pH

    // Deadbands (no action within this range around setpoint)
    float cond_deadband;        // ±µS/cm
    float alk_deadband;         // ±ppm
    float sulfite_deadband;     // ±ppm
    float ph_deadband;          // ±pH units

    // Output scaling
    float blowdown_max;         // Max blowdown duration per cycle (sec)
    float caustic_max_ml_min;   // Max NaOH rate ml/min
    float sulfite_max_ml_min;   // Max sulfite rate ml/min
    float acid_max_ml_min;      // Max acid rate ml/min

    // Control behavior
    bool aggressive_mode;       // Faster response, higher overshoot risk
    bool manual_override;       // Disable fuzzy, use manual rates
    uint8_t inference_method;   // 0=Mamdani, 1=Sugeno
    uint8_t defuzz_method;      // 0=Centroid, 1=Bisector, 2=MOM, 3=SOM, 4=LOM

} fuzzy_config_t;

// ============================================================================
// FUZZY LOGIC CONTROLLER CLASS
// ============================================================================

class FuzzyController {
public:
    FuzzyController();

    /**
     * @brief Initialize fuzzy controller with default rules
     * @param config Pointer to configuration structure
     * @return true if successful
     */
    bool begin(fuzzy_config_t* config);

    /**
     * @brief Process inputs through fuzzy inference
     * @param inputs Current sensor/manual readings
     * @return Fuzzy inference results
     */
    fuzzy_result_t evaluate(const fuzzy_inputs_t& inputs);

    /**
     * @brief Update configuration and rebuild membership functions
     * @param config New configuration
     */
    void updateConfig(fuzzy_config_t* config);

    /**
     * @brief Get membership degree for input in specific set
     * @param var_idx Variable index (fuzzy_input_t)
     * @param set_idx Set index within variable
     * @param value Crisp input value
     * @return Membership degree [0.0, 1.0]
     */
    float getMembership(uint8_t var_idx, uint8_t set_idx, float value);

    /**
     * @brief Get all membership degrees for an input
     * @param var_idx Variable index
     * @param value Crisp input value
     * @param degrees Output array of membership degrees
     */
    void fuzzify(uint8_t var_idx, float value, float* degrees);

    /**
     * @brief Add or modify a rule
     * @param rule_idx Rule index (0 to FUZZY_MAX_RULES-1)
     * @param rule Rule definition
     * @return true if successful
     */
    bool setRule(uint8_t rule_idx, const fuzzy_rule_t& rule);

    /**
     * @brief Enable/disable a rule
     * @param rule_idx Rule index
     * @param enabled Enable state
     */
    void enableRule(uint8_t rule_idx, bool enabled);

    /**
     * @brief Get number of active rules
     */
    uint8_t getActiveRuleCount();

    /**
     * @brief Reset to default rule base
     */
    void loadDefaultRules();

    /**
     * @brief Print debug info to Serial
     */
    void printDebugInfo();

    /**
     * @brief Manual test input entry (from LCD menu)
     * @param param Parameter type (alkalinity, sulfite, pH)
     * @param value Test result value
     * @param valid Whether the value is valid
     */
    void setManualInput(fuzzy_input_t param, float value, bool valid = true);

private:
    fuzzy_config_t* _config;

    // Linguistic variables
    linguistic_var_t _inputs[FUZZY_MAX_INPUTS];
    linguistic_var_t _outputs[FUZZY_MAX_OUTPUTS];

    // Rule base
    fuzzy_rule_t _rules[FUZZY_MAX_RULES];
    uint8_t _num_rules;

    // Current fuzzified inputs
    float _input_membership[FUZZY_MAX_INPUTS][FUZZY_MAX_SETS];

    // Manual input storage
    float _manual_values[FUZZY_MAX_INPUTS];
    bool _manual_valid[FUZZY_MAX_INPUTS];

    // Internal methods
    void initInputVariables();
    void initOutputVariables();
    void updateMembershipFunctions();

    float evaluateMF(const membership_func_t& mf, float value, float min_val, float max_val);
    float applyRule(const fuzzy_rule_t& rule);
    float defuzzify(uint8_t output_idx, float* aggregated);

    // T-norm and S-norm operations
    float tNormMin(float a, float b) { return min(a, b); }
    float tNormProduct(float a, float b) { return a * b; }
    float sNormMax(float a, float b) { return max(a, b); }
};

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

extern FuzzyController fuzzyController;

// ============================================================================
// HELPER MACROS FOR RULE DEFINITION
// ============================================================================

#define DONT_CARE 255

// Shorthand for common terms
#define VL TERM_VERY_LOW
#define LO TERM_LOW
#define ML TERM_MEDIUM_LOW
#define MD TERM_MEDIUM
#define MH TERM_MEDIUM_HIGH
#define HI TERM_HIGH
#define VH TERM_VERY_HIGH
#define DC DONT_CARE

#endif // FUZZY_LOGIC_H

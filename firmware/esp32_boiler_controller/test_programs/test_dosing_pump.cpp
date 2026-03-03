/**
 * @file test_dosing_pump.cpp
 * @brief Test program for dosing pump + water meter + LCD + encoder
 *
 * Features:
 * - Single stepper-driven dosing pump (A4988) using existing CNC-shield pins
 * - Water meter pulse input (1 pulse = 1 gallon)
 * - Simple dosing logic:
 *      - PER_GAL mode: fixed cups of chemical per gallon
 *      - PER_DAY mode: target cups per day; computes effective cups/gal
 *        based on gallons seen so far today, but never below base Dose/Gal
 * - 20x4 I2C LCD status screen
 * - Rotary encoder menu for:
 *      - Mode select
 *      - Dose/Gal
 *      - Target cups/day
 *      - Pump prime
 *      - Reset totals
 * - Small serial debug menu
 *
 * Usage:
 * - Build with PlatformIO env: [env:test_dosing_pump]
 * - Open Serial Monitor at 115200 baud
 * - Use encoder to navigate LCD menu; use serial for debug/inspect
 */

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "driver/gpio.h"    // gpio_set_level() — IRAM-safe, used inside timer ISR

#include "pin_definitions.h"
#include "encoder.h"
#include <Preferences.h>

// Optional encoder debug — set to 1 to enable verbose serial logging of
// menu movements and value-edit deltas for manual detent testing.
#define ENCODER_DEBUG 1

// #region agent log
static void agentDebugLog(const char* location, const char* message, const char* hypothesisId, int32_t value) {
    if (!Serial) {
        return;
    }
    Serial.print("{\"sessionId\":\"d7cd8f\",\"runId\":\"menu\",\"hypothesisId\":\"");
    Serial.print(hypothesisId);
    Serial.print("\",\"location\":\"");
    Serial.print(location);
    Serial.print("\",\"message\":\"");
    Serial.print(message);
    Serial.print("\",\"data\":{\"value\":");
    Serial.print(value);
    Serial.print("},\"timestamp\":");
    Serial.print((unsigned long)millis());
    Serial.println("}");
}
// #endregion

// ============================================================================
// DOSING CONFIGURATION
// ============================================================================

enum DoseMode {
    DOSE_PER_GALLON = 0,
    DOSE_PER_DAY_AVG = 1
};

struct DosingConfig {
    DoseMode mode;
    float dose_per_gallon_cups;   // User base concentration (cups/gal)
    float target_cups_per_day;    // Desired daily total (cups/day) for PER_DAY mode
};

struct DosingState {
    float cups_per_gallon_effective;  // Effective cups/gal used for dosing
    float total_gallons_today;        // Gallons measured in current "day"
    float cups_dispensed_today;       // Cups actually dosed in current "day"

    uint32_t day_start_ms;           // millis() when current day window started
    uint32_t last_pulse_count;       // Last raw pulse count snapshot
};

// Per-pump dosing configurations (independent settings)
static DosingConfig g_config_p1 = {
    DOSE_PER_GALLON,
    1.0f / 50.0f,   // Default: 1 cup / 50 gallons
    2.0f            // Default target: 2 cups/day
};

static DosingConfig g_config_p2 = {
    DOSE_PER_GALLON,
    1.0f / 50.0f,
    2.0f
};

static DosingState g_state = {
    1.0f / 50.0f,   // cups_per_gallon_effective (P1)
    0.0f,
    0.0f,
    0,
    0
};

// Pump enable flags
static bool g_pump1_enabled = true;
static bool g_pump2_enabled = true;

// P2 additional state (P1 state lives in g_state)
static float g_p2_eff_dose   = 1.0f / 50.0f;
static float g_p2_cups_today = 0.0f;

// Pump calibration: runtime steps per cup per pump (operator-calibrated).
// Using Nema17 (200 steps/rev) at 16x microstepping → 3200 steps/rev.
// Start with assumption: 1 rev ≈ 1/10 cup ⇒ ~10 rev/cup = 32000 steps/cup.
static float g_steps_per_cup_p1 = 32000.0f;   // Pump 1 (H2SO3), updated via Calibrate menu
static float g_steps_per_cup_p2 = 32000.0f;   // Pump 2 (NaOH), updated via Calibrate menu

// NVS persistence — calibration values are saved here and restored on boot.
static Preferences g_prefs;
static constexpr const char* NVS_NS = "dosing";   // namespace (≤15 chars)
static constexpr const char* NVS_P1 = "spc_p1";   // steps-per-cup pump 1
static constexpr const char* NVS_P2 = "spc_p2";   // steps-per-cup pump 2
static void loadCalibration();   // forward decl — defined near setup()
static void saveCalibration();   // forward decl — defined near setup()

// Calibration uses grams as the operator unit and converts to cups internally.
// Approximate density → 1 US cup ≈ 236.6 g (close enough for dosing purposes).
static const float GRAMS_PER_CUP      = 236.6f;
static const float CALIB_TARGET_GRAMS = 50.0f;   // aim for ~50 g test shot

// Day length for "today" statistics (ms). 24 hours = 86,400,000 ms.
static const uint32_t DAY_WINDOW_MS = 24UL * 60UL * 60UL * 1000UL;

// How often to refresh LCD status (ms)
static const uint32_t LCD_UPDATE_INTERVAL_MS = 500UL;

// Encoder: ~20 detents (clicks) per rotation; ENCODER_STEPS_PER_NOTCH pulses per detent (pin_definitions.h).
// Step per encoder position unit so that one physical click = desired step.
static const float DOSE_GAL_STEP_PER_DETENT  = 0.005f;   // cups/gal per click (20 clicks → 0.1 cups/gal per rotation)
static const float DOSE_DAY_STEP_PER_DETENT  = 0.2f;     // cups/day per click
static const float CALIB_GRAMS_PER_DETENT    = 0.1f;     // grams per click (calibrate measured mass)
#define DOSE_GAL_INC    (DOSE_GAL_STEP_PER_DETENT)   // encoder.getDelta() now reports detents
#define DOSE_DAY_INC    (DOSE_DAY_STEP_PER_DETENT)
#define CALIB_GRAMS_INC (CALIB_GRAMS_PER_DETENT)

// ============================================================================
// UI STATE
// ============================================================================

enum UiState {
    UI_HOME = 0,
    UI_MENU,
    UI_PRIME_MENU,
    UI_PRIME,
    UI_DOSE_MENU,
    UI_RESET_CONFIRM,
    UI_EDIT_P1_DOSE_GAL,
    UI_EDIT_P1_DOSE_DAY,
    UI_EDIT_P2_DOSE_GAL,
    UI_EDIT_P2_DOSE_DAY,
    UI_PUMP_ENABLE_MENU,
    UI_CALIBRATE_MENU,
    UI_CALIBRATE
};

enum CalibSubState {
    CALIB_READY = 0,
    CALIB_RUNNING,
    CALIB_MEASURE,
    CALIB_SAVED
};

static UiState       g_ui_state  = UI_HOME;
static UiState       g_last_ui_state = UI_HOME;
static CalibSubState g_calib_state = CALIB_READY;
static float         g_calib_measured_grams = 0.0f;
static uint32_t      g_calib_saved_ms = 0;
static long          g_calib_last_steps = 0;
static int           g_prime_pump = 0;   // 0 = P1 (H2SO3), 1 = P2 (NaOH)
static int           g_calib_pump = 0;   // 0 = P1, 1 = P2

// ============================================================================
// WATER METER PULSE TRACKING (SINGLE METER)
// ============================================================================

// Use canonical definitions from pin_definitions.h
// WATER_METER_PIN, WATER_METER_DEBOUNCE_MS, WATER_METER_PULSES_PER_GAL

static volatile uint32_t s_pulse_count = 0;
static volatile uint32_t s_last_pulse_ms = 0;

void IRAM_ATTR onWaterMeterPulse() {
    uint32_t now = millis();
    if (now - s_last_pulse_ms >= WATER_METER_DEBOUNCE_MS) {
        s_pulse_count++;
        s_last_pulse_ms = now;
    }
}

// Returns number of new gallons since last call, updates g_state.total_gallons_today
static float consumeNewGallons() {
    noInterrupts();
    uint32_t pulses = s_pulse_count;
    interrupts();

    uint32_t delta_pulses = pulses - g_state.last_pulse_count;
    g_state.last_pulse_count = pulses;

    if (delta_pulses == 0) {
        return 0.0f;
    }

    float delta_gallons = delta_pulses / (float)WATER_METER_PULSES_PER_GAL;
    g_state.total_gallons_today += delta_gallons;
    return delta_gallons;
}

// ============================================================================
// STEPPER PUMP CONTROL — Hardware Timer ISR, two independent pumps
// ============================================================================
//
// Pump 1 (H2SO3) → ESP32 hardware timer 0 / STEPPER1_STEP_PIN / STEPPER1_DIR_PIN
// Pump 2 (NaOH)  → ESP32 hardware timer 1 / STEPPER2_STEP_PIN / STEPPER2_DIR_PIN
//
// Both pumps share a single active-LOW enable pin (STEPPER_ENABLE_PIN).
// The enable pin is held LOW whenever either pump has steps remaining, and
// released HIGH only when both counters reach zero.
//
// Each timer fires at 2× the desired step rate.  On the first interrupt the
// STEP pin rises; on the second it falls (one complete step) and the counter
// decrements.  gpio_set_level() is IRAM-safe so it is safe inside an ISR.
//
// Timing: PUMP_FEED_MM_PER_SEC=100, PUMP_STEPS_PER_MM=16 → 1 600 steps/s
//         half-period = 500 000 / 1 600 = 312 µs

// Shared pump mechanics — TB6600 at 8× microstepping (1 600 steps/rev)
static const float PUMP_STEPS_PER_REV   = 1600.0f;  // 200 steps × 8 microsteps
static const float PUMP_MM_PER_REV      = 100.0f;   // 1 rev = 100 mm (tune on hardware)
static const float PUMP_STEPS_PER_MM    = PUMP_STEPS_PER_REV / PUMP_MM_PER_REV;
static const float PUMP_FEED_MM_PER_SEC = 100.0f;   // ~1 rev/s
static const float PUMP_MM_PER_GALLON   = 1000.0f;

// Shared enable state — true when either pump is running
static bool g_pump_active = false;

// Pump 1 (H2SO3) state
static volatile long g_pump1_steps = 0;
static volatile bool s_pump1_phase = false;
static portMUX_TYPE  s_pump1_mux   = portMUX_INITIALIZER_UNLOCKED;
static hw_timer_t*   s_pump1_timer = nullptr;

// Pump 2 (NaOH) state
static volatile long g_pump2_steps = 0;
static volatile bool s_pump2_phase = false;
static portMUX_TYPE  s_pump2_mux   = portMUX_INITIALIZER_UNLOCKED;
static hw_timer_t*   s_pump2_timer = nullptr;

void IRAM_ATTR onPump1Timer() {
    portENTER_CRITICAL_ISR(&s_pump1_mux);
    if (g_pump1_steps > 0) {
        if (!s_pump1_phase) {
            gpio_set_level(STEPPER1_STEP_PIN, 1);
            s_pump1_phase = true;
        } else {
            gpio_set_level(STEPPER1_STEP_PIN, 0);
            s_pump1_phase = false;
            g_pump1_steps--;
        }
    }
    portEXIT_CRITICAL_ISR(&s_pump1_mux);
}

void IRAM_ATTR onPump2Timer() {
    portENTER_CRITICAL_ISR(&s_pump2_mux);
    if (g_pump2_steps > 0) {
        if (!s_pump2_phase) {
            gpio_set_level(STEPPER2_STEP_PIN, 1);
            s_pump2_phase = true;
        } else {
            gpio_set_level(STEPPER2_STEP_PIN, 0);
            s_pump2_phase = false;
            g_pump2_steps--;
        }
    }
    portEXIT_CRITICAL_ISR(&s_pump2_mux);
}

static long mmToSteps(float mm) {
    return (long)lroundf(mm * PUMP_STEPS_PER_MM);
}

static void setSharedEnable(bool enable) {
    digitalWrite(STEPPER_ENABLE_PIN, enable ? LOW : HIGH);
    g_pump_active = enable;
}

static void pumpInit() {
    // Shared enable
    pinMode(STEPPER_ENABLE_PIN, OUTPUT);
    digitalWrite(STEPPER_ENABLE_PIN, HIGH);  // Active LOW — disabled on boot

    // Pump 1 (H2SO3)
    pinMode(STEPPER1_DIR_PIN, OUTPUT);
    digitalWrite(STEPPER1_DIR_PIN, HIGH);    // Forward (pumping) direction
    pinMode(STEPPER1_STEP_PIN, OUTPUT);
    digitalWrite(STEPPER1_STEP_PIN, LOW);

    // Pump 2 (NaOH)
    pinMode(STEPPER2_DIR_PIN, OUTPUT);
    digitalWrite(STEPPER2_DIR_PIN, HIGH);    // Forward (pumping) direction
    pinMode(STEPPER2_STEP_PIN, OUTPUT);
    digitalWrite(STEPPER2_STEP_PIN, LOW);

    g_pump1_steps = 0;  s_pump1_phase = false;
    g_pump2_steps = 0;  s_pump2_phase = false;

    const float    steps_per_sec = PUMP_FEED_MM_PER_SEC * PUMP_STEPS_PER_MM;
    const uint32_t half_us       = (uint32_t)(500000.0f / steps_per_sec);

    s_pump1_timer = timerBegin(0, 80, true);
    timerAttachInterrupt(s_pump1_timer, &onPump1Timer, true);
    timerAlarmWrite(s_pump1_timer, half_us, true);
    timerAlarmEnable(s_pump1_timer);

    s_pump2_timer = timerBegin(1, 80, true);
    timerAttachInterrupt(s_pump2_timer, &onPump2Timer, true);
    timerAlarmWrite(s_pump2_timer, half_us, true);
    timerAlarmEnable(s_pump2_timer);
}

// ---- Pump 1 (H2SO3) queue helpers ----

static void queuePumpSteps(long steps) {
    if (steps <= 0) return;
    setSharedEnable(true);
    portENTER_CRITICAL(&s_pump1_mux);
    g_pump1_steps += steps;
    portEXIT_CRITICAL(&s_pump1_mux);
}

static void pumpRunDistanceMm(float mm) {
    long steps = mmToSteps(mm);
    if (steps <= 0) return;
    queuePumpSteps(steps);
}

static void queuePumpVolumeCups(float cups) {
    if (cups <= 0.0f) return;
    long steps = (long)roundf(cups * g_steps_per_cup_p1);
    if (steps <= 0) return;
    queuePumpSteps(steps);
}

static void pumpPrime() {
    queuePumpVolumeCups(0.25f);
}

static void pumpCalibrationMove() {
    const float CALIB_MM = 100.0f;
    pumpRunDistanceMm(CALIB_MM);
    Serial.println();
    Serial.printf("P1 calibration: %.1f mm (%.3f rev)\n", CALIB_MM, CALIB_MM / PUMP_MM_PER_REV);
    Serial.println("Measure volume to derive ml/mm or cups/mm.");
}

// ---- Pump 2 (NaOH) queue helpers ----

static void queue2PumpSteps(long steps) {
    if (steps <= 0) return;
    setSharedEnable(true);
    portENTER_CRITICAL(&s_pump2_mux);
    g_pump2_steps += steps;
    portEXIT_CRITICAL(&s_pump2_mux);
}

static void pump2RunDistanceMm(float mm) {
    long steps = mmToSteps(mm);
    if (steps <= 0) return;
    queue2PumpSteps(steps);
}

static void pump2QueueVolumeCups(float cups) {
    if (cups <= 0.0f) return;
    long steps = (long)roundf(cups * g_steps_per_cup_p2);
    if (steps <= 0) return;
    queue2PumpSteps(steps);
}

static void pump2Prime() {
    pump2QueueVolumeCups(0.25f);
}

static void pump2CalibrationMove() {
    const float CALIB_MM = 100.0f;
    pump2RunDistanceMm(CALIB_MM);
    Serial.println();
    Serial.printf("P2 calibration: %.1f mm (%.3f rev)\n", CALIB_MM, CALIB_MM / PUMP_MM_PER_REV);
    Serial.println("Measure volume to derive ml/mm or cups/mm.");
}

// ---- Shared update / stop ----

// Called from loop() — manages the shared enable pin; all stepping is in ISRs.
static void pumpUpdate() {
    portENTER_CRITICAL(&s_pump1_mux);
    long r1 = g_pump1_steps;
    portEXIT_CRITICAL(&s_pump1_mux);
    portENTER_CRITICAL(&s_pump2_mux);
    long r2 = g_pump2_steps;
    portEXIT_CRITICAL(&s_pump2_mux);

    if (g_pump_active && r1 == 0 && r2 == 0) {
        setSharedEnable(false);
    }
}

static void pumpEmergencyStop() {
    portENTER_CRITICAL(&s_pump1_mux);
    g_pump1_steps = 0;  s_pump1_phase = false;
    portEXIT_CRITICAL(&s_pump1_mux);
    portENTER_CRITICAL(&s_pump2_mux);
    g_pump2_steps = 0;  s_pump2_phase = false;
    portEXIT_CRITICAL(&s_pump2_mux);
    gpio_set_level(STEPPER1_STEP_PIN, 0);
    gpio_set_level(STEPPER2_STEP_PIN, 0);
    setSharedEnable(false);
}

// ============================================================================
// DOSING LOGIC
// ============================================================================

static void maybeStartNewDay() {
    uint32_t now = millis();
    if (g_state.day_start_ms == 0) {
        g_state.day_start_ms = now;
        return;
    }

    if (now - g_state.day_start_ms >= DAY_WINDOW_MS) {
        g_state.day_start_ms = now;
        g_state.total_gallons_today = 0.0f;
        g_state.cups_dispensed_today = 0.0f;
        g_p2_cups_today = 0.0f;
        g_state.last_pulse_count = 0;
        s_pulse_count = 0;
    }
}

// Returns effective cups-per-gallon for a given pump config.
// Uses total gallons seen today to back-calculate rate in PER_DAY mode.
static float computeEffectiveDose(const DosingConfig& cfg) {
    if (cfg.mode == DOSE_PER_GALLON) {
        return cfg.dose_per_gallon_cups;
    }
    // PER_DAY_AVG mode: aim for target_cups_per_day distributed over today's flow
    float base   = cfg.dose_per_gallon_cups;
    float target = cfg.target_cups_per_day;
    if (g_state.total_gallons_today > 0.0f && target > 0.0f) {
        return max(base, target / g_state.total_gallons_today);
    }
    return base;
}

static void updateEffectiveDosePerGallon() {
    g_state.cups_per_gallon_effective = computeEffectiveDose(g_config_p1);
    g_p2_eff_dose                     = computeEffectiveDose(g_config_p2);
}

static void processDosingFromWaterMeter() {
    float delta_gallons = consumeNewGallons();
    if (delta_gallons <= 0.0f) {
        return;
    }

    // Recompute effective dose rates before queuing
    updateEffectiveDosePerGallon();

    // Pump 1 (H2SO3)
    if (g_pump1_enabled) {
        float cups_p1 = delta_gallons * g_state.cups_per_gallon_effective;
        long  steps_p1 = (long)roundf(cups_p1 * g_steps_per_cup_p1);
        agentDebugLog("test_dosing_pump.cpp:processDosingFromWaterMeter",
                      "P1_steps", "D1", steps_p1);
        if (steps_p1 > 0) {
            queuePumpSteps(steps_p1);
            g_state.cups_dispensed_today += cups_p1;
        }
    }

    // Pump 2 (NaOH)
    if (g_pump2_enabled) {
        float cups_p2 = delta_gallons * g_p2_eff_dose;
        long  steps_p2 = (long)roundf(cups_p2 * g_steps_per_cup_p2);
        agentDebugLog("test_dosing_pump.cpp:processDosingFromWaterMeter",
                      "P2_steps", "D2", steps_p2);
        if (steps_p2 > 0) {
            queue2PumpSteps(steps_p2);
            g_p2_cups_today += cups_p2;
        }
    }
}

// ============================================================================
// LCD UI
// ============================================================================

// Reuse main I2C bus pins/addr from pin_definitions.h
static LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

static uint32_t g_last_lcd_update_ms = 0;

static const char* modeToString(DoseMode mode) {
    switch (mode) {
        case DOSE_PER_GALLON:  return "PER_GAL ";
        case DOSE_PER_DAY_AVG: return "PER_DAY";
        default:               return "UNKN   ";
    }
}

static const char* modeShort(DoseMode mode) {
    return (mode == DOSE_PER_GALLON) ? "GAL" : "DAY";
}

static void lcdInit() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ);

    lcd.init();
    lcd.backlight();
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("Dosing Pump Test");
    lcd.setCursor(0, 1);
    lcd.print("Booting...");
}

// Forward declarations for screen renderers and helpers
static void lcdDrawHome();
static void lcdDrawMainMenu();
static void lcdDrawPrimeMenu();
static void lcdDrawPrime();
static void lcdDrawDoseMenu();
static void lcdDrawPumpEnableMenu();
static void lcdDrawResetConfirm();
static void lcdDrawCalibrateMenu();
static void lcdDrawCalibrate();
static void lcdDrawMenuList(const char* title, const char* const* items, int count, int selected);
static void lcdDrawEditValue(const char* title, const char* unit, float value);

static void lcdDrawHome() {
    char line[21];

    // Row 0: title
    lcd.setCursor(0, 0);
    snprintf(line, sizeof(line), "   BOILER  DOSING   ");
    lcd.print(line);

    // Row 1: total gallons today
    lcd.setCursor(0, 1);
    snprintf(line, sizeof(line), "Water:%6.1f gal/d", g_state.total_gallons_today);
    lcd.print(line);

    // Row 2: P1 status (enable flag + cups dispensed)
    lcd.setCursor(0, 2);
    snprintf(line, sizeof(line), "P1[%-3s] Dsp:%6.3fc",
             g_pump1_enabled ? "ON" : "OFF",
             g_state.cups_dispensed_today);
    lcd.print(line);

    // Row 3: P2 status
    lcd.setCursor(0, 3);
    snprintf(line, sizeof(line), "P2[%-3s] Dsp:%6.3fc",
             g_pump2_enabled ? "ON" : "OFF",
             g_p2_cups_today);
    lcd.print(line);
}

static void lcdDrawMenuList(const char* title, const char* const* items, int count, int selected) {
    char line[21];

    // Title on row 0
    lcd.setCursor(0, 0);
    snprintf(line, sizeof(line), "%-20s", title);
    lcd.print(line);

    // Show up to 3 items on rows 1–3 with a sliding window
    int first = 0;
    if (selected > 2) {
        first = selected - 2;
    }
    if (first < 0) first = 0;
    if (first > (count - 3)) first = max(0, count - 3);

    for (int row = 0; row < 3; ++row) {
        int idx = first + row;
        lcd.setCursor(0, row + 1);
        if (idx < count) {
            const char* label = items[idx];
            snprintf(line, sizeof(line), "%c%-19s",
                     (idx == selected) ? '>' : ' ',
                     label);
        } else {
            snprintf(line, sizeof(line), "%20s", "");
        }
        lcd.print(line);
    }
}

static void lcdDrawMainMenu() {
    static const char* const ITEMS[] = {
        "Prime",
        "Dose",
        "Calibrate",
        "Pump Enable",
        "Reset"
    };
    int selected = menuNav.getSelectedIndex();
    lcdDrawMenuList(" ------ MENU ------ ", ITEMS, 5, selected);
}

static void lcdDrawPrimeMenu() {
    static const char* const ITEMS[] = {
        "Pump 1 (Sulfite)",
        "Pump 2 (Amine)",
        "Back"
    };
    int selected = menuNav.getSelectedIndex();
    lcdDrawMenuList(" --- PRIME ---      ", ITEMS, 3, selected);
}

static void lcdDrawCalibrateMenu() {
    static const char* const ITEMS[] = {
        "Pump 1 (Sulfite)",
        "Pump 2 (Amine)",
        "Back"
    };
    int selected = menuNav.getSelectedIndex();
    lcdDrawMenuList(" -- CALIBRATE --    ", ITEMS, 3, selected);
}

static void lcdDrawDoseMenu() {
    static const char* const ITEMS[] = {
        "P1 Sulfite Dose/Gal",
        "P1 Sulfite Dose/Day",
        "P2 Amine Dose/Gal",
        "P2 Amine Dose/Day",
        "Back"
    };
    int selected = menuNav.getSelectedIndex();
    lcdDrawMenuList(" ----- DOSE -----   ", ITEMS, 5, selected);
}

static void lcdDrawPumpEnableMenu() {
    char item0[21], item1[21];
    snprintf(item0, sizeof(item0), "P1 (Sulfite):[%s]", g_pump1_enabled ? "ON " : "OFF");
    snprintf(item1, sizeof(item1), "P2 (Amine): [%s]", g_pump2_enabled ? "ON " : "OFF");
    const char* const items[3] = { item0, item1, "Back" };
    int selected = menuNav.getSelectedIndex();
    lcdDrawMenuList(" - PUMP ENABLE -    ", items, 3, selected);
}

static void lcdDrawEditValue(const char* title, const char* unit, float value) {
    char line[21];

    lcd.setCursor(0, 0);
    snprintf(line, sizeof(line), "%-20s", title);
    lcd.print(line);

    lcd.setCursor(0, 1);
    snprintf(line, sizeof(line), "   %7.3f %s", value, unit);
    lcd.print(line);

    lcd.setCursor(0, 2);
    snprintf(line, sizeof(line), "  Turn: adjust      ");
    lcd.print(line);

    lcd.setCursor(0, 3);
    snprintf(line, sizeof(line), "  Press: save       ");
    lcd.print(line);
}

static void lcdDrawPrime() {
    char line[21];
    const char* label = (g_prime_pump == 0) ? "P1" : "P2";
    lcd.setCursor(0, 0);
    snprintf(line, sizeof(line), " -- PRIME %s --     ", label);
    lcd.print(line);

    bool p1_run = (g_pump1_steps > 0);
    bool p2_run = (g_pump2_steps > 0);
    const char* status = (g_prime_pump == 0)
        ? (p1_run ? "  P1 running...     " : "  P1 idle.          ")
        : (p2_run ? "  P2 running...     " : "  P2 idle.          ");
    lcd.setCursor(0, 1);
    lcd.print(status);

    lcd.setCursor(0, 2);
    snprintf(line, sizeof(line), "                    ");
    lcd.print(line);

    lcd.setCursor(0, 3);
    snprintf(line, sizeof(line), "  Press: done       ");
    lcd.print(line);
}

static void lcdDrawResetConfirm() {
    char line[21];
    lcd.setCursor(0, 0);
    snprintf(line, sizeof(line), " -- RESET --        ");
    lcd.print(line);

    lcd.setCursor(0, 1);
    snprintf(line, sizeof(line), " Clear today totals?");
    lcd.print(line);

    lcd.setCursor(0, 2);
    snprintf(line, sizeof(line), "  Press: confirm    ");
    lcd.print(line);

    lcd.setCursor(0, 3);
    snprintf(line, sizeof(line), "  Hold: cancel      ");
    lcd.print(line);
}

static void lcdDrawCalibrate() {
    char line[21];
    const char* plabel = (g_calib_pump == 0) ? "P1" : "P2";

    switch (g_calib_state) {
        case CALIB_READY:
            lcd.setCursor(0, 0);
            snprintf(line, sizeof(line), " -- CAL %s --       ", plabel);
            lcd.print(line);

            lcd.setCursor(0, 1);
            snprintf(line, sizeof(line), "  Test dose: ~50g   ");
            lcd.print(line);

            lcd.setCursor(0, 2);
            snprintf(line, sizeof(line), "                    ");
            lcd.print(line);

            lcd.setCursor(0, 3);
            snprintf(line, sizeof(line), "   Press: dispense  ");
            lcd.print(line);
            break;

        case CALIB_RUNNING:
            lcd.setCursor(0, 0);
            snprintf(line, sizeof(line), " -- CAL %s --       ", plabel);
            lcd.print(line);

            lcd.setCursor(0, 1);
            snprintf(line, sizeof(line), "  Dispensing ~50g   ");
            lcd.print(line);

            lcd.setCursor(0, 2);
            snprintf(line, sizeof(line), "                    ");
            lcd.print(line);

            lcd.setCursor(0, 3);
            snprintf(line, sizeof(line), "   Please wait...   ");
            lcd.print(line);
            break;

        case CALIB_MEASURE:
            lcd.setCursor(0, 0);
            snprintf(line, sizeof(line), " -- CAL %s --       ", plabel);
            lcd.print(line);

            lcd.setCursor(0, 1);
            snprintf(line, sizeof(line), "  Weigh output (g): ");
            lcd.print(line);

            lcd.setCursor(0, 2);
            snprintf(line, sizeof(line), "       %4.1f g      ", g_calib_measured_grams);
            lcd.print(line);

            lcd.setCursor(0, 3);
            snprintf(line, sizeof(line), "Press:save Hold:redo");
            lcd.print(line);
            break;

        case CALIB_SAVED:
        default:
            lcd.setCursor(0, 0);
            snprintf(line, sizeof(line), " -- CAL %s --       ", plabel);
            lcd.print(line);

            lcd.setCursor(0, 1);
            snprintf(line, sizeof(line), "  Calibration saved ");
            lcd.print(line);

            lcd.setCursor(0, 2);
            snprintf(line, sizeof(line), "                    ");
            lcd.print(line);

            lcd.setCursor(0, 3);
            snprintf(line, sizeof(line), "                    ");
            lcd.print(line);
            break;
    }
}

static void lcdUpdate() {
    uint32_t now = millis();
    if (now - g_last_lcd_update_ms < LCD_UPDATE_INTERVAL_MS) return;
    g_last_lcd_update_ms = now;

    // Clear once when switching between screens to avoid stale content
    if (g_ui_state != g_last_ui_state) {
        lcd.clear();
        g_last_ui_state = g_ui_state;
    }

    switch (g_ui_state) {
        case UI_MENU:
            lcdDrawMainMenu();
            break;
        case UI_PRIME_MENU:
            lcdDrawPrimeMenu();
            break;
        case UI_PRIME:
            lcdDrawPrime();
            break;
        case UI_DOSE_MENU:
            lcdDrawDoseMenu();
            break;
        case UI_RESET_CONFIRM:
            lcdDrawResetConfirm();
            break;
        case UI_EDIT_P1_DOSE_GAL:
            lcdDrawEditValue(" - P1 DOSE/GAL -    ", "cups/gal", g_config_p1.dose_per_gallon_cups);
            break;
        case UI_EDIT_P1_DOSE_DAY:
            lcdDrawEditValue(" - P1 DOSE/DAY -    ", "cups/day", g_config_p1.target_cups_per_day);
            break;
        case UI_EDIT_P2_DOSE_GAL:
            lcdDrawEditValue(" - P2 DOSE/GAL -    ", "cups/gal", g_config_p2.dose_per_gallon_cups);
            break;
        case UI_EDIT_P2_DOSE_DAY:
            lcdDrawEditValue(" - P2 DOSE/DAY -    ", "cups/day", g_config_p2.target_cups_per_day);
            break;
        case UI_PUMP_ENABLE_MENU:
            lcdDrawPumpEnableMenu();
            break;
        case UI_CALIBRATE_MENU:
            lcdDrawCalibrateMenu();
            break;
        case UI_CALIBRATE:
            lcdDrawCalibrate();
            break;
        case UI_HOME:
        default:
            lcdDrawHome();
            break;
    }
}

// ============================================================================
// ENCODER + MENU
// ============================================================================

static void encoderInit() {
    encoder.begin();
    encoder.setAcceleration(true, 50, 4);
    // Start with main menu configuration (5 items)
    menuNav.setMenu(5, true);
    menuNav.setSelectedIndex(0);

    agentDebugLog("test_dosing_pump.cpp:encoderInit",
                  "menuNav main menu",
                  "M0",
                  menuNav.getSelectedIndex());
}

// Helper to centralize UI state transitions and menu configuration
static void uiTransitionTo(UiState newState) {
    if (g_ui_state == newState) {
        return;
    }

    g_ui_state = newState;

    // Use 1 step per detent on value-edit screens so dose/gal steps are exactly 0.005
    if (newState == UI_EDIT_P1_DOSE_GAL || newState == UI_EDIT_P2_DOSE_GAL ||
        newState == UI_EDIT_P1_DOSE_DAY || newState == UI_EDIT_P2_DOSE_DAY ||
        newState == UI_CALIBRATE) {
        encoder.setAcceleration(false);
    } else {
        encoder.setAcceleration(true, 50, 4);
    }

    switch (newState) {
        case UI_MENU:
            menuNav.setMenu(5, true);   // Prime, Dose, Calibrate, Pump Enable, Reset
            menuNav.setSelectedIndex(0);
            break;
        case UI_PRIME_MENU:
            menuNav.setMenu(3, true);   // Pump 1, Pump 2, Back
            menuNav.setSelectedIndex(0);
            break;
        case UI_DOSE_MENU:
            menuNav.setMenu(5, true);   // P1 D/G, P1 D/D, P2 D/G, P2 D/D, Back
            menuNav.setSelectedIndex(0);
            break;
        case UI_PUMP_ENABLE_MENU:
            menuNav.setMenu(3, true);   // Pump 1 toggle, Pump 2 toggle, Back
            menuNav.setSelectedIndex(0);
            break;
        case UI_CALIBRATE_MENU:
            menuNav.setMenu(3, true);   // Pump 1, Pump 2, Back
            menuNav.setSelectedIndex(0);
            break;
        case UI_EDIT_P1_DOSE_GAL:
        case UI_EDIT_P1_DOSE_DAY:
        case UI_EDIT_P2_DOSE_GAL:
        case UI_EDIT_P2_DOSE_DAY:
            encoder.clearLimits();
            encoder.resetPosition();   // Reset so getDelta() starts clean
            break;
        case UI_CALIBRATE:
            encoder.clearLimits();
            encoder.resetPosition();
            break;
        default:
            // Other screens do not use list navigation
            break;
    }

    if (newState == UI_CALIBRATE) {
        // Reset calibration sub-state each time we enter the calibrate UI
        g_calib_state = CALIB_READY;
        g_calib_measured_grams = 0.0f;
        g_calib_last_steps = 0;
        g_calib_saved_ms = 0;
    }
}

static void handleMenuNavigation() {
    // Update encoder internal state (button, acceleration)
    encoder.update();

    // When editing values (dose/gal, dose/day, calibrate measure), rotation drives getDelta()
    // not menu selection — pass false so we don't bounce between menu items
    bool process_rotation = (g_ui_state != UI_EDIT_P1_DOSE_GAL &&
                             g_ui_state != UI_EDIT_P1_DOSE_DAY &&
                             g_ui_state != UI_EDIT_P2_DOSE_GAL &&
                             g_ui_state != UI_EDIT_P2_DOSE_DAY &&
                             !(g_ui_state == UI_CALIBRATE && g_calib_state == CALIB_MEASURE));
    bool selection_changed = menuNav.update(process_rotation);

#if ENCODER_DEBUG
    if (selection_changed && process_rotation) {
        Serial.print("[ENC] menu state=");
        Serial.print((int)g_ui_state);
        Serial.print(" sel=");
        Serial.println(menuNav.getSelectedIndex());
    }
#endif

    // Handle emergency stop / back on long press (always active),
    // except for the calibration MEASURE step where Hold = "redo".
    if (menuNav.backPressed()) {
        if (g_ui_state == UI_CALIBRATE && g_calib_state == CALIB_MEASURE) {
            // Redo calibration shot: go back to READY without changing calibration.
            g_calib_measured_grams = 0.0f;
            g_calib_state = CALIB_READY;
            return;
        }

        pumpEmergencyStop();
        uiTransitionTo(UI_HOME);
        return;
    }

    switch (g_ui_state) {
        case UI_HOME:
            // From home, short press opens the main menu.
            if (menuNav.enterPressed()) {
                uiTransitionTo(UI_MENU);
            }
            break;

        case UI_MENU: {
            int sel = menuNav.getSelectedIndex();
            if (menuNav.enterPressed()) {
                switch (sel) {
                    case 0: // Prime → submenu
                        uiTransitionTo(UI_PRIME_MENU);
                        break;
                    case 1: // Dose submenu
                        uiTransitionTo(UI_DOSE_MENU);
                        break;
                    case 2: // Calibrate → submenu
                        uiTransitionTo(UI_CALIBRATE_MENU);
                        break;
                    case 3: // Pump Enable → submenu
                        uiTransitionTo(UI_PUMP_ENABLE_MENU);
                        break;
                    case 4: // Reset totals confirm
                        uiTransitionTo(UI_RESET_CONFIRM);
                        break;
                    default:
                        break;
                }
            }
            break;
        }

        case UI_PRIME_MENU: {
            int sel = menuNav.getSelectedIndex();
            if (menuNav.enterPressed()) {
                switch (sel) {
                    case 0: // Pump 1 (H2SO3)
                        g_prime_pump = 0;
                        pumpPrime();
                        uiTransitionTo(UI_PRIME);
                        break;
                    case 1: // Pump 2 (NaOH)
                        g_prime_pump = 1;
                        pump2Prime();
                        uiTransitionTo(UI_PRIME);
                        break;
                    case 2: // Back
                        uiTransitionTo(UI_MENU);
                        break;
                    default:
                        break;
                }
            }
            break;
        }

        case UI_DOSE_MENU: {
            int sel = menuNav.getSelectedIndex();
            if (menuNav.enterPressed()) {
                switch (sel) {
                    case 0: // P1 Dose / Gal
                        uiTransitionTo(UI_EDIT_P1_DOSE_GAL);
                        break;
                    case 1: // P1 Dose / Day
                        uiTransitionTo(UI_EDIT_P1_DOSE_DAY);
                        break;
                    case 2: // P2 Dose / Gal
                        uiTransitionTo(UI_EDIT_P2_DOSE_GAL);
                        break;
                    case 3: // P2 Dose / Day
                        uiTransitionTo(UI_EDIT_P2_DOSE_DAY);
                        break;
                    case 4: // Back
                        uiTransitionTo(UI_MENU);
                        break;
                    default:
                        break;
                }
            }
            break;
        }

        case UI_PUMP_ENABLE_MENU: {
            int sel = menuNav.getSelectedIndex();
            if (menuNav.enterPressed()) {
                switch (sel) {
                    case 0: // Toggle Pump 1
                        g_pump1_enabled = !g_pump1_enabled;
                        break;
                    case 1: // Toggle Pump 2
                        g_pump2_enabled = !g_pump2_enabled;
                        break;
                    case 2: // Back
                        uiTransitionTo(UI_MENU);
                        break;
                    default:
                        break;
                }
            }
            break;
        }

        case UI_CALIBRATE_MENU: {
            int sel = menuNav.getSelectedIndex();
            if (menuNav.enterPressed()) {
                switch (sel) {
                    case 0: // Pump 1 (H2SO3)
                        g_calib_pump = 0;
                        uiTransitionTo(UI_CALIBRATE);
                        break;
                    case 1: // Pump 2 (NaOH)
                        g_calib_pump = 1;
                        uiTransitionTo(UI_CALIBRATE);
                        break;
                    case 2: // Back
                        uiTransitionTo(UI_MENU);
                        break;
                    default:
                        break;
                }
            }
            break;
        }

        case UI_EDIT_P1_DOSE_GAL: {
            // Edit P1 base dose per gallon (0–1 cups/gal); one physical click = 0.005 cups/gal
            int32_t delta = encoder.getDelta();
#if ENCODER_DEBUG
            if (delta != 0) {
                Serial.print("[ENC] UI_EDIT_P1_DOSE_GAL detent delta=");
                Serial.println(delta);
            }
#endif
            if (delta != 0) {
                float v = g_config_p1.dose_per_gallon_cups + (delta * DOSE_GAL_INC);
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                g_config_p1.dose_per_gallon_cups = roundf(v / DOSE_GAL_STEP_PER_DETENT) * DOSE_GAL_STEP_PER_DETENT;
            }
            if (menuNav.enterPressed()) { uiTransitionTo(UI_DOSE_MENU); }
            break;
        }

        case UI_EDIT_P1_DOSE_DAY: {
            // Edit P1 target cups per day (0–10); one physical click = 0.2 cups/day
            int32_t delta = encoder.getDelta();
#if ENCODER_DEBUG
            if (delta != 0) {
                Serial.print("[ENC] UI_EDIT_P1_DOSE_DAY detent delta=");
                Serial.println(delta);
            }
#endif
            if (delta != 0) {
                float v = g_config_p1.target_cups_per_day + (delta * DOSE_DAY_INC);
                if (v < 0.0f) v = 0.0f;
                if (v > 10.0f) v = 10.0f;
                g_config_p1.target_cups_per_day = v;
            }
            if (menuNav.enterPressed()) { uiTransitionTo(UI_DOSE_MENU); }
            break;
        }

        case UI_EDIT_P2_DOSE_GAL: {
            // Edit P2 base dose per gallon (0–1 cups/gal); one physical click = 0.005 cups/gal
            int32_t delta = encoder.getDelta();
#if ENCODER_DEBUG
            if (delta != 0) {
                Serial.print("[ENC] UI_EDIT_P2_DOSE_GAL detent delta=");
                Serial.println(delta);
            }
#endif
            if (delta != 0) {
                float v = g_config_p2.dose_per_gallon_cups + (delta * DOSE_GAL_INC);
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                g_config_p2.dose_per_gallon_cups = roundf(v / DOSE_GAL_STEP_PER_DETENT) * DOSE_GAL_STEP_PER_DETENT;
            }
            if (menuNav.enterPressed()) { uiTransitionTo(UI_DOSE_MENU); }
            break;
        }

        case UI_EDIT_P2_DOSE_DAY: {
            // Edit P2 target cups per day (0–10); one physical click = 0.2 cups/day
            int32_t delta = encoder.getDelta();
#if ENCODER_DEBUG
            if (delta != 0) {
                Serial.print("[ENC] UI_EDIT_P2_DOSE_DAY detent delta=");
                Serial.println(delta);
            }
#endif
            if (delta != 0) {
                float v = g_config_p2.target_cups_per_day + (delta * DOSE_DAY_INC);
                if (v < 0.0f) v = 0.0f;
                if (v > 10.0f) v = 10.0f;
                g_config_p2.target_cups_per_day = v;
            }
            if (menuNav.enterPressed()) { uiTransitionTo(UI_DOSE_MENU); }
            break;
        }

        case UI_PRIME:
            // Prime screen: short press returns to main menu; long press already stops pumps.
            if (menuNav.enterPressed()) {
                uiTransitionTo(UI_MENU);
            }
            break;

        case UI_RESET_CONFIRM:
            // Confirm reset of daily totals.
            if (menuNav.enterPressed()) {
                g_state.total_gallons_today = 0.0f;
                g_state.cups_dispensed_today = 0.0f;
                g_p2_cups_today = 0.0f;
                g_state.last_pulse_count = 0;
                s_pulse_count = 0;
                g_state.day_start_ms = millis();
                uiTransitionTo(UI_MENU);
            }
            break;

        case UI_CALIBRATE: {
            // Automatic transition from RUNNING to MEASURE once the selected pump finishes.
            if (g_calib_state == CALIB_RUNNING) {
                long remaining;
                if (g_calib_pump == 0) {
                    portENTER_CRITICAL(&s_pump1_mux);
                    remaining = g_pump1_steps;
                    portEXIT_CRITICAL(&s_pump1_mux);
                } else {
                    portENTER_CRITICAL(&s_pump2_mux);
                    remaining = g_pump2_steps;
                    portEXIT_CRITICAL(&s_pump2_mux);
                }
                if (remaining == 0) {
                    g_calib_state = CALIB_MEASURE;
                    g_calib_measured_grams = 0.0f;
                    encoder.clearLimits();
                    encoder.resetPosition();   // Reset so getDelta() starts clean for grams input
                }
            }

            switch (g_calib_state) {
                case CALIB_READY:
                    if (menuNav.enterPressed()) {
                        float* steps_ptr = (g_calib_pump == 0) ? &g_steps_per_cup_p1 : &g_steps_per_cup_p2;
                        float fallback_steps_per_cup = 32000.0f;
                        float steps_per_cup = (*steps_ptr > 0.0f) ? *steps_ptr : fallback_steps_per_cup;
                        float steps_per_gram = steps_per_cup / GRAMS_PER_CUP;
                        long calib_steps = (long)lroundf(steps_per_gram * CALIB_TARGET_GRAMS);
                        if (calib_steps <= 0) {
                            calib_steps = 1000;
                        }
                        g_calib_last_steps = calib_steps;
                        if (g_calib_pump == 0) {
                            queuePumpSteps(calib_steps);
                        } else {
                            queue2PumpSteps(calib_steps);
                        }
                        g_calib_state = CALIB_RUNNING;
                    }
                    break;

                case CALIB_MEASURE: {
                    int32_t delta = encoder.getDelta();
                    if (delta != 0) {
                        g_calib_measured_grams += delta * CALIB_GRAMS_INC;  // one click = 0.1 g
                        if (g_calib_measured_grams < 0.1f) g_calib_measured_grams = 0.1f;
                        if (g_calib_measured_grams > 500.0f) g_calib_measured_grams = 500.0f;
                    }

                    if (menuNav.enterPressed() &&
                        g_calib_measured_grams > 0.0f &&
                        g_calib_last_steps > 0) {
                        float new_steps = (float)g_calib_last_steps * (GRAMS_PER_CUP / g_calib_measured_grams);
                        if (g_calib_pump == 0) {
                            g_steps_per_cup_p1 = new_steps;
                        } else {
                            g_steps_per_cup_p2 = new_steps;
                        }
                        saveCalibration();   // persist to NVS before transitioning
                        g_calib_state = CALIB_SAVED;
                        g_calib_saved_ms = millis();
                    }
                    break;
                }

                case CALIB_SAVED:
                    // After a brief confirmation, return to main menu.
                    if (millis() - g_calib_saved_ms > 1500U) {
                        uiTransitionTo(UI_MENU);
                    }
                    break;

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// SERIAL DEBUG INTERFACE
// ============================================================================

static void printStatus();

static void printHelp() {
    Serial.println();
    Serial.println("=== Dosing Pump Test Menu ===");
    Serial.println("  p   - Print status");
    Serial.println("  d   - Dump today's usage summary");
    Serial.println("  z   - Reset daily totals");
    Serial.println("  s   - Emergency stop both pumps");
    Serial.println("  c   - Pump 1 (Sulfite) calibration move");
    Serial.println("  e   - Pump 2 (Amine) calibration move");
    Serial.println("  q   - Prime Pump 2 (Amine)");
    Serial.println("  h/? - Show this help");
    Serial.println();
}

static void handleSerial() {
    if (!Serial.available()) return;
    char c = Serial.read();
    if (c == '\n' || c == '\r') return;

    switch (c) {
        case 'p':
        case 'P':
            printStatus();
            break;

        case 'c':
        case 'C':
            Serial.println();
            Serial.println("=== Pump 1 (Sulfite) CALIBRATION MOVE ===");
            pumpCalibrationMove();
            break;

        case 'e':
        case 'E':
            Serial.println();
            Serial.println("=== Pump 2 (Amine) CALIBRATION MOVE ===");
            pump2CalibrationMove();
            break;

        case 'q':
        case 'Q':
            Serial.println("Priming Pump 2 (Amine)...");
            pump2Prime();
            break;

        case 'd':
        case 'D':
            Serial.println();
            Serial.println("=== DAILY USAGE SUMMARY ===");
            Serial.printf("Gallons today      : %.2f gal\n", g_state.total_gallons_today);
            Serial.println("-- Pump 1 (Sulfite) --");
            Serial.printf("  Mode             : %s\n", modeToString(g_config_p1.mode));
            Serial.printf("  Dose/Gal base    : %.4f cups/gal\n", g_config_p1.dose_per_gallon_cups);
            Serial.printf("  Target cups/day  : %.2f cups\n", g_config_p1.target_cups_per_day);
            Serial.printf("  Eff. Dose/Gal    : %.4f cups/gal\n", g_state.cups_per_gallon_effective);
            Serial.printf("  Cups dispensed   : %.3f cups\n", g_state.cups_dispensed_today);
            Serial.printf("  Enabled          : %s\n", g_pump1_enabled ? "YES" : "NO");
            Serial.println("-- Pump 2 (Amine) --");
            Serial.printf("  Mode             : %s\n", modeToString(g_config_p2.mode));
            Serial.printf("  Dose/Gal base    : %.4f cups/gal\n", g_config_p2.dose_per_gallon_cups);
            Serial.printf("  Target cups/day  : %.2f cups\n", g_config_p2.target_cups_per_day);
            Serial.printf("  Eff. Dose/Gal    : %.4f cups/gal\n", g_p2_eff_dose);
            Serial.printf("  Cups dispensed   : %.3f cups\n", g_p2_cups_today);
            Serial.printf("  Enabled          : %s\n", g_pump2_enabled ? "YES" : "NO");
            Serial.println();
            break;

        case 'z':
        case 'Z':
            g_state.total_gallons_today = 0.0f;
            g_state.cups_dispensed_today = 0.0f;
            g_p2_cups_today = 0.0f;
            g_state.last_pulse_count = 0;
            s_pulse_count = 0;
            g_state.day_start_ms = millis();
            Serial.println("Daily totals reset.");
            break;

        case 's':
        case 'S':
            pumpEmergencyStop();
            Serial.println("Emergency stop: both pumps halted, driver disabled.");
            break;

        case 'h':
        case 'H':
        case '?':
            printHelp();
            break;

        default:
            Serial.printf("Unknown command '%c'. Press 'h' for help.\n", c);
            break;
    }
}

static void printStatus() {
    noInterrupts();
    uint32_t pulses = s_pulse_count;
    interrupts();

    Serial.println();
    Serial.println("=== STATUS ===");
    Serial.printf("Gallons today      : %.2f gal\n", g_state.total_gallons_today);
    Serial.printf("Raw pulse count    : %lu pulses\n", (unsigned long)pulses);
    Serial.printf("Driver enable      : %s\n", g_pump_active ? "YES" : "NO");
    Serial.println("-- Pump 1 (Sulfite) --");
    Serial.printf("  Mode             : %s\n", modeToString(g_config_p1.mode));
    Serial.printf("  Dose/Gal base    : %.4f cups/gal\n", g_config_p1.dose_per_gallon_cups);
    Serial.printf("  Target cups/day  : %.2f cups\n", g_config_p1.target_cups_per_day);
    Serial.printf("  Eff. Dose/Gal    : %.4f cups/gal\n", g_state.cups_per_gallon_effective);
    Serial.printf("  Cups dispensed   : %.3f cups\n", g_state.cups_dispensed_today);
    Serial.printf("  Enabled          : %s\n", g_pump1_enabled ? "YES" : "NO");
    Serial.printf("  Steps remaining  : %ld\n", g_pump1_steps);
    Serial.println("-- Pump 2 (Amine) --");
    Serial.printf("  Mode             : %s\n", modeToString(g_config_p2.mode));
    Serial.printf("  Dose/Gal base    : %.4f cups/gal\n", g_config_p2.dose_per_gallon_cups);
    Serial.printf("  Target cups/day  : %.2f cups\n", g_config_p2.target_cups_per_day);
    Serial.printf("  Eff. Dose/Gal    : %.4f cups/gal\n", g_p2_eff_dose);
    Serial.printf("  Cups dispensed   : %.3f cups\n", g_p2_cups_today);
    Serial.printf("  Enabled          : %s\n", g_pump2_enabled ? "YES" : "NO");
    Serial.printf("  Steps remaining  : %ld\n", g_pump2_steps);
    Serial.println();
}

// ============================================================================
// NVS CALIBRATION PERSISTENCE
// ============================================================================

// Reads g_steps_per_cup_p1 / _p2 from NVS.  If no saved value exists the
// runtime defaults (32 000 steps/cup) are left unchanged.
static void loadCalibration() {
    if (!g_prefs.begin(NVS_NS, /*readOnly=*/true)) {
        Serial.println("[NVS] No saved calibration found; using firmware defaults.");
        return;
    }
    g_steps_per_cup_p1 = g_prefs.getFloat(NVS_P1, g_steps_per_cup_p1);
    g_steps_per_cup_p2 = g_prefs.getFloat(NVS_P2, g_steps_per_cup_p2);
    g_prefs.end();
    Serial.printf("[NVS] Calibration loaded: P1=%.1f steps/cup  P2=%.1f steps/cup\n",
                  g_steps_per_cup_p1, g_steps_per_cup_p2);
}

// Persists the current g_steps_per_cup_p1 / _p2 to NVS.
// Call this immediately after a successful calibration save.
static void saveCalibration() {
    if (!g_prefs.begin(NVS_NS, /*readOnly=*/false)) {
        Serial.println("[NVS] ERROR: could not open NVS for writing — calibration NOT saved.");
        return;
    }
    g_prefs.putFloat(NVS_P1, g_steps_per_cup_p1);
    g_prefs.putFloat(NVS_P2, g_steps_per_cup_p2);
    g_prefs.end();
    Serial.printf("[NVS] Calibration saved:  P1=%.1f steps/cup  P2=%.1f steps/cup\n",
                  g_steps_per_cup_p1, g_steps_per_cup_p2);
}

// ============================================================================
// ARDUINO SETUP / LOOP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  DOSING PUMP TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();

    // Restore calibration from NVS before initialising the pump
    loadCalibration();

    // Initialize pump
    pumpInit();

    // Initialize water meter input
    pinMode(WATER_METER_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(WATER_METER_PIN), onWaterMeterPulse, FALLING);

    // Initialize LCD, encoder, and menu
    lcdInit();
    encoderInit();

    // Initialize day tracking
    g_state.day_start_ms = millis();
    g_state.total_gallons_today = 0.0f;
    g_state.cups_dispensed_today = 0.0f;
    g_state.last_pulse_count = 0;
    s_pulse_count = 0;

    printHelp();
}

void loop() {
    // Give the stepper scheduler first chance each loop for smoother motion
    pumpUpdate();

    handleSerial();
    maybeStartNewDay();
    processDosingFromWaterMeter();
    handleMenuNavigation();
    lcdUpdate();

    // Only delay when both pumps are idle (ISR handles stepping, not loop).
    if (g_pump1_steps == 0 && g_pump2_steps == 0) {
        delay(1);
    }
}


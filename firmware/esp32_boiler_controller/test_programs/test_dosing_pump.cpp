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
#include <AccelStepper.h>

#include "pin_definitions.h"
#include "encoder.h"

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

static DosingConfig g_config = {
    DOSE_PER_GALLON,
    1.0f / 50.0f,   // Default: 1 cup / 50 gallons
    2.0f            // Default target: 2 cups/day (arbitrary starting point)
};

static DosingState g_state = {
    1.0f / 50.0f,
    0.0f,
    0.0f,
    0,
    0
};

// Pump calibration: rough steps per cup (user should calibrate on hardware).
// Using Nema17 (200 steps/rev) at 16x microstepping → 3200 steps/rev.
// Start with assumption: 1 rev ≈ 1/10 cup ⇒ ~10 rev/cup = 32000 steps/cup.
static const float STEPS_PER_CUP = 32000.0f;

// Day length for "today" statistics (ms). 24 hours = 86,400,000 ms.
static const uint32_t DAY_WINDOW_MS = 24UL * 60UL * 60UL * 1000UL;

// How often to refresh LCD status (ms)
static const uint32_t LCD_UPDATE_INTERVAL_MS = 500UL;

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
// STEPPER PUMP CONTROL (SINGLE PUMP)
// ============================================================================

// Use Stepper 1 (H2SO3) pins from pin_definitions.h for this test
AccelStepper dosingPump(AccelStepper::DRIVER, STEPPER1_STEP_PIN, STEPPER1_DIR_PIN);

static bool g_pump_active = false;

static void pumpInit() {
    // Common enable pin shared by all A4988 drivers
    pinMode(STEPPER_ENABLE_PIN, OUTPUT);
    digitalWrite(STEPPER_ENABLE_PIN, HIGH); // Disable on boot (HIGH = disabled)

    dosingPump.setMaxSpeed(STEPPER_MAX_SPEED);
    dosingPump.setAcceleration(STEPPER_ACCELERATION);
    dosingPump.setCurrentPosition(0);
}

static void pumpEnable(bool enable) {
    digitalWrite(STEPPER_ENABLE_PIN, enable ? LOW : HIGH);  // Active LOW
    g_pump_active = enable;
}

static void queuePumpSteps(long steps) {
    if (steps <= 0) return;
    pumpEnable(true);
    dosingPump.move(dosingPump.distanceToGo() + steps);
}

static void queuePumpVolumeCups(float cups) {
    if (cups <= 0.0f) return;
    long steps = (long)roundf(cups * STEPS_PER_CUP);
    if (steps <= 0) return;
    queuePumpSteps(steps);
}

static void pumpPrime() {
    // Prime with a small fixed volume (e.g. 0.25 cup)
    const float PRIME_CUPS = 0.25f;
    queuePumpVolumeCups(PRIME_CUPS);
}

static void pumpUpdate() {
    dosingPump.run();

    if (g_pump_active && !dosingPump.isRunning() && dosingPump.distanceToGo() == 0) {
        // Queue is empty and motor is idle; disable driver
        pumpEnable(false);
    }
}

static void pumpEmergencyStop() {
    dosingPump.stop();
    dosingPump.setCurrentPosition(dosingPump.currentPosition());
    dosingPump.move(0);
    pumpEnable(false);
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
        g_state.last_pulse_count = 0;
        s_pulse_count = 0;
    }
}

static void updateEffectiveDosePerGallon() {
    if (g_config.mode == DOSE_PER_GALLON) {
        g_state.cups_per_gallon_effective = g_config.dose_per_gallon_cups;
        return;
    }

    // PER_DAY_AVG mode
    float base = g_config.dose_per_gallon_cups;
    float target = g_config.target_cups_per_day;

    float dose_target_per_gallon;
    if (g_state.total_gallons_today > 0.0f && target > 0.0f) {
        dose_target_per_gallon = target / g_state.total_gallons_today;
    } else {
        dose_target_per_gallon = base;
    }

    // Never dose less than base concentration; system can increase automatically
    g_state.cups_per_gallon_effective = max(base, dose_target_per_gallon);
}

static void processDosingFromWaterMeter() {
    float delta_gallons = consumeNewGallons();
    if (delta_gallons <= 0.0f) return;

    updateEffectiveDosePerGallon();

    float cups_to_dose = g_state.cups_per_gallon_effective * delta_gallons;
    if (cups_to_dose <= 0.0f) return;

    g_state.cups_dispensed_today += cups_to_dose;
    queuePumpVolumeCups(cups_to_dose);
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

static void lcdUpdate() {
    uint32_t now = millis();
    if (now - g_last_lcd_update_ms < LCD_UPDATE_INTERVAL_MS) return;
    g_last_lcd_update_ms = now;

    lcd.setCursor(0, 0);
    lcd.print("Mode:");
    lcd.print(modeToString(g_config.mode));
    lcd.print(" ");
    lcd.print(dosingPump.isRunning() ? "RUN" : "IDL");

    lcd.setCursor(0, 1);
    lcd.print("Dose/Gal:");
    lcd.print(g_state.cups_per_gallon_effective, 3);
    lcd.print("c   ");

    lcd.setCursor(0, 2);
    lcd.print("Today:");
    lcd.print(g_state.total_gallons_today, 1);
    lcd.print("gal   ");

    lcd.setCursor(0, 3);
    lcd.print("Chem:");
    lcd.print(g_state.cups_dispensed_today, 2);
    lcd.print("c   ");
}

// ============================================================================
// ENCODER + MENU
// ============================================================================

// Menu indices
enum MenuItem {
    MENU_MODE = 0,
    MENU_DOSE_PER_GAL = 1,
    MENU_TARGET_PER_DAY = 2,
    MENU_PUMP_PRIME = 3,
    MENU_RESET_TOTALS = 4,
    MENU_COUNT = 5
};

static bool g_editing = false;
static MenuItem g_current_item = MENU_MODE;

static void encoderInit() {
    encoder.begin();
    encoder.setAcceleration(true, 50, 4);
    menuNav.setMenu(MENU_COUNT, true);
}

static void handleMenuNavigation() {
    // Update encoder internal state (button, acceleration)
    encoder.update();

    // Update menu selection
    if (menuNav.update()) {
        g_current_item = static_cast<MenuItem>(menuNav.getSelectedIndex());
    }

    // Handle value editing
    if (!g_editing) {
        if (menuNav.enterPressed()) {
            // Start editing or trigger actions depending on item
            switch (g_current_item) {
                case MENU_MODE: {
                    // Toggle mode
                    g_config.mode = (g_config.mode == DOSE_PER_GALLON)
                        ? DOSE_PER_DAY_AVG
                        : DOSE_PER_GALLON;
                    break;
                }

                case MENU_DOSE_PER_GAL: {
                    g_editing = true;
                    break;
                }

                case MENU_TARGET_PER_DAY: {
                    g_editing = true;
                    break;
                }

                case MENU_PUMP_PRIME: {
                    pumpPrime();
                    break;
                }

                case MENU_RESET_TOTALS: {
                    g_state.total_gallons_today = 0.0f;
                    g_state.cups_dispensed_today = 0.0f;
                    g_state.last_pulse_count = 0;
                    s_pulse_count = 0;
                    g_state.day_start_ms = millis();
                    break;
                }

                default:
                    break;
            }
        }

        if (menuNav.backPressed()) {
            // Long press anywhere = emergency stop
            pumpEmergencyStop();
        }

        return;
    }

    // When editing numeric values, use MenuNavigator::editValue helpers
    switch (g_current_item) {
        case MENU_DOSE_PER_GAL: {
            // Limit Dose/Gal between 0 and 1 cup/gal, step 0.001
            if (!menuNav.editValue(&g_config.dose_per_gallon_cups, 0.0f, 1.0f, 0.001f)) {
                g_editing = false;
                encoder.clearLimits();
            }
            break;
        }

        case MENU_TARGET_PER_DAY: {
            // Limit target/day between 0 and 10 cups/day, step 0.1
            if (!menuNav.editValue(&g_config.target_cups_per_day, 0.0f, 10.0f, 0.1f)) {
                g_editing = false;
                encoder.clearLimits();
            }
            break;
        }

        default:
            g_editing = false;
            break;
    }

    if (menuNav.backPressed()) {
        // Cancel editing via long press
        g_editing = false;
        encoder.clearLimits();
    }
}

// ============================================================================
// SERIAL DEBUG INTERFACE
// ============================================================================

static void printStatus();

static void printHelp() {
    Serial.println();
    Serial.println("=== Dosing Pump Test Menu ===");
    Serial.println("  p - Print status");
    Serial.println("  d - Dump today's usage summary");
    Serial.println("  z - Reset daily totals");
    Serial.println("  s - Emergency stop pump");
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

        case 'd':
        case 'D':
            Serial.println();
            Serial.println("=== DAILY USAGE SUMMARY ===");
            Serial.printf("Mode: %s\n", modeToString(g_config.mode));
            Serial.printf("Dose/Gal base      : %.4f cups/gal\n", g_config.dose_per_gallon_cups);
            Serial.printf("Target cups/day    : %.2f cups\n", g_config.target_cups_per_day);
            Serial.printf("Effective Dose/Gal : %.4f cups/gal\n", g_state.cups_per_gallon_effective);
            Serial.printf("Gallons today      : %.2f gal\n", g_state.total_gallons_today);
            Serial.printf("Cups dispensed     : %.3f cups\n", g_state.cups_dispensed_today);
            Serial.println();
            break;

        case 'z':
        case 'Z':
            g_state.total_gallons_today = 0.0f;
            g_state.cups_dispensed_today = 0.0f;
            g_state.last_pulse_count = 0;
            s_pulse_count = 0;
            g_state.day_start_ms = millis();
            Serial.println("Daily totals reset.");
            break;

        case 's':
        case 'S':
            pumpEmergencyStop();
            Serial.println("Emergency stop: pump halted and driver disabled.");
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
    Serial.printf("Mode               : %s\n", modeToString(g_config.mode));
    Serial.printf("Dose/Gal base      : %.4f cups/gal\n", g_config.dose_per_gallon_cups);
    Serial.printf("Target cups/day    : %.2f cups\n", g_config.target_cups_per_day);
    Serial.printf("Effective Dose/Gal : %.4f cups/gal\n", g_state.cups_per_gallon_effective);
    Serial.printf("Gallons today      : %.2f gal\n", g_state.total_gallons_today);
    Serial.printf("Cups dispensed     : %.3f cups\n", g_state.cups_dispensed_today);
    Serial.printf("Raw pulse count    : %lu pulses\n", (unsigned long)pulses);
    Serial.printf("Pump running       : %s\n", dosingPump.isRunning() ? "YES" : "NO");
    Serial.printf("Pump distanceToGo  : %ld steps\n", dosingPump.distanceToGo());
    Serial.println();
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
    handleSerial();

    maybeStartNewDay();
    processDosingFromWaterMeter();

    handleMenuNavigation();
    pumpUpdate();
    lcdUpdate();

    delay(5);
}


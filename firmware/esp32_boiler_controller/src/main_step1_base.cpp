/**
 * @file main_step1_base.cpp
 * @brief Phase 1: Base System for Columbia CT-6 Boiler Controller
 *
 * This phase establishes the minimum viable hardware control:
 * - NVS configuration (Preferences)
 * - I2C LCD Display
 * - Rotary Encoder (UI input)
 * - Chemical Pumps (Stepper control)
 * - Water Meter input
 * 
 * FreeRTOS tasks used: Control, Measurement, Display.
 * (Excludes sensors, blowdown, logging, network, coprocessor)
 */

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

#include "config.h"
#include "pin_definitions.h"
#include "chemical_pump.h"
#include "water_meter.h"
#include "display.h"
#include "device_manager.h"
#include "encoder.h"
#include "self_test.h"
#include <esp_task_wdt.h>

// ============================================================================
// GLOBAL INSTANCES
// ============================================================================

// System configuration (stored in NVS)
system_config_t systemConfig;

// Runtime state
system_state_t_runtime systemState;

// NVS for persistent storage
Preferences preferences;

// Task handles for FreeRTOS
TaskHandle_t taskControl = NULL;
TaskHandle_t taskMeasurement = NULL;
TaskHandle_t taskDisplay = NULL;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void loadConfiguration();
void saveConfiguration();
void initializeDefaults();
void processInputs();
void updateFeedwaterPumpMonitor();
void saveFeedwaterPumpNVS();

// FreeRTOS task functions
void taskControlLoop(void* parameter);
void taskMeasurementLoop(void* parameter);
void taskDisplayLoop(void* parameter);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  Columbia CT-6 Boiler Controller - Phase 1");
    Serial.printf("  Firmware Version: %s\n", FIRMWARE_VERSION_STRING);
    Serial.println("========================================");
    Serial.println();

    // Initialize I2C (required for Display)
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ);

    // Load configuration from NVS
    loadConfiguration();

    // Initialize device manager (uses enabled_devices from config)
    deviceManager.begin(&systemConfig.enabled_devices);

    // Log boot reason (local only)
    selfTest.logBootReason();

    // Run power-on self-test (updates deviceManager with installed status)
    selfTest.runAll();

    // Arm hardware watchdog (30-second timeout, panic on expiry)
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);  // Subscribe Arduino loop() task

    // Initialize subsystems
    Serial.println("Initializing subsystems...");

    // Display (initialize first for feedback)
    if (!display.begin()) {
        Serial.println("ERROR: Display initialization failed!");
    }
    display.showMessage("Initializing...", "Phase 1 Base");

    // Pump manager
    if (!pumpManager.begin()) {
        Serial.println("ERROR: Pump manager initialization failed!");
    }
    pumpManager.configure(systemConfig.pumps);

    // Water meter manager
    if (!waterMeterManager.begin()) {
        Serial.println("ERROR: Water meter initialization failed!");
    }
    waterMeterManager.configure(systemConfig.meters);
    waterMeterManager.loadAllFromNVS();

    // Initialize feedwater pump monitor input (optocoupler output, external 10k pull-up)
    pinMode(FEEDWATER_PUMP_PIN, INPUT);
    systemState.feedwater_pump_on = false;
    systemState.fw_pump_cycle_count = 0;
    systemState.fw_pump_on_time_sec = 0;
    systemState.fw_pump_current_cycle_ms = 0;
    systemState.fw_pump_last_cycle_sec = 0;
    systemState.fw_pump_last_on_time = 0;

    // Load feedwater pump totals from NVS
    if (preferences.begin(NVS_NAMESPACE, true)) {
        systemState.fw_pump_cycle_count = preferences.getUInt(NVS_KEY_FW_PUMP_CYCLES, 0);
        systemState.fw_pump_on_time_sec = preferences.getUInt(NVS_KEY_FW_PUMP_ONTIME, 0);
        preferences.end();
        Serial.printf("Feedwater pump totals loaded: %u cycles, %u sec on-time\n",
                      systemState.fw_pump_cycle_count, systemState.fw_pump_on_time_sec);
    } else {
        Serial.println("NVS begin failed - feedwater pump totals use defaults (0)");
    }

    // Initialize auxiliary inputs
    pinMode(AUX_INPUT1_PIN, INPUT_PULLUP);

    // Create FreeRTOS tasks
    Serial.println("Creating tasks...");

    if (xTaskCreatePinnedToCore(taskControlLoop, "Control", TASK_STACK_CONTROL, NULL, TASK_PRIORITY_CONTROL, &taskControl, 1) != pdPASS) {
        Serial.println("FATAL: Control task creation failed");
        display.showAlarm("INIT FAIL");
        for (;;) { delay(1000); }
    }
    if (xTaskCreatePinnedToCore(taskMeasurementLoop, "Measurement", TASK_STACK_MEASUREMENT, NULL, TASK_PRIORITY_MEASUREMENT, &taskMeasurement, 1) != pdPASS) {
        Serial.println("FATAL: Measurement task creation failed");
        display.showAlarm("INIT FAIL");
        for (;;) { delay(1000); }
    }
    if (xTaskCreatePinnedToCore(taskDisplayLoop, "Display", TASK_STACK_DISPLAY, NULL, TASK_PRIORITY_DISPLAY, &taskDisplay, 0) != pdPASS) {
        Serial.println("FATAL: Display task creation failed");
        display.showAlarm("INIT FAIL");
        for (;;) { delay(1000); }
    }

    // Initialization complete
    Serial.println("Initialization complete!");
    display.showMessage("Ready", "Phase 1 Running");
    delay(1000);
    display.setScreen(SCREEN_MAIN);
}

// ============================================================================
// MAIN LOOP
// ============================================================================

/**
 * @brief Arduino main loop
 * 
 * Feeds hardware watchdog, updates encoder tick, and processes button inputs.
 * Heavy lifting is delegated to FreeRTOS tasks.
 */
void loop() {
    esp_task_wdt_reset();  // Feed the hardware watchdog
    encoder.setTickMs(millis()); // Feed encoder ISR-safe tick
    processInputs(); // Check for button presses
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ============================================================================
// FREERTOS TASKS
// ============================================================================

/**
 * @brief Core control loop (runs on Core 1)
 * 
 * Periodically updates feed pump status based on water meter flow rates.
 * Monitors feedwater pump status.
 */
void taskControlLoop(void* parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();

        // Feedwater pump monitoring
        updateFeedwaterPumpMonitor();

        // Get water meter data
        uint32_t water_contacts = waterMeterManager.getContactsSinceLast(2);
        float water_volume = waterMeterManager.getVolumeSinceLast(2);

        // Dummy fuzzy rates (Phase 1 has no fuzzy logic)
        float dummy_rates[PUMP_COUNT] = {0.0f, 0.0f, 0.0f};

        // Process pump feed modes
        pumpManager.processFeedModes(false, 0, water_contacts, water_volume, dummy_rates);

        // Update pumps (run steppers)
        pumpManager.update();

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_CONTROL_MS));
    }
}

/**
 * @brief Measurement loop (runs on Core 1)
 * 
 * Reads fast-changing data like water meter contacts.
 */
void taskMeasurementLoop(void* parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();

        // Update water meters
        waterMeterManager.update();

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_MEASUREMENT_MS));
    }
}

/**
 * @brief Display loop (runs on Core 0)
 * 
 * Refreshes UI data continuously.
 */
void taskDisplayLoop(void* parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();

        // Update display
        display.update();

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_DISPLAY_MS));
    }
}

// ============================================================================
// CONFIGURATION MANAGEMENT
// ============================================================================

/**
 * @brief Initializes default system configuration values
 * 
 * Used when NVS is empty or config is corrupted.
 */
void initializeDefaults() {
    memset(&systemConfig, 0, sizeof(system_config_t));

    systemConfig.magic = CONFIG_MAGIC;
    systemConfig.version = CONFIG_VERSION;

    // Pump defaults
    for (int i = 0; i < 3; i++) {
        systemConfig.pumps[i].enabled = true;
        systemConfig.pumps[i].feed_mode = FEED_MODE_DISABLED;
        systemConfig.pumps[i].hoa_mode = HOA_AUTO;
        systemConfig.pumps[i].steps_per_ml = PUMP_DEFAULT_STEPS_PER_ML;
        systemConfig.pumps[i].max_speed = PUMP_DEFAULT_MAX_SPEED;
        systemConfig.pumps[i].acceleration = PUMP_DEFAULT_ACCELERATION;
    }
    strncpy(systemConfig.pumps[0].name, "H2SO3", sizeof(systemConfig.pumps[0].name));
    strncpy(systemConfig.pumps[1].name, "NaOH", sizeof(systemConfig.pumps[1].name));
    strncpy(systemConfig.pumps[2].name, "Amine", sizeof(systemConfig.pumps[2].name));

    // Water meter defaults
    for (int i = 0; i < 2; i++) {
        systemConfig.meters[i].type = METER_TYPE_CONTACTOR;
        systemConfig.meters[i].units = 0;  // Gallons
        systemConfig.meters[i].volume_per_contact = WATER_METER_PULSES_PER_GAL;
        systemConfig.meters[i].k_factor = 1.0;
        systemConfig.meters[i].totalizer = 0;
    }
    systemConfig.meters[1].type = METER_TYPE_DISABLED;

    systemConfig.enabled_devices = HW_CONFIG_DEFAULT_ENABLED;
    
    // Display defaults
    systemConfig.led_brightness = LED_BRIGHTNESS;
    systemConfig.display_in_ppm = false;
}

/**
 * @brief Loads the system configuration from Non-Volatile Storage
 */
void loadConfiguration() {
    Serial.println("Loading configuration from NVS...");

    if (!preferences.begin(NVS_NAMESPACE, true)) {
        Serial.println("NVS begin failed (read) - using defaults");
        preferences.end();
        initializeDefaults();
        return;
    }

    size_t config_size = preferences.getBytesLength(NVS_KEY_CONFIG);
    if (config_size == sizeof(system_config_t)) {
        preferences.getBytes(NVS_KEY_CONFIG, &systemConfig, sizeof(system_config_t));
        preferences.end();
        if (systemConfig.magic == CONFIG_MAGIC) {
            Serial.println("Configuration loaded successfully");
            return;
        }
    }
    preferences.end();
    initializeDefaults();
}

/**
 * @brief Saves the system configuration to Non-Volatile Storage
 */
void saveConfiguration() {
    Serial.println("Saving configuration to NVS...");
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        Serial.println("NVS begin failed (write) - configuration not persisted");
        return;
    }
    preferences.putBytes(NVS_KEY_CONFIG, &systemConfig, sizeof(system_config_t));
    preferences.end();
    Serial.println("Configuration saved");
}

// ============================================================================
// INPUT PROCESSING (Rotary Encoder)
// ============================================================================

/**
 * @brief Processes rotary encoder button presses (short and long press detection)
 */
void processInputs() {
    static uint32_t last_button_time = 0;
    static bool last_btn = true;
    static uint32_t btn_press_start = 0;
    static bool long_press_fired = false;

    bool btn = digitalRead(ENCODER_BUTTON_PIN);

    if (millis() - last_button_time < ENCODER_BTN_DEBOUNCE_MS) {
        last_btn = btn;
        return;
    }

    if (!btn && last_btn) {
        btn_press_start = millis();
        long_press_fired = false;
    }

    if (!btn && !long_press_fired && (millis() - btn_press_start >= ENCODER_LONG_PRESS_MS)) {
        long_press_fired = true;
        last_button_time = millis();
        display.toggleMenu();
    }

    if (btn && !last_btn && !long_press_fired) {
        last_button_time = millis();
        display.select();
    }
    last_btn = btn;
}

// ============================================================================
// FEEDWATER PUMP MONITOR
// ============================================================================

/**
 * @brief Monitors the feedwater pump input GPIO and tracks run cycle statistics
 */
void updateFeedwaterPumpMonitor() {
    static uint32_t last_edge_time = 0;
    static bool prev_state = false;
    static uint32_t nvs_save_timer = 0;

    bool raw = (digitalRead(FEEDWATER_PUMP_PIN) == FEEDWATER_PUMP_ACTIVE);
    uint32_t now = millis();

    if (raw != prev_state && (now - last_edge_time >= FEEDWATER_PUMP_DEBOUNCE_MS)) {
        last_edge_time = now;
        if (raw && !systemState.feedwater_pump_on) {
            systemState.feedwater_pump_on = true;
            systemState.fw_pump_last_on_time = now;
            systemState.fw_pump_cycle_count++;
            Serial.printf("[FW PUMP] ON  — cycle #%u\n", systemState.fw_pump_cycle_count);
        }
        else if (!raw && systemState.feedwater_pump_on) {
            uint32_t cycle_ms = now - systemState.fw_pump_last_on_time;
            uint32_t cycle_sec = cycle_ms / 1000;
            systemState.feedwater_pump_on = false;
            systemState.fw_pump_last_cycle_sec = cycle_sec;
            systemState.fw_pump_on_time_sec += cycle_sec;
            systemState.fw_pump_current_cycle_ms = 0;
            Serial.printf("[FW PUMP] OFF — ran %u sec\n", cycle_sec);
        }
        prev_state = raw;
    }

    if (systemState.feedwater_pump_on) {
        systemState.fw_pump_current_cycle_ms = now - systemState.fw_pump_last_on_time;
    }

    if (now - nvs_save_timer >= 300000) {
        nvs_save_timer = now;
        saveFeedwaterPumpNVS();
    }
}

/**
 * @brief Persists feedwater pump totals to NVS to survive reboots
 */
void saveFeedwaterPumpNVS() {
    if (!preferences.begin(NVS_NAMESPACE, false)) return;
    preferences.putUInt(NVS_KEY_FW_PUMP_CYCLES, systemState.fw_pump_cycle_count);
    preferences.putUInt(NVS_KEY_FW_PUMP_ONTIME, systemState.fw_pump_on_time_sec);
    preferences.end();
}

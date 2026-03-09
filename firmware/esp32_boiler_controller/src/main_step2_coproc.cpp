/**
 * @file main_step2_coproc.cpp
 * @brief Phase 2: Coprocessor & Automation for Columbia CT-6 Boiler Controller
 *
 * Builds upon Phase 1 by adding:
 * - RS-485 Coprocessor Link (receives conductivity & temperature telemetry)
 * - Sensor Health Monitoring (tracks telemetry freshness and faults)
 * - Automated Blowdown Control (opens/closes valve based on conductivity)
 * - Alarm Checking & Safe Mode Enforcement (stops dosing/blowdown on fault)
 *
 * FreeRTOS tasks used: Control, Measurement, Display.
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
#include "sensor_health.h"
#include "blowdown.h"
#include "coprocessor_protocol.h"
#include "coprocessor_link.h"
#include <esp_task_wdt.h>

// Force compilation of coprocessor link for Phase 2
#ifndef USE_COPROCESSOR_LINK
#define USE_COPROCESSOR_LINK
#endif

// ============================================================================
// GLOBAL INSTANCES
// ============================================================================

// System configuration (stored in NVS)
system_config_t systemConfig;

// Runtime state
system_state_t_runtime systemState;

// NVS for persistent storage
Preferences preferences;

// RS-485 Coprocessor Link (receives conductivity and temperature from panel)
CoprocessorLink coprocessorLink(Serial2, CP_LINK_DE_RE_PIN);
static bool s_last_blowdown_energized = false;
static bool s_coprocessor_ready = false; 
static const uint32_t COPROC_WAIT_TIMEOUT_MS = 10000;

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
void checkAlarms();

// FreeRTOS task functions
void taskControlLoop(void* parameter);
void taskMeasurementLoop(void* parameter);
void taskDisplayLoop(void* parameter);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  Columbia CT-6 Boiler Controller - Phase 2");
    Serial.printf("  Firmware Version: %s\n", FIRMWARE_VERSION_STRING);
    Serial.println("========================================");
    Serial.println();

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ);

    loadConfiguration();

    deviceManager.begin(&systemConfig.enabled_devices);
    sensorHealth.begin();

    selfTest.logBootReason();
    selfTest.runAll();

    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);

    Serial.println("Initializing subsystems...");

    if (!display.begin()) {
        Serial.println("ERROR: Display initialization failed!");
    }
    display.showMessage("Initializing...", "Phase 2 Coproc");

    // Initialize RS-485 Link
    if (!coprocessorLink.begin(CP_LINK_BAUD)) {
        Serial.println("ERROR: Coprocessor link initialization failed!");
        display.showAlarm("LINK ERROR");
    } else {
        Serial.println("Coprocessor link initialized");
    }

    if (!pumpManager.begin()) {
        Serial.println("ERROR: Pump manager initialization failed!");
    }
    pumpManager.configure(systemConfig.pumps);

    if (!waterMeterManager.begin()) {
        Serial.println("ERROR: Water meter initialization failed!");
    }
    waterMeterManager.configure(systemConfig.meters);
    waterMeterManager.loadAllFromNVS();

    // Initialize Blowdown Controller
    if (!blowdownController.begin()) {
        Serial.println("ERROR: Blowdown controller initialization failed!");
    }
    blowdownController.configure(&systemConfig.blowdown);
    blowdownController.setConductivityConfig(&systemConfig.conductivity);
    s_last_blowdown_energized = false;

    pinMode(FEEDWATER_PUMP_PIN, INPUT);
    systemState.feedwater_pump_on = false;
    systemState.fw_pump_cycle_count = 0;
    systemState.fw_pump_on_time_sec = 0;
    systemState.fw_pump_current_cycle_ms = 0;
    systemState.fw_pump_last_cycle_sec = 0;
    systemState.fw_pump_last_on_time = 0;

    if (preferences.begin(NVS_NAMESPACE, true)) {
        systemState.fw_pump_cycle_count = preferences.getUInt(NVS_KEY_FW_PUMP_CYCLES, 0);
        systemState.fw_pump_on_time_sec = preferences.getUInt(NVS_KEY_FW_PUMP_ONTIME, 0);
        preferences.end();
    }

    pinMode(AUX_INPUT1_PIN, INPUT_PULLUP);

    Serial.println("Creating tasks...");
    if (xTaskCreatePinnedToCore(taskControlLoop, "Control", TASK_STACK_CONTROL, NULL, TASK_PRIORITY_CONTROL, &taskControl, 1) != pdPASS) {
        display.showAlarm("INIT FAIL");
        for (;;) { delay(1000); }
    }
    if (xTaskCreatePinnedToCore(taskMeasurementLoop, "Measurement", TASK_STACK_MEASUREMENT, NULL, TASK_PRIORITY_MEASUREMENT, &taskMeasurement, 1) != pdPASS) {
        display.showAlarm("INIT FAIL");
        for (;;) { delay(1000); }
    }
    if (xTaskCreatePinnedToCore(taskDisplayLoop, "Display", TASK_STACK_DISPLAY, NULL, TASK_PRIORITY_DISPLAY, &taskDisplay, 0) != pdPASS) {
        display.showAlarm("INIT FAIL");
        for (;;) { delay(1000); }
    }

    Serial.println("Initialization complete!");
    display.showMessage("Ready", "Phase 2 Running");
    delay(1000);
    display.setScreen(SCREEN_MAIN);
}

// ============================================================================
// MAIN LOOP
// ============================================================================

/**
 * @brief Arduino main loop
 */
void loop() {
    esp_task_wdt_reset();
    encoder.setTickMs(millis());
    processInputs();
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ============================================================================
// ALARM PROCESSING
// ============================================================================

/**
 * @brief Checks sensor data and system timeouts to update active alarms.
 * Updates display alarm states if faults are detected.
 */
void checkAlarms() {
    uint16_t new_alarms = ALARM_NONE;
    float cond = systemState.conductivity_calibrated;
    uint16_t setpoint = systemConfig.blowdown.setpoint;

    bool sensor_valid = sensorHealth.isConductivityValid();

    if (sensor_valid) {
        if (systemConfig.alarms.use_percent_alarms) {
            float high_threshold = setpoint * (1.0 + systemConfig.alarms.cond_high_percent / 100.0);
            float low_threshold = setpoint * (1.0 - systemConfig.alarms.cond_low_percent / 100.0);

            if (cond > high_threshold && systemConfig.alarms.cond_high_percent > 0) new_alarms |= ALARM_COND_HIGH;
            if (cond < low_threshold && systemConfig.alarms.cond_low_percent > 0) new_alarms |= ALARM_COND_LOW;
        } else {
            if (cond > systemConfig.alarms.cond_high_absolute && systemConfig.alarms.cond_high_absolute > 0) new_alarms |= ALARM_COND_HIGH;
            if (cond < systemConfig.alarms.cond_low_absolute && systemConfig.alarms.cond_low_absolute > 0) new_alarms |= ALARM_COND_LOW;
        }
    }

    if (blowdownController.isTimeout()) new_alarms |= ALARM_BLOWDOWN_TIMEOUT;

    const cp_link_telemetry_t& t = coprocessorLink.getLastTelemetry();
    if (!t.valid || !t.sensor_ok || !sensor_valid) new_alarms |= ALARM_SENSOR_ERROR;
    if (!t.valid || !t.temp_ok) new_alarms |= ALARM_TEMP_ERROR;
    
    if (!sensorHealth.isMeasurementFresh()) new_alarms |= ALARM_STALE_DATA;
    if (sensorHealth.isInSafeMode()) new_alarms |= ALARM_SAFE_MODE;
    if (deviceManager.isEnabled(DEV_AUX_INPUT_1) && digitalRead(AUX_INPUT1_PIN) == LOW) new_alarms |= ALARM_DRUM_LEVEL_1;
    if (t.valid && t.valve_fault) new_alarms |= ALARM_VALVE_FAULT;

    uint16_t rising_alarms = new_alarms & ~systemState.active_alarms;
    if (rising_alarms & ALARM_COND_HIGH) display.showAlarm("HIGH CONDUCTIVITY");
    if (rising_alarms & ALARM_COND_LOW) display.showAlarm("LOW CONDUCTIVITY");
    if (rising_alarms & ALARM_BLOWDOWN_TIMEOUT) display.showAlarm("BLOWDOWN TIMEOUT");
    if (rising_alarms & ALARM_SENSOR_ERROR) display.showAlarm("SENSOR ERROR");
    if (rising_alarms & ALARM_STALE_DATA) display.showAlarm("STALE DATA");
    if (rising_alarms & ALARM_SAFE_MODE) display.showAlarm("SAFE MODE");

    uint16_t falling_alarms = systemState.active_alarms & ~new_alarms;
    if (falling_alarms) display.clearAlarm();

    systemState.active_alarms = new_alarms;
    systemState.alarm_active = (new_alarms != ALARM_NONE);
}

// ============================================================================
// FREERTOS TASKS
// ============================================================================

/**
 * @brief Core control loop (runs on Core 1)
 * 
 * Main automation logic loop. It polls the coprocessor for telemetry,
 * evaluates safe mode, updates the blowdown valve state based on conductivity,
 * and processes chemical pump feed modes.
 */
void taskControlLoop(void* parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();

        // 1. Sensor Health & Telemetry Check
        sensorHealth.update();
        coprocessorLink.poll();

        if (coprocessorLink.isCommsLost()) {
            sensorHealth.reportCommsLost(true);
        } else {
            const cp_link_telemetry_t& t = coprocessorLink.getLastTelemetry();
            if (t.valid) {
                if (!s_coprocessor_ready) s_coprocessor_ready = true;
                sensorHealth.reportCommsLost(false);
                sensorHealth.reportMeasurementCycle();
                sensorHealth.reportConductivityOK(t.conductivity_uS_cm);
                sensorHealth.reportTemperatureOK(t.temperature_c);
                systemState.conductivity_calibrated = t.conductivity_uS_cm;
                systemState.temperature_celsius = t.temperature_c;
                systemState.conductivity_raw = t.conductivity_uS_cm;
                systemState.conductivity_compensated = t.conductivity_uS_cm;
            } else {
                sensorHealth.reportCommsLost(true);
            }
        }

        if (!s_coprocessor_ready && (millis() >= COPROC_WAIT_TIMEOUT_MS)) {
            s_coprocessor_ready = true;
        }

        // 2. Safe Mode Enforcement
        if (sensorHealth.isInSafeMode() || !s_coprocessor_ready) {
            blowdownController.closeValve();
            pumpManager.stopAll();

            if (!coprocessorLink.isCommsLost()) {
                coprocessorLink.sendBlowdownClose();
            }
            s_last_blowdown_energized = false;

            updateFeedwaterPumpMonitor();
            checkAlarms();
            vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_CONTROL_MS));
            continue;
        }

        // 3. Normal Operation Updates
        updateFeedwaterPumpMonitor();

        float conductivity = systemState.conductivity_calibrated;
        
        // Update Blowdown logic
        blowdownController.update(conductivity);

        // Send blowdown command to panel if state changed
        bool energized = blowdownController.getStatus().relay_energized;
        if (energized != s_last_blowdown_energized) {
            s_last_blowdown_energized = energized;
            if (!coprocessorLink.isCommsLost()) {
                if (energized) coprocessorLink.sendBlowdownOpen();
                else coprocessorLink.sendBlowdownClose();
            }
        }

        uint32_t water_contacts = waterMeterManager.getContactsSinceLast(2);
        float water_volume = waterMeterManager.getVolumeSinceLast(2);
        float dummy_rates[PUMP_COUNT] = {0.0f, 0.0f, 0.0f};

        // Update pumps and alarms
        pumpManager.processFeedModes(blowdownController.isActive(), blowdownController.getAccumulatedTime(), water_contacts, water_volume, dummy_rates);
        pumpManager.update();
        checkAlarms();

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_CONTROL_MS));
    }
}

/**
 * @brief Measurement loop (runs on Core 1)
 */
void taskMeasurementLoop(void* parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();
        waterMeterManager.update();
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_MEASUREMENT_MS));
    }
}

/**
 * @brief Display loop (runs on Core 0)
 */
void taskDisplayLoop(void* parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();
        display.update();
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_DISPLAY_MS));
    }
}

// ============================================================================
// UTILITY FUNCTIONS (Configuration, Inputs, Pump Monitor)
// ============================================================================

void initializeDefaults() {
    memset(&systemConfig, 0, sizeof(system_config_t));
    systemConfig.magic = CONFIG_MAGIC;
    systemConfig.version = CONFIG_VERSION;

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

    for (int i = 0; i < 2; i++) {
        systemConfig.meters[i].type = METER_TYPE_CONTACTOR;
        systemConfig.meters[i].volume_per_contact = WATER_METER_PULSES_PER_GAL;
        systemConfig.meters[i].k_factor = 1.0;
        systemConfig.meters[i].totalizer = 0;
    }
    systemConfig.meters[1].type = METER_TYPE_DISABLED;

    systemConfig.blowdown.setpoint = BLOW_DEFAULT_SETPOINT;
    systemConfig.blowdown.deadband = BLOW_DEFAULT_DEADBAND;
    systemConfig.blowdown.time_limit_seconds = BLOW_DEFAULT_TIME_LIMIT;
    systemConfig.blowdown.control_direction = BLOW_DEFAULT_DIRECTION;
    systemConfig.blowdown.ball_valve_delay = BLOW_DEFAULT_VALVE_DELAY;
    systemConfig.blowdown.hoa_mode = HOA_AUTO;

    systemConfig.enabled_devices = HW_CONFIG_DEFAULT_ENABLED;
    systemConfig.led_brightness = LED_BRIGHTNESS;
    systemConfig.display_in_ppm = false;
}

void loadConfiguration() {
    if (!preferences.begin(NVS_NAMESPACE, true)) {
        preferences.end();
        initializeDefaults();
        return;
    }
    size_t config_size = preferences.getBytesLength(NVS_KEY_CONFIG);
    if (config_size == sizeof(system_config_t)) {
        preferences.getBytes(NVS_KEY_CONFIG, &systemConfig, sizeof(system_config_t));
        preferences.end();
        if (systemConfig.magic == CONFIG_MAGIC) return;
    }
    preferences.end();
    initializeDefaults();
}

void saveConfiguration() {
    if (!preferences.begin(NVS_NAMESPACE, false)) return;
    preferences.putBytes(NVS_KEY_CONFIG, &systemConfig, sizeof(system_config_t));
    preferences.end();
}

void processInputs() {
    static uint32_t last_button_time = 0;
    static bool last_btn = true;
    static uint32_t btn_press_start = 0;
    static bool long_press_fired = false;

    bool btn = digitalRead(ENCODER_BUTTON_PIN);
    if (millis() - last_button_time < ENCODER_BTN_DEBOUNCE_MS) { last_btn = btn; return; }

    if (!btn && last_btn) { btn_press_start = millis(); long_press_fired = false; }
    if (!btn && !long_press_fired && (millis() - btn_press_start >= ENCODER_LONG_PRESS_MS)) {
        long_press_fired = true; last_button_time = millis(); display.toggleMenu();
    }
    if (btn && !last_btn && !long_press_fired) {
        last_button_time = millis(); display.select();
    }
    last_btn = btn;
}

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
        }
        else if (!raw && systemState.feedwater_pump_on) {
            uint32_t cycle_ms = now - systemState.fw_pump_last_on_time;
            uint32_t cycle_sec = cycle_ms / 1000;
            systemState.feedwater_pump_on = false;
            systemState.fw_pump_last_cycle_sec = cycle_sec;
            systemState.fw_pump_on_time_sec += cycle_sec;
            systemState.fw_pump_current_cycle_ms = 0;
        }
        prev_state = raw;
    }

    if (systemState.feedwater_pump_on) systemState.fw_pump_current_cycle_ms = now - systemState.fw_pump_last_on_time;
    if (now - nvs_save_timer >= 300000) { nvs_save_timer = now; saveFeedwaterPumpNVS(); }
}

void saveFeedwaterPumpNVS() {
    if (!preferences.begin(NVS_NAMESPACE, false)) return;
    preferences.putUInt(NVS_KEY_FW_PUMP_CYCLES, systemState.fw_pump_cycle_count);
    preferences.putUInt(NVS_KEY_FW_PUMP_ONTIME, systemState.fw_pump_on_time_sec);
    preferences.end();
}

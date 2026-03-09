/**
 * @file main_step3_logging.cpp
 * @brief Phase 3: Data Logging & Connectivity for Columbia CT-6 Boiler Controller
 *
 * Builds upon Phase 2 by adding:
 * - WiFi Connectivity
 * - SD Card Logging (via SPI, includes mutex for bus sharing if needed)
 * - TimescaleDB Logging (remote telemetry storage)
 * - FreeRTOS Logging Task
 *
 * FreeRTOS tasks used: Control, Measurement, Display, Logging.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>

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
#include "data_logger.h"
#include "sd_logger.h"
#include <esp_task_wdt.h>

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

// RS-485 Coprocessor Link
CoprocessorLink coprocessorLink(Serial2, CP_LINK_DE_RE_PIN);
static bool s_last_blowdown_energized = false;
static bool s_coprocessor_ready = false; 
static const uint32_t COPROC_WAIT_TIMEOUT_MS = 10000;

// SPI bus mutex for SD card
SemaphoreHandle_t spiMutex = NULL;

// Task handles for FreeRTOS
TaskHandle_t taskControl = NULL;
TaskHandle_t taskMeasurement = NULL;
TaskHandle_t taskDisplay = NULL;
TaskHandle_t taskLogging = NULL;

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
void logSensorData();

// FreeRTOS task functions
void taskControlLoop(void* parameter);
void taskMeasurementLoop(void* parameter);
void taskDisplayLoop(void* parameter);
void taskLoggingLoop(void* parameter);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  Columbia CT-6 Boiler Controller - Phase 3");
    Serial.printf("  Firmware Version: %s\n", FIRMWARE_VERSION_STRING);
    Serial.println("========================================");
    Serial.println();

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ);

    // Initialize shared VSPI bus
    SPI.begin(MAX31865_SCK_PIN, MAX31865_MISO_PIN, MAX31865_MOSI_PIN);
    spiMutex = xSemaphoreCreateMutex();
    if (spiMutex == NULL) {
        Serial.println("FATAL: SPI mutex creation failed");
        for (;;) { delay(1000); }
    }

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
    display.showMessage("Initializing...", "Phase 3 Logging");

    if (!coprocessorLink.begin(CP_LINK_BAUD)) {
        display.showAlarm("LINK ERROR");
    }

    pumpManager.begin();
    pumpManager.configure(systemConfig.pumps);

    waterMeterManager.begin();
    waterMeterManager.configure(systemConfig.meters);
    waterMeterManager.loadAllFromNVS();

    blowdownController.begin();
    blowdownController.configure(&systemConfig.blowdown);
    blowdownController.setConductivityConfig(&systemConfig.conductivity);
    s_last_blowdown_energized = false;

    // Data logger initialization
    if (!dataLogger.begin(&systemConfig)) {
        Serial.println("WARNING: Data logger initialization failed!");
    }

    // Connect to WiFi
    if (strlen(systemConfig.wifi_ssid) > 0) {
        display.showMessage("Connecting WiFi...", systemConfig.wifi_ssid);
        if (dataLogger.connectWiFi()) {
            display.showMessage("WiFi Connected!", WiFi.localIP().toString().c_str());
            delay(1000);
        } else {
            display.showMessage("WiFi Failed", "Running offline");
            delay(2000);
        }
    }

    // SD card logger
    if (!sdLogger.begin(SD_CS_PIN, SPI, spiMutex)) {
        Serial.println("WARNING: SD card not available — logging to WiFi/buffer only");
        deviceManager.setInstalled(DEV_SD_CARD, false);
    } else {
        display.showMessage("SD Card OK", sdLogger.getCurrentFilename());
        deviceManager.setInstalled(DEV_SD_CARD, true);
        delay(500);
    }

    if (sdLogger.isAvailable()) {
        sdLogger.logEvent("BOOT", selfTest.getResetReasonString(), (int32_t)esp_reset_reason());
    }

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
        display.showAlarm("INIT FAIL"); for (;;) { delay(1000); }
    }
    if (xTaskCreatePinnedToCore(taskMeasurementLoop, "Measurement", TASK_STACK_MEASUREMENT, NULL, TASK_PRIORITY_MEASUREMENT, &taskMeasurement, 1) != pdPASS) {
        display.showAlarm("INIT FAIL"); for (;;) { delay(1000); }
    }
    if (xTaskCreatePinnedToCore(taskDisplayLoop, "Display", TASK_STACK_DISPLAY, NULL, TASK_PRIORITY_DISPLAY, &taskDisplay, 0) != pdPASS) {
        display.showAlarm("INIT FAIL"); for (;;) { delay(1000); }
    }
    if (xTaskCreatePinnedToCore(taskLoggingLoop, "Logging", TASK_STACK_LOGGING, NULL, TASK_PRIORITY_LOGGING, &taskLogging, 0) != pdPASS) {
        display.showAlarm("INIT FAIL"); for (;;) { delay(1000); }
    }

    Serial.println("Initialization complete!");
    display.showMessage("Ready", "Phase 3 Running");
    delay(1000);
    display.setScreen(SCREEN_MAIN);
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    esp_task_wdt_reset();
    encoder.setTickMs(millis());
    processInputs();
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ============================================================================
// DATA LOGGING
// ============================================================================

/**
 * @brief Logs current sensor readings to SD card and TimescaleDB buffer.
 */
void logSensorData() {
    sensor_reading_t reading;

    reading.timestamp = dataLogger.getTimestamp();
    reading.conductivity = systemState.conductivity_calibrated;
    reading.temperature = systemState.temperature_celsius;
    reading.water_meter1 = waterMeterManager.getMeter(0)->getTotalVolume();
    reading.water_meter2 = waterMeterManager.getMeter(1)->getTotalVolume();
    reading.flow_rate = waterMeterManager.getCombinedFlowRate();
    reading.blowdown_active = blowdownController.isActive();
    reading.valve_position_mA = blowdownController.getFeedbackmA();
    reading.pump1_active = pumpManager.getPump(PUMP_H2SO3)->isRunning();
    reading.pump2_active = pumpManager.getPump(PUMP_NAOH)->isRunning();
    reading.pump3_active = pumpManager.getPump(PUMP_AMINE)->isRunning();
    reading.feedwater_pump_on = systemState.feedwater_pump_on;
    reading.fw_pump_cycle_count = systemState.fw_pump_cycle_count;
    reading.fw_pump_on_time_sec = systemState.fw_pump_on_time_sec;
    reading.active_alarms = systemState.active_alarms;

    reading.safe_mode = (uint8_t)sensorHealth.getSafeMode();
    reading.cond_sensor_valid = sensorHealth.isConductivityValid();
    reading.temp_sensor_valid = sensorHealth.isTemperatureValid();
    reading.devices_operational = deviceManager.countOperational();
    reading.devices_faulted = deviceManager.countFaulted();
    reading.devices_faulted_mask = deviceManager.getFaultedMask();
    reading.measurement_age_ms = sensorHealth.getMeasurementAge();

    // Log to TimescaleDB (via WiFi) and/or RAM buffer
    dataLogger.logReading(&reading);

    // Log to SD card
    sdLogger.logReading(&reading);
}

// ============================================================================
// ALARM PROCESSING
// ============================================================================

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
    
    if (rising_alarms & ALARM_COND_HIGH) {
        dataLogger.logAlarm(ALARM_COND_HIGH, "HIGH CONDUCTIVITY", true, cond);
        display.showAlarm("HIGH CONDUCTIVITY");
    }
    if (rising_alarms & ALARM_COND_LOW) {
        dataLogger.logAlarm(ALARM_COND_LOW, "LOW CONDUCTIVITY", true, cond);
        display.showAlarm("LOW CONDUCTIVITY");
    }
    if (rising_alarms & ALARM_BLOWDOWN_TIMEOUT) {
        dataLogger.logAlarm(ALARM_BLOWDOWN_TIMEOUT, "BLOWDOWN TIMEOUT", true, 0);
        display.showAlarm("BLOWDOWN TIMEOUT");
    }
    if (rising_alarms & ALARM_SENSOR_ERROR) {
        dataLogger.logAlarm(ALARM_SENSOR_ERROR, "SENSOR ERROR", true, 0);
        display.showAlarm("SENSOR ERROR");
    }
    if (rising_alarms & ALARM_STALE_DATA) {
        dataLogger.logAlarm(ALARM_STALE_DATA, "STALE DATA", true, (float)sensorHealth.getMeasurementAge());
        display.showAlarm("STALE DATA");
    }
    if (rising_alarms & ALARM_SAFE_MODE) {
        dataLogger.logAlarm(ALARM_SAFE_MODE, "SAFE MODE", true, (float)sensorHealth.getSafeMode());
        display.showAlarm("SAFE MODE");
    }

    uint16_t falling_alarms = systemState.active_alarms & ~new_alarms;
    if (falling_alarms) display.clearAlarm();

    systemState.active_alarms = new_alarms;
    systemState.alarm_active = (new_alarms != ALARM_NONE);
}

// ============================================================================
// FREERTOS TASKS
// ============================================================================

void taskControlLoop(void* parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();
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

        if (sensorHealth.isInSafeMode() || !s_coprocessor_ready) {
            blowdownController.closeValve();
            pumpManager.stopAll();

            if (!coprocessorLink.isCommsLost()) coprocessorLink.sendBlowdownClose();
            s_last_blowdown_energized = false;

            updateFeedwaterPumpMonitor();
            checkAlarms();
            vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_CONTROL_MS));
            continue;
        }

        updateFeedwaterPumpMonitor();
        float conductivity = systemState.conductivity_calibrated;
        
        blowdownController.update(conductivity);

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

        pumpManager.processFeedModes(blowdownController.isActive(), blowdownController.getAccumulatedTime(), water_contacts, water_volume, dummy_rates);
        pumpManager.update();
        checkAlarms();

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_CONTROL_MS));
    }
}

void taskMeasurementLoop(void* parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();
    while (true) {
        esp_task_wdt_reset();
        waterMeterManager.update();
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_MEASUREMENT_MS));
    }
}

void taskDisplayLoop(void* parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();
    while (true) {
        esp_task_wdt_reset();
        display.update();
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_DISPLAY_MS));
    }
}

/**
 * @brief Logging loop (runs on Core 0)
 * 
 * Periodically captures system state and flushes it to local SD card 
 * and remote TimescaleDB logging targets.
 */
void taskLoggingLoop(void* parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();
    uint32_t lastLogTime = 0;

    while (true) {
        esp_task_wdt_reset();

        // Maintain WiFi connection and dispatch network buffers
        dataLogger.update();

        // Flush SD buffers
        sdLogger.update();

        uint32_t now = millis();
        if (now - lastLogTime >= systemConfig.log_interval_ms) {
            lastLogTime = now;
            logSensorData();
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_LOGGING_MS));
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

    systemConfig.tsdb_port = TSDB_HTTP_PORT;
    systemConfig.log_interval_ms = TSDB_LOG_INTERVAL_MS;

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
            dataLogger.logEvent("FW_PUMP_ON", "Feedwater pump started", systemState.fw_pump_cycle_count);
            if (sdLogger.isAvailable()) sdLogger.logEvent("FW_PUMP_ON", "Feedwater pump started", systemState.fw_pump_cycle_count);
        }
        else if (!raw && systemState.feedwater_pump_on) {
            uint32_t cycle_ms = now - systemState.fw_pump_last_on_time;
            uint32_t cycle_sec = cycle_ms / 1000;
            systemState.feedwater_pump_on = false;
            systemState.fw_pump_last_cycle_sec = cycle_sec;
            systemState.fw_pump_on_time_sec += cycle_sec;
            systemState.fw_pump_current_cycle_ms = 0;
            dataLogger.logEvent("FW_PUMP_OFF", "Feedwater pump stopped", cycle_sec);
            if (sdLogger.isAvailable()) sdLogger.logEvent("FW_PUMP_OFF", "Feedwater pump stopped", cycle_sec);
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

/**
 * @file main.cpp
 * @brief Main Application for Columbia CT-6 Boiler Dosing Controller
 *
 * ESP32-based boiler water treatment controller implementing features from:
 * - Lakewood Instruments Model 1575e
 * - Walchem WBL400/410 Series
 *
 * Hardware:
 * - 3x Nema17 Stepper Motors with A4988 Drivers (Chemical Pumps)
 * - Sensorex CS675HTTC/P1K Conductivity Probe via Atlas Scientific EZO-EC (UART)
 * - PT1000 RTD Temperature Sensor via Adafruit MAX31865 (SPI)
 * - Water Meter Input (1 pulse per gallon)
 * - 20x4 I2C LCD Display
 * - WS2812 RGB LED Status Indicators
 * - Automated Blowdown Valve
 * - WiFi for TimescaleDB/Grafana Integration
 */

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <SPI.h>

#include "config.h"
#include "pin_definitions.h"
#include "conductivity.h"
#include "chemical_pump.h"
#include "water_meter.h"
#include "blowdown.h"
#include "display.h"
#include "data_logger.h"
#include "sd_logger.h"
#include "web_server.h"
#include "fuzzy_logic.h"
#include "device_manager.h"
#include "self_test.h"
#include "sensor_health.h"
#include <esp_task_wdt.h>

#ifdef USE_COPROCESSOR_LINK
#include "coprocessor_link.h"
#endif

// ============================================================================
// GLOBAL INSTANCES
// ============================================================================

// Conductivity sensor (Atlas Scientific EZO-EC via UART + Adafruit MAX31865 PT1000 RTD via hardware VSPI)
// When USE_COPROCESSOR_LINK: Serial2 is used for RS-485 link; EZO/MAX31865 are on the panel.
#ifndef USE_COPROCESSOR_LINK
ConductivitySensor conductivitySensor(
    Serial2,
    EZO_EC_RX_PIN, EZO_EC_TX_PIN,
    MAX31865_CS_PIN  // Hardware SPI — shares VSPI bus with SD card
);
#endif

#ifdef USE_COPROCESSOR_LINK
CoprocessorLink coprocessorLink(Serial2, CP_LINK_DE_RE_PIN);
static bool s_last_blowdown_energized = false;  // For command-on-change only
static bool s_coprocessor_ready = false;         // First valid telemetry or timeout
static const uint32_t COPROC_WAIT_TIMEOUT_MS = 10000;  // Enter normal after first telemetry or 10 s
#endif

// System configuration (stored in NVS)
system_config_t systemConfig;

// Runtime state
system_state_t_runtime systemState;

// NVS for persistent storage
Preferences preferences;

// SPI bus mutex (shared VSPI: MAX31865 + SD card)
SemaphoreHandle_t spiMutex = NULL;

// Task handles for FreeRTOS
TaskHandle_t taskControl = NULL;
TaskHandle_t taskMeasurement = NULL;
TaskHandle_t taskDisplay = NULL;
TaskHandle_t taskLogging = NULL;

// Conductivity history for trend (rate of change µS/cm per minute)
#define COND_HISTORY_MIN_MS 60000   // Min 1 minute between samples for trend
static float s_cond_history_value = 0.0f;
static uint32_t s_cond_history_time = 0;
static bool s_cond_history_valid = false;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void loadConfiguration();
void saveConfiguration();
void initializeDefaults();
void checkAlarms();
void processInputs();
void logSensorData();
void updateFeedwaterPumpMonitor();
void saveFeedwaterPumpNVS();

// FreeRTOS task functions
void taskControlLoop(void* parameter);
void taskMeasurementLoop(void* parameter);
void taskDisplayLoop(void* parameter);
void taskLoggingLoop(void* parameter);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  Columbia CT-6 Boiler Controller");
    Serial.printf("  Firmware Version: %s\n", FIRMWARE_VERSION_STRING);
    Serial.printf("  Build Date: %s %s\n", FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);
    Serial.println("========================================");
    Serial.println();

    // Initialize I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ);

    // Initialize shared VSPI bus (MAX31865 + SD card)
    SPI.begin(MAX31865_SCK_PIN, MAX31865_MISO_PIN, MAX31865_MOSI_PIN);
    spiMutex = xSemaphoreCreateMutex();
    Serial.printf("VSPI bus: SCK=%d, MISO=%d, MOSI=%d (shared: MAX31865 + SD)\n",
                  MAX31865_SCK_PIN, MAX31865_MISO_PIN, MAX31865_MOSI_PIN);

    // Load configuration from NVS
    loadConfiguration();

    // Initialize device manager (uses enabled_devices from config)
    deviceManager.begin(&systemConfig.enabled_devices);

    // Initialize sensor health monitor
    sensorHealth.begin();

    // Log boot reason
    selfTest.logBootReason();

    // Run power-on self-test (updates deviceManager with installed status)
    self_test_result_t post_result = selfTest.runAll();

    // Arm hardware watchdog (30-second timeout, panic on expiry)
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);  // Subscribe Arduino loop() task

    // Initialize subsystems
    Serial.println("Initializing subsystems...");

    // Display (initialize first for feedback)
    if (!display.begin()) {
        Serial.println("ERROR: Display initialization failed!");
    }
    display.showMessage("Initializing...", "Please wait");

#ifdef USE_COPROCESSOR_LINK
    // RS-485 link to panel (conductivity/temp on panel)
    if (!coprocessorLink.begin(CP_LINK_BAUD)) {
        Serial.println("ERROR: Coprocessor link initialization failed!");
        display.showAlarm("LINK ERROR");
    } else {
        Serial.println("Coprocessor link initialized (2-DevKit mode)");
    }
#else
    // Conductivity sensor (hardware SPI via shared VSPI)
    if (!conductivitySensor.begin()) {
        Serial.println("ERROR: Conductivity sensor initialization failed!");
        display.showAlarm("SENSOR ERROR");
    }
    conductivitySensor.configure(&systemConfig.conductivity);
#endif

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

    // Blowdown controller (local relay; when USE_COPROCESSOR_LINK panel also has valve, we send commands)
    if (!blowdownController.begin()) {
        Serial.println("ERROR: Blowdown controller initialization failed!");
    }
    blowdownController.configure(&systemConfig.blowdown);
    blowdownController.setConductivityConfig(&systemConfig.conductivity);
#ifdef USE_COPROCESSOR_LINK
    s_last_blowdown_energized = false;
#endif

    // Data logger
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

    // Web server (accessible via AP hotspot at 192.168.4.1 and via STA IP)
    if (webServer.begin(&systemConfig, &fuzzyController)) {
        Serial.printf("Web UI: http://%s/ (AP) or http://%s/ (STA)\n",
                      WiFi.softAPIP().toString().c_str(),
                      WiFi.localIP().toString().c_str());
        display.showMessage("Web UI Active", WiFi.softAPIP().toString().c_str());
        delay(500);
    }

    // SD card logger (shares VSPI bus with MAX31865)
    if (!sdLogger.begin(SD_CS_PIN, SPI, spiMutex)) {
        Serial.println("WARNING: SD card not available — logging to WiFi/buffer only");
        deviceManager.setInstalled(DEV_SD_CARD, false);
    } else {
        display.showMessage("SD Card OK", sdLogger.getCurrentFilename());
        deviceManager.setInstalled(DEV_SD_CARD, true);
        delay(500);
    }

    // Log boot event to SD (includes reset reason)
    if (sdLogger.isAvailable()) {
        sdLogger.logEvent("BOOT", selfTest.getResetReasonString(),
                          (int32_t)esp_reset_reason());
    }

    // Initialize feedwater pump monitor input (optocoupler output, external 10k pull-up)
    pinMode(FEEDWATER_PUMP_PIN, INPUT);  // GPIO35: input-only, no internal pull-up
    systemState.feedwater_pump_on = false;
    systemState.fw_pump_cycle_count = 0;
    systemState.fw_pump_on_time_sec = 0;
    systemState.fw_pump_current_cycle_ms = 0;
    systemState.fw_pump_last_cycle_sec = 0;
    systemState.fw_pump_last_on_time = 0;

    // Load feedwater pump totals from NVS
    preferences.begin(NVS_NAMESPACE, true);
    systemState.fw_pump_cycle_count = preferences.getUInt(NVS_KEY_FW_PUMP_CYCLES, 0);
    systemState.fw_pump_on_time_sec = preferences.getUInt(NVS_KEY_FW_PUMP_ONTIME, 0);
    preferences.end();
    Serial.printf("Feedwater pump totals loaded: %u cycles, %u sec on-time\n",
                  systemState.fw_pump_cycle_count, systemState.fw_pump_on_time_sec);

    // Initialize auxiliary inputs
    pinMode(AUX_INPUT1_PIN, INPUT_PULLUP);
    // Note: AUX_INPUT2 (GPIO18) repurposed for MAX31865 SCK

    // Rotary encoder pins are initialized by the encoder module's begin().
    // The encoder push button (GPIO0) is the sole physical button (select/menu).

    // Create FreeRTOS tasks
    Serial.println("Creating tasks...");

    xTaskCreatePinnedToCore(
        taskControlLoop,
        "Control",
        TASK_STACK_CONTROL,
        NULL,
        TASK_PRIORITY_CONTROL,
        &taskControl,
        1  // Core 1
    );

    xTaskCreatePinnedToCore(
        taskMeasurementLoop,
        "Measurement",
        TASK_STACK_MEASUREMENT,
        NULL,
        TASK_PRIORITY_MEASUREMENT,
        &taskMeasurement,
        1  // Core 1
    );

    xTaskCreatePinnedToCore(
        taskDisplayLoop,
        "Display",
        TASK_STACK_DISPLAY,
        NULL,
        TASK_PRIORITY_DISPLAY,
        &taskDisplay,
        0  // Core 0
    );

    xTaskCreatePinnedToCore(
        taskLoggingLoop,
        "Logging",
        TASK_STACK_LOGGING,
        NULL,
        TASK_PRIORITY_LOGGING,
        &taskLogging,
        0  // Core 0
    );

    // Initialization complete
    Serial.println("Initialization complete!");
    display.showMessage("Ready", "System Running");
    delay(1000);
    display.setScreen(SCREEN_MAIN);
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    // Main loop is mostly empty since we use FreeRTOS tasks
    // Handle any non-time-critical operations here

    esp_task_wdt_reset();  // Feed the hardware watchdog

    // Check for button presses
    processInputs();

    // Small delay to prevent watchdog issues
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ============================================================================
// FREERTOS TASKS
// ============================================================================

void taskControlLoop(void* parameter) {
    esp_task_wdt_add(NULL);  // Subscribe this task to watchdog
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();  // Feed the watchdog

        // --- Sensor health check ---
        sensorHealth.update();

#ifdef USE_COPROCESSOR_LINK
        // Poll RS-485 for telemetry; update health and comms-lost state
        coprocessorLink.poll();
        if (coprocessorLink.isCommsLost()) {
            sensorHealth.reportCommsLost(true);
        } else {
            const cp_link_telemetry_t& t = coprocessorLink.getLastTelemetry();
            if (t.valid) {
                if (!s_coprocessor_ready) s_coprocessor_ready = true;  // Boot handshake: first valid telemetry
                sensorHealth.reportCommsLost(false);
                sensorHealth.reportMeasurementCycle();
                sensorHealth.reportConductivityOK(t.conductivity_uS_cm);
                sensorHealth.reportTemperatureOK(t.temperature_c);
            } else {
                sensorHealth.reportCommsLost(true);
            }
        }
        // Wait for first valid telemetry (or timeout) before running normal control
        if (!s_coprocessor_ready && (millis() >= COPROC_WAIT_TIMEOUT_MS)) {
            s_coprocessor_ready = true;  // Timeout: proceed anyway, safe mode will apply if still no telemetry
        }
#endif

        // --- Safe mode enforcement (also when waiting for coprocessor) ---
        if (sensorHealth.isInSafeMode()
#ifdef USE_COPROCESSOR_LINK
            || !s_coprocessor_ready
#endif
        ) {
            blowdownController.closeValve();
            pumpManager.stopAll();
#ifdef USE_COPROCESSOR_LINK
            // Tell panel to close valve when we are in safe mode
            if (!coprocessorLink.isCommsLost()) {
                coprocessorLink.sendBlowdownClose();
            }
            s_last_blowdown_energized = false;
#endif

            // Still monitor feedwater pump and check alarms
            updateFeedwaterPumpMonitor();
            checkAlarms();

            vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_CONTROL_MS));
            continue;  // Skip normal control logic
        }

        // --- Feedwater pump monitoring (GPIO35 via optocoupler) ---
        updateFeedwaterPumpMonitor();

        // Get current conductivity and temperature (local sensors or telemetry)
        float conductivity;
        float temperature_c;
#ifdef USE_COPROCESSOR_LINK
        {
            const cp_link_telemetry_t& t = coprocessorLink.getLastTelemetry();
            if (t.valid) {
                conductivity = t.conductivity_uS_cm;
                temperature_c = t.temperature_c;
                systemState.conductivity_calibrated = t.conductivity_uS_cm;
                systemState.temperature_celsius = t.temperature_c;
                systemState.conductivity_raw = t.conductivity_uS_cm;
                systemState.conductivity_compensated = t.conductivity_uS_cm;
            } else {
                conductivity = 0.0f;
                temperature_c = 0.0f;
            }
        }
#else
        conductivity = conductivitySensor.getLastReading().calibrated;
        temperature_c = conductivitySensor.getLastReading().temperature_c;
#endif

        // Update blowdown control (flow_ok always true — no flow switch installed)
        blowdownController.update(conductivity);

#ifdef USE_COPROCESSOR_LINK
        // Send blowdown open/close to panel on transition only
        bool energized = blowdownController.getStatus().relay_energized;
        if (energized != s_last_blowdown_energized) {
            s_last_blowdown_energized = energized;
            if (!coprocessorLink.isCommsLost()) {
                if (energized)
                    coprocessorLink.sendBlowdownOpen();
                else
                    coprocessorLink.sendBlowdownClose();
            }
        }
#endif

        // Get water meter data for feed modes
        uint32_t water_contacts = waterMeterManager.getContactsSinceLast(2);  // Both meters
        float water_volume = waterMeterManager.getVolumeSinceLast(2);

        // Conductivity trend (µS/cm per minute)
        float cond_trend = 0.0f;
        uint32_t now_ms = millis();
        if (s_cond_history_valid && (now_ms - s_cond_history_time) >= COND_HISTORY_MIN_MS) {
            float dt_min = (now_ms - s_cond_history_time) / 60000.0f;
            if (dt_min > 0.0f) {
                cond_trend = (conductivity - s_cond_history_value) / dt_min;
            }
        }
        s_cond_history_value = conductivity;
        s_cond_history_time = now_ms;
        s_cond_history_valid = true;

        // Build fuzzy inputs from current readings and manual test values
        fuzzy_inputs_t fuzzy_inputs;
        fuzzy_inputs.conductivity = conductivity;
        fuzzy_inputs.temperature = temperature_c;
        fuzzy_inputs.cond_trend = cond_trend;
        // Manual inputs are set via web UI or LCD menu through fuzzyController.setManualInput()
        fuzzy_inputs.alkalinity = 0.0f;
        fuzzy_inputs.sulfite = 0.0f;
        fuzzy_inputs.ph = 0.0f;
        fuzzy_inputs.alkalinity_valid = false;
        fuzzy_inputs.sulfite_valid = false;
        fuzzy_inputs.ph_valid = false;

        // Evaluate fuzzy logic controller
        fuzzy_result_t fuzzy_result = fuzzyController.evaluate(fuzzy_inputs);

        // Map fuzzy outputs to pump array:
        // [0] = PUMP_H2SO3 (acid) → acid_rate
        // [1] = PUMP_NAOH (caustic) → caustic_rate
        // [2] = PUMP_AMINE (sulfite) → sulfite_rate
        float fuzzy_rates[PUMP_COUNT];
        fuzzy_rates[PUMP_H2SO3] = fuzzy_result.acid_rate;
        fuzzy_rates[PUMP_NAOH] = fuzzy_result.caustic_rate;
        fuzzy_rates[PUMP_AMINE] = fuzzy_result.sulfite_rate;

        // Clamp rates so effective ml/min does not exceed configured max (Mode F)
        float flow_gal_per_min = waterMeterManager.getCombinedFlowRate();
        const float max_ml_min[] = {
            systemConfig.fuzzy.acid_max_ml_min,
            systemConfig.fuzzy.caustic_max_ml_min,
            systemConfig.fuzzy.sulfite_max_ml_min
        };
        if (flow_gal_per_min > 0.01f) {
            for (int i = 0; i < PUMP_COUNT; i++) {
                float ml_per_gal = systemConfig.pumps[i].ml_per_gallon_at_100pct;
                if (ml_per_gal > 0 && max_ml_min[i] > 0) {
                    float max_pct = 100.0f * max_ml_min[i] / (flow_gal_per_min * ml_per_gal);
                    if (max_pct < 100.0f && fuzzy_rates[i] > max_pct) {
                        fuzzy_rates[i] = max_pct;
                    }
                }
            }
        }

        // Process pump feed modes with fuzzy rates for Mode F
        pumpManager.processFeedModes(
            blowdownController.isActive(),
            blowdownController.getAccumulatedTime(),
            water_contacts,
            water_volume,
            fuzzy_rates
        );

        // Update pumps (run steppers)
        pumpManager.update();

        // Check alarms
        checkAlarms();

        // Wait for next cycle
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_CONTROL_MS));
    }
}

void taskMeasurementLoop(void* parameter) {
    esp_task_wdt_add(NULL);  // Subscribe this task to watchdog
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();  // Feed the watchdog

#ifndef USE_COPROCESSOR_LINK
        // Read conductivity sensor (acquires shared SPI bus for MAX31865)
        if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            conductivity_reading_t reading = conductivitySensor.read();
            xSemaphoreGive(spiMutex);

            // Update system state
            systemState.conductivity_raw = reading.raw_conductivity;
            systemState.conductivity_compensated = reading.temp_compensated;
            systemState.conductivity_calibrated = reading.calibrated;
            systemState.temperature_celsius = reading.temperature_c;

            // Report to sensor health monitor
            if (reading.sensor_ok) {
                sensorHealth.reportConductivityOK(reading.calibrated);
            } else {
                sensorHealth.reportConductivityFail();
            }

            if (reading.temp_sensor_ok) {
                sensorHealth.reportTemperatureOK(reading.temperature_c);
            } else {
                sensorHealth.reportTemperatureFail();
            }

            // Mark measurement cycle as fresh
            sensorHealth.reportMeasurementCycle();
        }
        // If mutex not acquired, measurement is stale — sensorHealth.update()
        // in the control task will detect this via getMeasurementAge().
#endif
        // When USE_COPROCESSOR_LINK, conductivity/temp come from telemetry in control task.

        // Update water meters
        waterMeterManager.update();

        // Wait for next cycle
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_MEASUREMENT_MS));
    }
}

void taskDisplayLoop(void* parameter) {
    esp_task_wdt_add(NULL);  // Subscribe this task to watchdog
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        esp_task_wdt_reset();  // Feed the watchdog

        // Update display
        display.update();

        // Wait for next cycle
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_DISPLAY_MS));
    }
}

void taskLoggingLoop(void* parameter) {
    esp_task_wdt_add(NULL);  // Subscribe this task to watchdog
    TickType_t lastWakeTime = xTaskGetTickCount();
    uint32_t lastLogTime = 0;

    while (true) {
        esp_task_wdt_reset();  // Feed the watchdog

        // Update data logger (handle WiFi reconnection, etc.)
        dataLogger.update();

        // Update SD logger (periodic flush)
        sdLogger.update();

        // Service web server requests (AP + STA clients)
        webServer.handleClient();
        webServer.checkManualTestExpiry();
        webServer.applyEstimatedPhIfNeeded();
        webServer.updateReadings(
            systemState.conductivity_calibrated,
            systemState.temperature_celsius,
            waterMeterManager.getCombinedFlowRate()
        );

        // Log sensor data at configured interval
        uint32_t now = millis();
        if (now - lastLogTime >= systemConfig.log_interval_ms) {
            lastLogTime = now;
            logSensorData();
        }

        // Wait for next cycle
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_LOGGING_MS));
    }
}

// ============================================================================
// CONFIGURATION MANAGEMENT
// ============================================================================

void loadConfiguration() {
    Serial.println("Loading configuration from NVS...");

    preferences.begin(NVS_NAMESPACE, true);  // Read-only

    // Check if config exists
    size_t config_size = preferences.getBytesLength(NVS_KEY_CONFIG);

    if (config_size == sizeof(system_config_t)) {
        preferences.getBytes(NVS_KEY_CONFIG, &systemConfig, sizeof(system_config_t));

        // Validate magic number
        if (systemConfig.magic != CONFIG_MAGIC) {
            Serial.println("Invalid config magic - initializing defaults");
            initializeDefaults();
        } else {
            Serial.println("Configuration loaded successfully");
        }
    } else {
        Serial.println("No valid configuration found - initializing defaults");
        initializeDefaults();
    }

    preferences.end();
}

void saveConfiguration() {
    Serial.println("Saving configuration to NVS...");

    // Update checksum
    systemConfig.magic = CONFIG_MAGIC;
    systemConfig.version = CONFIG_VERSION;

    preferences.begin(NVS_NAMESPACE, false);  // Read-write
    preferences.putBytes(NVS_KEY_CONFIG, &systemConfig, sizeof(system_config_t));
    preferences.end();

    Serial.println("Configuration saved");
}

void initializeDefaults() {
    memset(&systemConfig, 0, sizeof(system_config_t));

    systemConfig.magic = CONFIG_MAGIC;
    systemConfig.version = CONFIG_VERSION;

    // Conductivity defaults
    systemConfig.conductivity.range_max = COND_DEFAULT_RANGE_MAX;
    systemConfig.conductivity.cell_constant = COND_DEFAULT_CELL_CONSTANT;
    systemConfig.conductivity.ppm_conversion_factor = COND_DEFAULT_PPM_FACTOR;
    systemConfig.conductivity.calibration_percent = COND_DEFAULT_CAL_PERCENT;
    systemConfig.conductivity.units = COND_DEFAULT_UNITS;
    systemConfig.conductivity.temp_comp_enabled = COND_DEFAULT_TEMP_COMP;
    systemConfig.conductivity.temp_comp_coefficient = COND_DEFAULT_TEMP_COEFF;
    systemConfig.conductivity.ezo_output_ec = COND_DEFAULT_EZO_OUTPUT_EC;
    systemConfig.conductivity.ezo_output_tds = COND_DEFAULT_EZO_OUTPUT_TDS;
    systemConfig.conductivity.ezo_output_sal = COND_DEFAULT_EZO_OUTPUT_SAL;
    systemConfig.conductivity.ezo_output_sg = COND_DEFAULT_EZO_OUTPUT_SG;
    systemConfig.conductivity.sample_mode = COND_DEFAULT_SAMPLE_MODE;
    systemConfig.conductivity.interval_seconds = COND_DEFAULT_INTERVAL;
    systemConfig.conductivity.duration_seconds = COND_DEFAULT_DURATION;
    systemConfig.conductivity.hold_time_seconds = COND_DEFAULT_HOLD_TIME;
    systemConfig.conductivity.blow_time_seconds = COND_DEFAULT_BLOW_TIME;
    systemConfig.conductivity.prop_band = COND_DEFAULT_PROP_BAND;
    systemConfig.conductivity.max_prop_time_seconds = COND_DEFAULT_MAX_PROP_TIME;
    systemConfig.conductivity.rtd_nominal = COND_DEFAULT_RTD_NOMINAL;
    systemConfig.conductivity.rtd_reference = COND_DEFAULT_RTD_REFERENCE;
    systemConfig.conductivity.rtd_wires = COND_DEFAULT_RTD_WIRES;

    // Blowdown defaults
    systemConfig.blowdown.setpoint = BLOW_DEFAULT_SETPOINT;
    systemConfig.blowdown.deadband = BLOW_DEFAULT_DEADBAND;
    systemConfig.blowdown.time_limit_seconds = BLOW_DEFAULT_TIME_LIMIT;
    systemConfig.blowdown.control_direction = BLOW_DEFAULT_DIRECTION;
    systemConfig.blowdown.ball_valve_delay = BLOW_DEFAULT_VALVE_DELAY;
    systemConfig.blowdown.hoa_mode = HOA_AUTO;
    systemConfig.blowdown.feedback_enabled = BLOW_DEFAULT_FEEDBACK;

    // Pump defaults
    for (int i = 0; i < 3; i++) {
        systemConfig.pumps[i].enabled = true;
        systemConfig.pumps[i].feed_mode = FEED_MODE_DISABLED;
        systemConfig.pumps[i].hoa_mode = HOA_AUTO;
        systemConfig.pumps[i].steps_per_ml = PUMP_DEFAULT_STEPS_PER_ML;
        systemConfig.pumps[i].max_speed = PUMP_DEFAULT_MAX_SPEED;
        systemConfig.pumps[i].acceleration = PUMP_DEFAULT_ACCELERATION;
    }

    // Pump names
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

    // Second meter disabled by default
    systemConfig.meters[1].type = METER_TYPE_DISABLED;

    // Alarm defaults
    systemConfig.alarms.use_percent_alarms = false;
    systemConfig.alarms.cond_high_absolute = 5000;
    systemConfig.alarms.cond_low_absolute = 0;
    systemConfig.alarms.blowdown_timeout_enabled = true;
    systemConfig.alarms.feed_timeout_enabled = true;
    systemConfig.alarms.sensor_error_enabled = true;

    // Fuzzy logic defaults
    systemConfig.fuzzy.enabled = false;
    systemConfig.fuzzy.cond_setpoint = FUZZY_DEFAULT_COND_SETPOINT;
    systemConfig.fuzzy.alk_setpoint = FUZZY_DEFAULT_ALK_SETPOINT;
    systemConfig.fuzzy.sulfite_setpoint = FUZZY_DEFAULT_SULFITE_SETPOINT;
    systemConfig.fuzzy.ph_setpoint = FUZZY_DEFAULT_PH_SETPOINT;
    systemConfig.fuzzy.cond_deadband = FUZZY_DEFAULT_COND_DEADBAND;
    systemConfig.fuzzy.alk_deadband = FUZZY_DEFAULT_ALK_DEADBAND;
    systemConfig.fuzzy.sulfite_deadband = FUZZY_DEFAULT_SULFITE_DEADBAND;
    systemConfig.fuzzy.ph_deadband = FUZZY_DEFAULT_PH_DEADBAND;
    systemConfig.fuzzy.blowdown_max_sec = 60.0f;
    systemConfig.fuzzy.caustic_max_ml_min = 10.0f;
    systemConfig.fuzzy.sulfite_max_ml_min = 5.0f;
    systemConfig.fuzzy.acid_max_ml_min = 5.0f;
    systemConfig.fuzzy.aggressive_mode = false;
    systemConfig.fuzzy.inference_method = 0;
    systemConfig.fuzzy.defuzz_method = 0;
    systemConfig.fuzzy.manual_input_timeout = 1440;  // 24 h in minutes; 0 = never expire

    // Network defaults
    systemConfig.tsdb_port = TSDB_HTTP_PORT;
    systemConfig.log_interval_ms = TSDB_LOG_INTERVAL_MS;

    // Security defaults
    systemConfig.access_code = 2222;
    systemConfig.access_code_enabled = false;

    // Display defaults
    systemConfig.led_brightness = LED_BRIGHTNESS;
    systemConfig.display_in_ppm = false;

    // Hardware device enable defaults (all devices enabled on first boot)
    systemConfig.enabled_devices = HW_CONFIG_DEFAULT_ENABLED;

    // Save defaults
    saveConfiguration();
}

// ============================================================================
// ALARM PROCESSING
// ============================================================================

void checkAlarms() {
    uint16_t new_alarms = ALARM_NONE;
    float cond = systemState.conductivity_calibrated;
    uint16_t setpoint = systemConfig.blowdown.setpoint;

    // Only evaluate conductivity alarms if sensor data is valid
    bool sensor_valid = sensorHealth.isConductivityValid();

    if (sensor_valid) {
        if (systemConfig.alarms.use_percent_alarms) {
            float high_threshold = setpoint * (1.0 + systemConfig.alarms.cond_high_percent / 100.0);
            float low_threshold = setpoint * (1.0 - systemConfig.alarms.cond_low_percent / 100.0);

            if (cond > high_threshold && systemConfig.alarms.cond_high_percent > 0) {
                new_alarms |= ALARM_COND_HIGH;
            }
            if (cond < low_threshold && systemConfig.alarms.cond_low_percent > 0) {
                new_alarms |= ALARM_COND_LOW;
            }
        } else {
            if (cond > systemConfig.alarms.cond_high_absolute && systemConfig.alarms.cond_high_absolute > 0) {
                new_alarms |= ALARM_COND_HIGH;
            }
            if (cond < systemConfig.alarms.cond_low_absolute && systemConfig.alarms.cond_low_absolute > 0) {
                new_alarms |= ALARM_COND_LOW;
            }
        }
    }

    // Blowdown timeout
    if (blowdownController.isTimeout()) {
        new_alarms |= ALARM_BLOWDOWN_TIMEOUT;
    }

    // Sensor errors (from health monitor, or telemetry flags in coprocessor mode)
#ifdef USE_COPROCESSOR_LINK
    {
        const cp_link_telemetry_t& t = coprocessorLink.getLastTelemetry();
        if (!t.valid || !t.sensor_ok || !sensor_valid) {
            new_alarms |= ALARM_SENSOR_ERROR;
        }
        if (!t.valid || !t.temp_ok) {
            new_alarms |= ALARM_TEMP_ERROR;
        }
    }
#else
    if (!conductivitySensor.isSensorOK() || !sensor_valid) {
        new_alarms |= ALARM_SENSOR_ERROR;
    }
    if (!conductivitySensor.isTempSensorOK()) {
        new_alarms |= ALARM_TEMP_ERROR;
    }
#endif

    // Stale data alarm
    if (!sensorHealth.isMeasurementFresh()) {
        new_alarms |= ALARM_STALE_DATA;
    }

    // Safe mode alarm
    if (sensorHealth.isInSafeMode()) {
        new_alarms |= ALARM_SAFE_MODE;
    }

    // Drum level switch
    if (deviceManager.isEnabled(DEV_AUX_INPUT_1) && digitalRead(AUX_INPUT1_PIN) == LOW) {
        new_alarms |= ALARM_DRUM_LEVEL_1;
    }

    // Blowdown valve fault (4-20mA feedback out of range)
#ifdef USE_COPROCESSOR_LINK
    if (coprocessorLink.getLastTelemetry().valid && coprocessorLink.getLastTelemetry().valve_fault) {
        new_alarms |= ALARM_VALVE_FAULT;
    }
#else
    if (blowdownController.isValveFault()) {
        new_alarms |= ALARM_VALVE_FAULT;
    }
#endif

    // --- Rising edge (new alarms) ---
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
    if (rising_alarms & ALARM_VALVE_FAULT) {
        dataLogger.logAlarm(ALARM_VALVE_FAULT, "VALVE FAULT",
                            true, blowdownController.getFeedbackmA());
        display.showAlarm("VALVE FAULT");
    }
    if (rising_alarms & ALARM_STALE_DATA) {
        dataLogger.logAlarm(ALARM_STALE_DATA, "STALE DATA", true,
                            (float)sensorHealth.getMeasurementAge());
        display.showAlarm("STALE DATA");
    }
    if (rising_alarms & ALARM_SAFE_MODE) {
        dataLogger.logAlarm(ALARM_SAFE_MODE, "SAFE MODE", true,
                            (float)sensorHealth.getSafeMode());
        display.showAlarm("SAFE MODE");
    }

    // --- Falling edge (cleared alarms) — log each individually ---
    uint16_t falling_alarms = systemState.active_alarms & ~new_alarms;

    if (falling_alarms & ALARM_COND_HIGH) {
        dataLogger.logAlarm(ALARM_COND_HIGH, "HIGH CONDUCTIVITY", false, cond);
    }
    if (falling_alarms & ALARM_COND_LOW) {
        dataLogger.logAlarm(ALARM_COND_LOW, "LOW CONDUCTIVITY", false, cond);
    }
    if (falling_alarms & ALARM_BLOWDOWN_TIMEOUT) {
        dataLogger.logAlarm(ALARM_BLOWDOWN_TIMEOUT, "BLOWDOWN TIMEOUT", false, 0);
    }
    if (falling_alarms & ALARM_SENSOR_ERROR) {
        dataLogger.logAlarm(ALARM_SENSOR_ERROR, "SENSOR ERROR", false, 0);
    }
    if (falling_alarms & ALARM_VALVE_FAULT) {
        dataLogger.logAlarm(ALARM_VALVE_FAULT, "VALVE FAULT", false, 0);
    }
    if (falling_alarms & ALARM_STALE_DATA) {
        dataLogger.logAlarm(ALARM_STALE_DATA, "STALE DATA", false, 0);
    }
    if (falling_alarms & ALARM_SAFE_MODE) {
        dataLogger.logAlarm(ALARM_SAFE_MODE, "SAFE MODE", false, 0);
    }

    if (falling_alarms) {
        display.clearAlarm();
    }

    // Update state
    systemState.active_alarms = new_alarms;
    systemState.alarm_active = (new_alarms != ALARM_NONE);
}

// ============================================================================
// INPUT PROCESSING (Rotary Encoder — sole input device)
// ============================================================================
// Rotate CW  → next screen / increment value
// Rotate CCW → prev screen / decrement value
// Short press → select / confirm
// Long press  → enter / exit menu

void processInputs() {
    static uint32_t last_button_time = 0;
    static bool last_btn = true;
    static uint32_t btn_press_start = 0;
    static bool long_press_fired = false;

    // --- Encoder rotation (interrupt-driven count read) ---
    // The encoder module accumulates steps via ISR on ENCODER_PIN_A / _B.
    // TODO: Read encoder delta from encoder module when integrated.
    // For now, stub: display.nextScreen() / display.prevScreen()
    // will be driven by the encoder callback.

    // --- Encoder push button (GPIO0, active LOW) ---
    bool btn = digitalRead(ENCODER_BUTTON_PIN);

    // Debounce
    if (millis() - last_button_time < ENCODER_BTN_DEBOUNCE_MS) {
        last_btn = btn;
        return;
    }

    // Button pressed (falling edge)
    if (!btn && last_btn) {
        btn_press_start = millis();
        long_press_fired = false;
    }

    // Button held — detect long press
    if (!btn && !long_press_fired &&
        (millis() - btn_press_start >= ENCODER_LONG_PRESS_MS)) {
        long_press_fired = true;
        last_button_time = millis();
        // Long press: enter/exit menu
        display.toggleMenu();
    }

    // Button released (rising edge) — short press if long press was not fired
    if (btn && !last_btn && !long_press_fired) {
        last_button_time = millis();
        // Short press: select / confirm
        display.select();
    }

    last_btn = btn;
}

// ============================================================================
// DATA LOGGING
// ============================================================================

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

    // Health & diagnostics
    reading.safe_mode = (uint8_t)sensorHealth.getSafeMode();
    reading.cond_sensor_valid = sensorHealth.isConductivityValid();
    reading.temp_sensor_valid = sensorHealth.isTemperatureValid();
    reading.devices_operational = deviceManager.countOperational();
    reading.devices_faulted = deviceManager.countFaulted();
    reading.devices_faulted_mask = deviceManager.getFaultedMask();
    reading.measurement_age_ms = sensorHealth.getMeasurementAge();

    // Log to TimescaleDB (via WiFi) and/or RAM buffer
    dataLogger.logReading(&reading);

    // Log to SD card (always-on local storage)
    sdLogger.logReading(&reading);
}

// ============================================================================
// FEEDWATER PUMP MONITOR (GPIO35 via PC817 optocoupler)
// ============================================================================
// Tracks CT-6 boiler feedwater pump activation cycles and on-time.
// The 110VAC pump-on indicator light is isolated through an optocoupler
// (PC817 + 2x33kΩ + 1N4007), producing a LOW on GPIO35 when pump runs.

void updateFeedwaterPumpMonitor() {
    static uint32_t last_edge_time = 0;
    static bool prev_state = false;
    static uint32_t nvs_save_timer = 0;

    bool raw = (digitalRead(FEEDWATER_PUMP_PIN) == FEEDWATER_PUMP_ACTIVE);

    // Debounce — ignore edges within FEEDWATER_PUMP_DEBOUNCE_MS of last edge
    uint32_t now = millis();
    if (raw != prev_state && (now - last_edge_time >= FEEDWATER_PUMP_DEBOUNCE_MS)) {
        last_edge_time = now;

        if (raw && !systemState.feedwater_pump_on) {
            // --- Pump just turned ON ---
            systemState.feedwater_pump_on = true;
            systemState.fw_pump_last_on_time = now;
            systemState.fw_pump_cycle_count++;

            Serial.printf("[FW PUMP] ON  — cycle #%u\n", systemState.fw_pump_cycle_count);
            dataLogger.logEvent("FW_PUMP_ON", "Feedwater pump started",
                                systemState.fw_pump_cycle_count);
            sdLogger.logEvent("FW_PUMP_ON", "Feedwater pump started",
                              systemState.fw_pump_cycle_count);
        }
        else if (!raw && systemState.feedwater_pump_on) {
            // --- Pump just turned OFF ---
            uint32_t cycle_ms = now - systemState.fw_pump_last_on_time;
            uint32_t cycle_sec = cycle_ms / 1000;

            systemState.feedwater_pump_on = false;
            systemState.fw_pump_last_cycle_sec = cycle_sec;
            systemState.fw_pump_on_time_sec += cycle_sec;
            systemState.fw_pump_current_cycle_ms = 0;

            Serial.printf("[FW PUMP] OFF — ran %u sec (total: %u sec, %u cycles)\n",
                          cycle_sec, systemState.fw_pump_on_time_sec,
                          systemState.fw_pump_cycle_count);
            dataLogger.logEvent("FW_PUMP_OFF", "Feedwater pump stopped", cycle_sec);
            sdLogger.logEvent("FW_PUMP_OFF", "Feedwater pump stopped", cycle_sec);
        }

        prev_state = raw;
    }

    // Update running cycle duration while pump is on
    if (systemState.feedwater_pump_on) {
        systemState.fw_pump_current_cycle_ms = now - systemState.fw_pump_last_on_time;
    }

    // Persist totals to NVS every 5 minutes
    if (now - nvs_save_timer >= 300000) {
        nvs_save_timer = now;
        saveFeedwaterPumpNVS();
    }
}

void saveFeedwaterPumpNVS() {
    preferences.begin(NVS_NAMESPACE, false);
    preferences.putUInt(NVS_KEY_FW_PUMP_CYCLES, systemState.fw_pump_cycle_count);
    preferences.putUInt(NVS_KEY_FW_PUMP_ONTIME, systemState.fw_pump_on_time_sec);
    preferences.end();
}

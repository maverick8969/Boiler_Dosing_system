/**
 * @file web_server.cpp
 * @brief ESP32 Web Server Implementation
 */

#include "web_server.h"
#include "ph_estimator.h"
#include "device_manager.h"
#include "sensor_health.h"
#include "self_test.h"
#include "sd_logger.h"
#include <WiFi.h>

// Global instance
BoilerWebServer webServer;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

BoilerWebServer::BoilerWebServer()
    : _server(WEB_SERVER_PORT)
    , _config(nullptr)
    , _fuzzy(nullptr)
    , _running(false)
    , _current_conductivity(0)
    , _current_temperature(0)
    , _current_flow_rate(0)
    , _test_input_callback(nullptr)
{
    memset(&_current_fuzzy_result, 0, sizeof(_current_fuzzy_result));
    memset(_manual_tests, 0, sizeof(_manual_tests));
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool BoilerWebServer::begin(system_config_t* config, FuzzyController* fuzzy) {
    _config = config;
    _fuzzy = fuzzy;

    // Setup routes
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
    _server.on("/api/fuzzy", HTTP_GET, [this]() { handleGetFuzzy(); });
    _server.on("/api/devices", HTTP_GET, [this]() { handleGetDevices(); });
    _server.on("/api/sd/status", HTTP_GET, [this]() { handleGetSDStatus(); });
    _server.on("/api/sd/format", HTTP_POST, [this]() { handlePostSDFormat(); });
    _server.on("/api/sd/format", HTTP_OPTIONS, [this]() { sendCORSHeaders(); _server.send(204); });
    _server.on("/api/tests", HTTP_GET, [this]() { handleGetTests(); });
    _server.on("/api/tests", HTTP_POST, [this]() { handlePostTest(); });
    _server.on("/api/tests", HTTP_DELETE, [this]() { handleClearTests(); });
    _server.on("/api/tests", HTTP_OPTIONS, [this]() { sendCORSHeaders(); _server.send(204); });

    _server.onNotFound([this]() { handleNotFound(); });

    _server.begin();
    _running = true;

    Serial.printf("Web server started on port %d\n", WEB_SERVER_PORT);
    Serial.printf("  AP:  http://%s/\n", WiFi.softAPIP().toString().c_str());
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("  STA: http://%s/\n", WiFi.localIP().toString().c_str());
    }

    return true;
}

void BoilerWebServer::handleClient() {
    if (_running) {
        _server.handleClient();
    }
}

void BoilerWebServer::stop() {
    _server.stop();
    _running = false;
}

void BoilerWebServer::setTestInputCallback(void (*callback)(fuzzy_input_t, float, bool)) {
    _test_input_callback = callback;
}

void BoilerWebServer::updateReadings(float conductivity, float temperature, float flow_rate) {
    _current_conductivity = conductivity;
    _current_temperature = temperature;
    _current_flow_rate = flow_rate;
}

void BoilerWebServer::updateFuzzyOutput(const fuzzy_result_t& result) {
    _current_fuzzy_result = result;
}

void BoilerWebServer::checkManualTestExpiry() {
    if (!_config || !_fuzzy) return;

    uint16_t timeout_min = _config->fuzzy.manual_input_timeout;
    if (timeout_min == 0) return;  // Disabled

    const uint32_t timeout_ms = (uint32_t)timeout_min * 60000;
    const fuzzy_input_t param_map[] = {
        FUZZY_IN_TDS, FUZZY_IN_ALKALINITY, FUZZY_IN_SULFITE, FUZZY_IN_PH
    };

    uint32_t now = millis();
    for (int i = 0; i < 4; i++) {
        if (_manual_tests[i].valid && (now - _manual_tests[i].timestamp) > timeout_ms) {
            _manual_tests[i].valid = false;
            _fuzzy->setManualInput(param_map[i], _manual_tests[i].value, false);
            if (_test_input_callback) {
                _test_input_callback(param_map[i], _manual_tests[i].value, false);
            }
        }
    }
    // P-alkalinity [4]: expire validity only (no fuzzy input)
    if (_manual_tests[4].valid && (now - _manual_tests[4].timestamp) > timeout_ms) {
        _manual_tests[4].valid = false;
    }
}

void BoilerWebServer::applyEstimatedPhIfNeeded() {
    if (!_fuzzy) return;
    // If manual pH is set, fuzzy already has it
    if (_manual_tests[3].valid) return;
    // Otherwise try estimate from P- and M-alkalinity
    if (!_manual_tests[1].valid || !_manual_tests[4].valid) {
        _fuzzy->setManualInput(FUZZY_IN_PH, 0, false);
        return;
    }
    float M_ppm = _manual_tests[1].value;
    float P_ppm = _manual_tests[4].value;
    float est_pH = 0.0f;
    if (estimate_pH_from_alkalinity(P_ppm, M_ppm, &est_pH, nullptr, 0)) {
        _fuzzy->setManualInput(FUZZY_IN_PH, est_pH, true);
    } else {
        _fuzzy->setManualInput(FUZZY_IN_PH, 0, false);
    }
}

// ============================================================================
// CORS HEADERS
// ============================================================================

void BoilerWebServer::sendCORSHeaders() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    _server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ============================================================================
// ROUTE HANDLERS
// ============================================================================

void BoilerWebServer::handleRoot() {
    _server.send(200, "text/html", generateIndexHTML());
}

void BoilerWebServer::handleGetStatus() {
    sendCORSHeaders();

    JsonDocument doc;

    doc["conductivity"] = _current_conductivity;
    doc["temperature"] = _current_temperature;
    doc["flow_rate"] = _current_flow_rate;
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();

    // Health & diagnostics
    JsonObject health = doc["health"].to<JsonObject>();
    health["safe_mode"] = sensorHealth.getSafeModeString();
    health["safe_mode_code"] = (uint8_t)sensorHealth.getSafeMode();
    health["in_safe_mode"] = sensorHealth.isInSafeMode();
    health["cond_valid"] = sensorHealth.isConductivityValid();
    health["temp_valid"] = sensorHealth.isTemperatureValid();
    health["feedback_valid"] = sensorHealth.isFeedbackValid();
    health["measurement_fresh"] = sensorHealth.isMeasurementFresh();
    health["measurement_age_ms"] = sensorHealth.getMeasurementAge();
    health["devices_operational"] = deviceManager.countOperational();
    health["devices_faulted"] = deviceManager.countFaulted();

    // Sensor health details
    const sensor_health_t* cond = sensorHealth.getConductivityHealth();
    const sensor_health_t* temp = sensorHealth.getTemperatureHealth();
    JsonObject cond_h = health["conductivity"].to<JsonObject>();
    cond_h["consecutive_failures"] = cond->consecutive_failures;
    cond_h["total_failures"] = cond->total_failures;
    cond_h["faulted"] = cond->faulted;
    cond_h["stale"] = cond->stale;

    JsonObject temp_h = health["temperature"].to<JsonObject>();
    temp_h["consecutive_failures"] = temp->consecutive_failures;
    temp_h["total_failures"] = temp->total_failures;
    temp_h["faulted"] = temp->faulted;
    temp_h["stale"] = temp->stale;

    // POST results
    self_test_result_t post = selfTest.getLastResult();
    JsonObject post_obj = doc["post"].to<JsonObject>();
    post_obj["total_tested"] = post.total_tested;
    post_obj["total_passed"] = post.total_passed;
    post_obj["total_failed"] = post.total_failed;
    post_obj["total_skipped"] = post.total_skipped;
    post_obj["critical_failure"] = post.critical_failure;
    post_obj["reset_reason"] = post.reset_reason ? post.reset_reason : "UNKNOWN";

    // Manual test values
    JsonObject tests = doc["manual_tests"].to<JsonObject>();
    tests["tds"]["value"] = _manual_tests[0].value;
    tests["tds"]["valid"] = _manual_tests[0].valid;
    tests["tds"]["age_min"] = _manual_tests[0].valid ?
        (millis() - _manual_tests[0].timestamp) / 60000 : -1;

    tests["alkalinity"]["value"] = _manual_tests[1].value;
    tests["alkalinity"]["valid"] = _manual_tests[1].valid;
    tests["alkalinity"]["age_min"] = _manual_tests[1].valid ?
        (millis() - _manual_tests[1].timestamp) / 60000 : -1;

    tests["sulfite"]["value"] = _manual_tests[2].value;
    tests["sulfite"]["valid"] = _manual_tests[2].valid;
    tests["sulfite"]["age_min"] = _manual_tests[2].valid ?
        (millis() - _manual_tests[2].timestamp) / 60000 : -1;

    tests["ph"]["value"] = _manual_tests[3].value;
    tests["ph"]["valid"] = _manual_tests[3].valid;
    tests["ph"]["age_min"] = _manual_tests[3].valid ?
        (millis() - _manual_tests[3].timestamp) / 60000 : -1;

    tests["p_alkalinity"]["value"] = _manual_tests[4].value;
    tests["p_alkalinity"]["valid"] = _manual_tests[4].valid;
    tests["p_alkalinity"]["age_min"] = _manual_tests[4].valid ?
        (millis() - _manual_tests[4].timestamp) / 60000 : -1;

    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void BoilerWebServer::handleGetFuzzy() {
    sendCORSHeaders();

    JsonDocument doc;

    doc["enabled"] = _config ? _config->fuzzy.enabled : false;

    // Outputs
    JsonObject outputs = doc["outputs"].to<JsonObject>();
    outputs["blowdown"] = _current_fuzzy_result.blowdown_rate;
    outputs["caustic"] = _current_fuzzy_result.caustic_rate;
    outputs["sulfite"] = _current_fuzzy_result.sulfite_rate;
    outputs["acid"] = _current_fuzzy_result.acid_rate;

    // Diagnostics
    doc["active_rules"] = _current_fuzzy_result.active_rules;
    doc["max_firing"] = _current_fuzzy_result.max_firing_strength;
    doc["dominant_rule"] = _current_fuzzy_result.dominant_rule;

    // Input validity affects confidence (ALL manual inputs)
    int valid_inputs = 0;
    if (_manual_tests[0].valid) valid_inputs++;  // TDS
    if (_manual_tests[1].valid) valid_inputs++;  // Alkalinity
    if (_manual_tests[2].valid) valid_inputs++;  // Sulfite
    if (_manual_tests[3].valid) valid_inputs++;  // pH

    doc["input_count"] = valid_inputs;
    doc["confidence"] = (valid_inputs == 4) ? "HIGH" :
                        (valid_inputs >= 2) ? "MEDIUM" : "LOW";

    // Setpoints for reference
    if (_config) {
        JsonObject setpoints = doc["setpoints"].to<JsonObject>();
        setpoints["tds"] = _config->fuzzy.cond_setpoint;  // TDS target (ppm)
        setpoints["alkalinity"] = _config->fuzzy.alk_setpoint;
        setpoints["sulfite"] = _config->fuzzy.sulfite_setpoint;
        setpoints["ph"] = _config->fuzzy.ph_setpoint;
    }

    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void BoilerWebServer::handleGetDevices() {
    sendCORSHeaders();

    JsonDocument doc;

    doc["count"] = DEV_COUNT;
    doc["operational"] = deviceManager.countOperational();
    doc["faulted"] = deviceManager.countFaulted();
    doc["enabled_mask"] = deviceManager.getEnabledMask();

    JsonArray devices = doc["devices"].to<JsonArray>();
    for (int i = 0; i < DEV_COUNT; i++) {
        device_id_t id = (device_id_t)i;
        const device_info_t* info = deviceManager.getDeviceInfo(id);
        if (!info) continue;

        JsonObject dev = devices.add<JsonObject>();
        dev["id"] = i;
        dev["name"] = info->name;
        dev["state"] = deviceManager.getStateString(id);
        dev["enabled"] = info->enabled;
        dev["required"] = info->required;
        dev["installed"] = info->installed;
        dev["healthy"] = info->healthy;
        dev["fault_count"] = info->fault_count;
        dev["total_faults"] = info->total_faults;
        if (info->last_ok_time > 0) {
            dev["last_ok_age_ms"] = millis() - info->last_ok_time;
        }
        if (info->last_fault_time > 0) {
            dev["last_fault_age_ms"] = millis() - info->last_fault_time;
        }
    }

    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void BoilerWebServer::handleGetSDStatus() {
    sendCORSHeaders();

    JsonDocument doc;

    sd_card_status_t status = sdLogger.getCardStatus();
    doc["available"] = sdLogger.isAvailable();

    switch (status) {
        case SD_STATUS_OK:              doc["status"] = "OK"; break;
        case SD_STATUS_NO_CARD:         doc["status"] = "NO_CARD"; break;
        case SD_STATUS_MOUNT_FAILED:    doc["status"] = "MOUNT_FAILED"; break;
        case SD_STATUS_NOT_INITIALIZED: doc["status"] = "NOT_INITIALIZED"; break;
        default:                        doc["status"] = "UNKNOWN"; break;
    }

    doc["needs_format"] = (status == SD_STATUS_MOUNT_FAILED);

    if (sdLogger.isAvailable()) {
        doc["size_mb"] = sdLogger.getCardSizeMB();
        doc["used_mb"] = sdLogger.getUsedSpaceMB();
        doc["records_today"] = sdLogger.getRecordsToday();
        doc["current_file"] = sdLogger.getCurrentFilename();
    }

    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void BoilerWebServer::handlePostSDFormat() {
    sendCORSHeaders();

    // Require explicit confirmation to prevent accidental data loss
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json",
            "{\"error\":\"POST body required with {\\\"confirm\\\":true}\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _server.arg("plain"));

    if (error || !doc.containsKey("confirm") || doc["confirm"] != true) {
        _server.send(400, "application/json",
            "{\"error\":\"Send {\\\"confirm\\\":true} to format. THIS DESTROYS ALL DATA.\"}");
        return;
    }

    Serial.println("Web API: SD card format requested");

    bool ok = sdLogger.formatCard();

    // Update DeviceManager to reflect new SD card state
    deviceManager.setInstalled(DEV_SD_CARD, ok);

    JsonDocument resp;
    resp["success"] = ok;
    if (ok) {
        resp["message"] = "SD card formatted and mounted successfully";
        resp["size_mb"] = sdLogger.getCardSizeMB();
    } else {
        resp["message"] = "Format failed — check card is inserted and retry";
    }

    String response;
    serializeJson(resp, response);
    _server.send(ok ? 200 : 500, "application/json", response);
}

void BoilerWebServer::handlePostTest() {
    sendCORSHeaders();

    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"No body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _server.arg("plain"));

    if (error) {
        _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    bool updated = false;

    // Update TDS (ppm)
    if (doc.containsKey("tds")) {
        float value = doc["tds"].as<float>();
        if (value >= 0 && value <= 5000) {
            _manual_tests[0].value = value;
            _manual_tests[0].timestamp = millis();
            _manual_tests[0].valid = true;

            if (_fuzzy) {
                _fuzzy->setManualInput(FUZZY_IN_TDS, value, true);
            }
            if (_test_input_callback) {
                _test_input_callback(FUZZY_IN_TDS, value, true);
            }
            updated = true;
        }
    }

    // Update alkalinity
    if (doc.containsKey("alkalinity")) {
        float value = doc["alkalinity"].as<float>();
        if (value >= 0 && value <= 1000) {
            _manual_tests[1].value = value;
            _manual_tests[1].timestamp = millis();
            _manual_tests[1].valid = true;

            if (_fuzzy) {
                _fuzzy->setManualInput(FUZZY_IN_ALKALINITY, value, true);
            }
            if (_test_input_callback) {
                _test_input_callback(FUZZY_IN_ALKALINITY, value, true);
            }
            updated = true;
        }
    }

    // Update sulfite
    if (doc.containsKey("sulfite")) {
        float value = doc["sulfite"].as<float>();
        if (value >= 0 && value <= 100) {
            _manual_tests[2].value = value;
            _manual_tests[2].timestamp = millis();
            _manual_tests[2].valid = true;

            if (_fuzzy) {
                _fuzzy->setManualInput(FUZZY_IN_SULFITE, value, true);
            }
            if (_test_input_callback) {
                _test_input_callback(FUZZY_IN_SULFITE, value, true);
            }
            updated = true;
        }
    }

    // Update pH
    if (doc.containsKey("ph")) {
        float value = doc["ph"].as<float>();
        if (value >= 7.0 && value <= 14.0) {
            _manual_tests[3].value = value;
            _manual_tests[3].timestamp = millis();
            _manual_tests[3].valid = true;

            if (_fuzzy) {
                _fuzzy->setManualInput(FUZZY_IN_PH, value, true);
            }
            if (_test_input_callback) {
                _test_input_callback(FUZZY_IN_PH, value, true);
            }
            updated = true;
        }
    }

    // Update P-alkalinity (ppm as CaCO3, for pH estimate when M-alk also set)
    if (doc.containsKey("p_alkalinity")) {
        float value = doc["p_alkalinity"].as<float>();
        if (value >= 0 && value <= 2000) {
            _manual_tests[4].value = value;
            _manual_tests[4].timestamp = millis();
            _manual_tests[4].valid = true;
            updated = true;
        }
    }

    if (updated) {
        _server.send(200, "application/json", "{\"success\":true}");
    } else {
        _server.send(400, "application/json", "{\"error\":\"No valid values\"}");
    }
}

void BoilerWebServer::handleGetTests() {
    sendCORSHeaders();

    JsonDocument doc;

    doc["tds"]["value"] = _manual_tests[0].value;
    doc["tds"]["valid"] = _manual_tests[0].valid;
    doc["tds"]["age_min"] = _manual_tests[0].valid ?
        (millis() - _manual_tests[0].timestamp) / 60000 : -1;

    doc["alkalinity"]["value"] = _manual_tests[1].value;
    doc["alkalinity"]["valid"] = _manual_tests[1].valid;
    doc["alkalinity"]["age_min"] = _manual_tests[1].valid ?
        (millis() - _manual_tests[1].timestamp) / 60000 : -1;

    doc["sulfite"]["value"] = _manual_tests[2].value;
    doc["sulfite"]["valid"] = _manual_tests[2].valid;
    doc["sulfite"]["age_min"] = _manual_tests[2].valid ?
        (millis() - _manual_tests[2].timestamp) / 60000 : -1;

    doc["ph"]["value"] = _manual_tests[3].value;
    doc["ph"]["valid"] = _manual_tests[3].valid;
    doc["ph"]["age_min"] = _manual_tests[3].valid ?
        (millis() - _manual_tests[3].timestamp) / 60000 : -1;

    doc["p_alkalinity"]["value"] = _manual_tests[4].value;
    doc["p_alkalinity"]["valid"] = _manual_tests[4].valid;
    doc["p_alkalinity"]["age_min"] = _manual_tests[4].valid ?
        (millis() - _manual_tests[4].timestamp) / 60000 : -1;

    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void BoilerWebServer::handleClearTests() {
    sendCORSHeaders();

    for (int i = 0; i < 5; i++) {
        _manual_tests[i].valid = false;
    }

    if (_fuzzy) {
        _fuzzy->setManualInput(FUZZY_IN_TDS, 0, false);
        _fuzzy->setManualInput(FUZZY_IN_ALKALINITY, 0, false);
        _fuzzy->setManualInput(FUZZY_IN_SULFITE, 0, false);
        _fuzzy->setManualInput(FUZZY_IN_PH, 0, false);
    }

    _server.send(200, "application/json", "{\"success\":true}");
}

void BoilerWebServer::handleNotFound() {
    _server.send(404, "text/plain", "Not Found");
}

// ============================================================================
// HTML GENERATION
// ============================================================================

String BoilerWebServer::generateIndexHTML() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Boiler Controller</title>
    <style>
)rawliteral";

    html += generateCSS();

    html += R"rawliteral(
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Boiler Water Test Entry</h1>
            <div class="status-bar" role="status" aria-live="polite" aria-atomic="true">
                <span id="wifi-status" class="status-indicator" aria-hidden="true">●</span>
                <span id="connection-text">Connecting...</span>
                <span id="system-status" class="system-status" aria-live="polite"></span>
            </div>
        </header>

        <!-- Sensor Reading -->
        <section class="card">
            <h2>Sensor Reading</h2>
            <div class="readings-grid three">
                <div class="reading" id="reading-temp">
                    <span class="label">Temperature</span>
                    <span class="value" id="temperature">--</span>
                    <span class="unit">°C</span>
                </div>
                <div class="reading" id="reading-cond">
                    <span class="label">Conductivity</span>
                    <span class="value" id="conductivity">--</span>
                    <span class="unit">µS/cm</span>
                </div>
                <div class="reading" id="reading-flow">
                    <span class="label">Flow</span>
                    <span class="value" id="flow-rate">--</span>
                    <span class="unit">L/min</span>
                </div>
            </div>
        </section>

        <!-- Manual Test Entry -->
        <section class="card">
            <h2>Enter Test Results</h2>
            <form id="test-form" aria-describedby="form-error">
                <div id="form-error" class="form-error" role="alert" aria-live="polite" aria-atomic="true"></div>
                <div class="input-group">
                    <label for="tds">TDS (ppm)</label>
                    <input type="number" id="tds" name="tds"
                           min="0" max="5000" step="1" placeholder="e.g., 2500" aria-describedby="tds-age form-error">
                    <span class="test-age" id="tds-age"></span>
                </div>

                <div class="input-group">
                    <label for="alkalinity">Alkalinity (ppm CaCO₃)</label>
                    <input type="number" id="alkalinity" name="alkalinity"
                           min="0" max="1000" step="1" placeholder="e.g., 350" aria-describedby="alk-age form-error">
                    <span class="test-age" id="alk-age"></span>
                </div>

                <div class="input-group">
                    <label for="sulfite">Sulfite (ppm SO₃)</label>
                    <input type="number" id="sulfite" name="sulfite"
                           min="0" max="100" step="1" placeholder="e.g., 30" aria-describedby="sulf-age form-error">
                    <span class="test-age" id="sulf-age"></span>
                </div>

                <div class="input-group">
                    <label for="ph">pH</label>
                    <input type="number" id="ph" name="ph"
                           min="7" max="14" step="0.1" placeholder="e.g., 11.0" aria-describedby="ph-age form-error">
                    <span class="test-age" id="ph-age"></span>
                </div>

                <div class="button-group">
                    <button type="submit" class="btn btn-primary">Submit Tests</button>
                    <button type="button" class="btn btn-secondary" onclick="clearTests()">Clear All</button>
                </div>
            </form>
        </section>

        <!-- Fuzzy Logic Output -->
        <section class="card">
            <h2>Control Recommendations</h2>
            <div class="confidence-bar">
                <span>Confidence:</span>
                <span id="confidence" class="confidence-badge">--</span>
            </div>
            <div class="output-grid">
                <div class="output">
                    <span class="output-label" id="blowdown-label">Blowdown <span class="rec-badge">REC</span></span>
                    <div class="progress-bar" role="progressbar" aria-labelledby="blowdown-label" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100">
                        <div class="progress" id="blowdown-bar" style="width: 0%"></div>
                    </div>
                    <span class="output-value" id="blowdown-val">0%</span>
                </div>
                <div class="output">
                    <span class="output-label" id="caustic-label">Caustic (NaOH)</span>
                    <div class="progress-bar" role="progressbar" aria-labelledby="caustic-label" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100">
                        <div class="progress caustic" id="caustic-bar" style="width: 0%"></div>
                    </div>
                    <span class="output-value" id="caustic-val">0%</span>
                </div>
                <div class="output">
                    <span class="output-label" id="sulfite-label">Sulfite</span>
                    <div class="progress-bar" role="progressbar" aria-labelledby="sulfite-label" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100">
                        <div class="progress sulfite" id="sulfite-bar" style="width: 0%"></div>
                    </div>
                    <span class="output-value" id="sulfite-val">0%</span>
                </div>
                <div class="output">
                    <span class="output-label" id="acid-label">Acid</span>
                    <div class="progress-bar" role="progressbar" aria-labelledby="acid-label" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100">
                        <div class="progress acid" id="acid-bar" style="width: 0%"></div>
                    </div>
                    <span class="output-value" id="acid-val">0%</span>
                </div>
            </div>
            <div class="rules-info">
                <span>Active Rules: <strong id="active-rules">0</strong></span>
            </div>
        </section>

        <!-- Quick Reference -->
        <section class="card collapsed">
            <h2 onclick="toggleCard(this)">Target Ranges ▼</h2>
            <div class="card-content">
                <p class="reference-note">Reference only. Targets come from controller config.</p>
                <table class="reference-table">
                    <tr><th>Parameter</th><th>Target</th><th>Range</th></tr>
                    <tr><td>TDS</td><td id="sp-tds">2500</td><td>2000-3000 ppm</td></tr>
                    <tr><td>Alkalinity</td><td id="sp-alk">300</td><td>200-400 ppm</td></tr>
                    <tr><td>Sulfite</td><td id="sp-sulf">30</td><td>20-40 ppm</td></tr>
                    <tr><td>pH</td><td id="sp-ph">11.0</td><td>10.5-11.5</td></tr>
                </table>
            </div>
        </section>
    </div>

    <script>
)rawliteral";

    html += generateJS();

    html += R"rawliteral(
    </script>
</body>
</html>
)rawliteral";

    return html;
}

String BoilerWebServer::generateCSS() {
    return R"rawliteral(
@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }
@keyframes slideIn { from { opacity: 0; transform: translateY(-10px); } to { opacity: 1; transform: translateY(0); } }
@keyframes glow { 0%, 100% { box-shadow: 0 0 5px currentColor; } 50% { box-shadow: 0 0 20px currentColor, 0 0 30px currentColor; } }

* { box-sizing: border-box; margin: 0; padding: 0; }
body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: linear-gradient(135deg, #0f0f23 0%, #1a1a3e 50%, #0f0f23 100%);
    background-attachment: fixed;
    color: #eee;
    min-height: 100vh;
    padding-bottom: 20px;
}
.container { max-width: 500px; margin: 0 auto; padding: 16px; }

header {
    text-align: center;
    padding: 24px 16px;
    background: linear-gradient(180deg, rgba(255,255,255,0.05) 0%, transparent 100%);
    border-bottom: 1px solid rgba(255,255,255,0.1);
    margin-bottom: 20px;
}
header h1 {
    font-size: 1.5em;
    font-weight: 600;
    background: linear-gradient(90deg, #4CAF50, #00BCD4);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    background-clip: text;
}
.status-bar {
    margin-top: 12px;
    font-size: 0.85em;
    color: #888;
    display: flex;
    align-items: center;
    justify-content: center;
    flex-wrap: wrap;
    gap: 6px;
}
.status-indicator {
    font-size: 0.9em;
    animation: pulse 2s infinite;
}
.status-indicator.online { color: #4CAF50; text-shadow: 0 0 10px #4CAF50; }
.status-indicator.offline { color: #f44336; animation: none; }
.status-indicator.degraded { color: #FF9800; text-shadow: 0 0 8px #FF9800; }
.system-status { display: block; width: 100%; font-size: 0.75em; margin-top: 4px; }
.system-status.degraded { color: #FF9800; }
.system-status.normal { color: #888; }

.card {
    background: linear-gradient(145deg, rgba(30,40,70,0.9) 0%, rgba(20,30,50,0.95) 100%);
    border: 1px solid rgba(255,255,255,0.08);
    border-radius: 16px;
    padding: 20px;
    margin-bottom: 16px;
    box-shadow: 0 4px 20px rgba(0,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.05);
    backdrop-filter: blur(10px);
    animation: slideIn 0.3s ease-out;
}
.card h2 {
    font-size: 0.85em;
    font-weight: 600;
    color: #7aa2d4;
    margin-bottom: 16px;
    text-transform: uppercase;
    letter-spacing: 1.5px;
    display: flex;
    align-items: center;
    gap: 8px;
}
.card h2::before {
    content: '';
    display: inline-block;
    width: 4px;
    height: 16px;
    background: linear-gradient(180deg, #4CAF50, #00BCD4);
    border-radius: 2px;
}
.card.collapsed .card-content { display: none; }
.card.collapsed h2 { margin-bottom: 0; cursor: pointer; }
.card.collapsed h2:hover { color: #9fc5f8; }

.readings-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
}
.readings-grid.single {
    grid-template-columns: 1fr;
    max-width: 200px;
    margin: 0 auto;
}
.readings-grid.three {
    grid-template-columns: repeat(3, 1fr);
    max-width: 100%;
}
@media (max-width: 400px) {
    .readings-grid.three { grid-template-columns: 1fr; }
}
.reading.invalid .value { color: #f44336; -webkit-text-fill-color: #f44336; }
.reading.stale .value { color: #FF9800; -webkit-text-fill-color: #FF9800; }
.reading {
    text-align: center;
    padding: 20px 12px;
    background: linear-gradient(145deg, rgba(0,0,0,0.3) 0%, rgba(0,0,0,0.1) 100%);
    border: 1px solid rgba(255,255,255,0.05);
    border-radius: 12px;
    transition: transform 0.2s, box-shadow 0.2s;
}
.reading:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(0,0,0,0.3);
}
.reading .label {
    display: block;
    font-size: 0.75em;
    color: #888;
    text-transform: uppercase;
    letter-spacing: 1px;
    margin-bottom: 8px;
}
.reading .value {
    display: block;
    font-size: 2.2em;
    font-weight: 700;
    background: linear-gradient(90deg, #4CAF50, #8BC34A);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    background-clip: text;
    line-height: 1.1;
}
.reading .unit {
    font-size: 0.8em;
    color: #666;
    margin-top: 4px;
    display: block;
}

.input-group {
    margin-bottom: 20px;
    position: relative;
}
.input-group label {
    display: block;
    font-size: 0.85em;
    color: #9fc5f8;
    margin-bottom: 8px;
    font-weight: 500;
}
.input-group input {
    width: 100%;
    padding: 16px;
    font-size: 1.1em;
    border: 2px solid rgba(255,255,255,0.1);
    border-radius: 12px;
    background: rgba(0,0,0,0.3);
    color: #fff;
    outline: none;
    transition: all 0.3s ease;
}
.input-group input:focus {
    border-color: #4CAF50;
    box-shadow: 0 0 0 3px rgba(76,175,80,0.2);
    background: rgba(0,0,0,0.4);
}
.input-group input::placeholder { color: #555; }
.input-group input:valid:not(:placeholder-shown) {
    border-color: rgba(76,175,80,0.5);
}
.test-age {
    position: absolute;
    right: 14px;
    top: 42px;
    font-size: 0.7em;
    color: #888;
    background: rgba(0,0,0,0.5);
    padding: 2px 8px;
    border-radius: 10px;
}
.test-age.stale { color: #ff9800; }
.test-age.expired { color: #f44336; }

.form-error {
    font-size: 0.85em;
    color: #f44336;
    margin-bottom: 12px;
    min-height: 1.2em;
}
.reference-note {
    font-size: 0.75em;
    color: #666;
    margin-bottom: 8px;
}

.button-group {
    display: flex;
    gap: 12px;
    margin-top: 24px;
}
.btn {
    flex: 1;
    padding: 16px;
    font-size: 1em;
    font-weight: 600;
    border: none;
    border-radius: 12px;
    cursor: pointer;
    transition: all 0.2s ease;
    text-transform: uppercase;
    letter-spacing: 0.5px;
}
.btn:active { transform: scale(0.97); }
.btn-primary {
    background: linear-gradient(135deg, #4CAF50 0%, #45a049 100%);
    color: white;
    box-shadow: 0 4px 15px rgba(76,175,80,0.4);
}
.btn-primary:hover {
    box-shadow: 0 6px 20px rgba(76,175,80,0.5);
    transform: translateY(-1px);
}
.btn-primary.success {
    background: linear-gradient(135deg, #00BCD4 0%, #0097A7 100%);
}
.btn-secondary {
    background: rgba(255,255,255,0.1);
    color: #aaa;
    border: 1px solid rgba(255,255,255,0.1);
}
.btn-secondary:hover {
    background: rgba(255,255,255,0.15);
    color: #fff;
}

.confidence-bar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 16px;
    padding-bottom: 16px;
    border-bottom: 1px solid rgba(255,255,255,0.1);
}
.confidence-badge {
    padding: 6px 16px;
    border-radius: 20px;
    font-weight: 600;
    font-size: 0.8em;
    text-transform: uppercase;
    letter-spacing: 1px;
}
.confidence-badge.HIGH {
    background: linear-gradient(135deg, #4CAF50, #45a049);
    color: white;
    box-shadow: 0 2px 10px rgba(76,175,80,0.4);
}
.confidence-badge.MEDIUM {
    background: linear-gradient(135deg, #FF9800, #F57C00);
    color: white;
    box-shadow: 0 2px 10px rgba(255,152,0,0.4);
}
.confidence-badge.LOW {
    background: linear-gradient(135deg, #f44336, #d32f2f);
    color: white;
    box-shadow: 0 2px 10px rgba(244,67,54,0.4);
    animation: pulse 1.5s infinite;
}

.output-grid { display: flex; flex-direction: column; gap: 16px; }
.output {
    display: grid;
    grid-template-columns: 110px 1fr 55px;
    align-items: center;
    gap: 12px;
}
.output-label {
    font-size: 0.85em;
    color: #aaa;
    font-weight: 500;
}
.rec-badge {
    font-size: 0.7em;
    background: linear-gradient(135deg, #FF9800, #F57C00);
    color: white;
    padding: 2px 6px;
    border-radius: 4px;
    margin-left: 6px;
    vertical-align: middle;
}
.progress-bar {
    height: 12px;
    background: rgba(0,0,0,0.4);
    border-radius: 6px;
    overflow: hidden;
    box-shadow: inset 0 2px 4px rgba(0,0,0,0.3);
}
.progress {
    height: 100%;
    border-radius: 6px;
    transition: width 0.5s cubic-bezier(0.4, 0, 0.2, 1);
    background: linear-gradient(90deg, #4CAF50, #8BC34A);
    box-shadow: 0 0 10px rgba(76,175,80,0.5);
}
.progress.caustic {
    background: linear-gradient(90deg, #2196F3, #03A9F4);
    box-shadow: 0 0 10px rgba(33,150,243,0.5);
}
.progress.sulfite {
    background: linear-gradient(90deg, #9C27B0, #E91E63);
    box-shadow: 0 0 10px rgba(156,39,176,0.5);
}
.progress.acid {
    background: linear-gradient(90deg, #FF9800, #FFC107);
    box-shadow: 0 0 10px rgba(255,152,0,0.5);
}
.output-value {
    text-align: right;
    font-weight: 600;
    font-size: 0.95em;
    font-family: 'SF Mono', 'Monaco', monospace;
}

.rules-info {
    margin-top: 16px;
    padding-top: 16px;
    border-top: 1px solid rgba(255,255,255,0.1);
    font-size: 0.85em;
    color: #888;
    display: flex;
    justify-content: center;
    gap: 20px;
}
.rules-info strong {
    color: #4CAF50;
    font-family: 'SF Mono', 'Monaco', monospace;
}

.reference-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.9em;
}
.reference-table th, .reference-table td {
    padding: 12px 8px;
    text-align: left;
}
.reference-table th {
    color: #888;
    font-weight: 500;
    border-bottom: 1px solid rgba(255,255,255,0.1);
}
.reference-table td {
    border-bottom: 1px solid rgba(255,255,255,0.05);
}
.reference-table tr:last-child td { border-bottom: none; }
.reference-table td:nth-child(2) {
    color: #4CAF50;
    font-weight: 600;
    font-family: 'SF Mono', 'Monaco', monospace;
}

/* Toast notification */
.toast {
    position: fixed;
    bottom: 20px;
    left: 50%;
    transform: translateX(-50%) translateY(100px);
    background: linear-gradient(135deg, #323232, #424242);
    color: white;
    padding: 14px 28px;
    border-radius: 30px;
    font-size: 0.9em;
    font-weight: 500;
    box-shadow: 0 4px 20px rgba(0,0,0,0.4);
    transition: transform 0.3s cubic-bezier(0.4, 0, 0.2, 1);
    z-index: 1000;
}
.toast.show { transform: translateX(-50%) translateY(0); }
.toast.success { background: linear-gradient(135deg, #4CAF50, #45a049); }
.toast.error { background: linear-gradient(135deg, #f44336, #d32f2f); }
)rawliteral";
}

String BoilerWebServer::generateJS() {
    return R"rawliteral(
let connected = false;
let toastTimeout = null;
const FETCH_TIMEOUT_MS = 15000;

// Toast notification system
function showToast(message, type = 'info') {
    let toast = document.getElementById('toast');
    if (!toast) {
        toast = document.createElement('div');
        toast.id = 'toast';
        toast.className = 'toast';
        toast.setAttribute('role', 'status');
        toast.setAttribute('aria-live', 'polite');
        toast.setAttribute('aria-atomic', 'true');
        document.body.appendChild(toast);
    }

    toast.textContent = message;
    toast.className = 'toast ' + type;

    clearTimeout(toastTimeout);
    requestAnimationFrame(() => {
        toast.classList.add('show');
        toastTimeout = setTimeout(() => toast.classList.remove('show'), 3000);
    });
}

// Animate value changes
function animateValue(elem, newValue, suffix = '') {
    const current = parseFloat(elem.textContent) || 0;
    const diff = newValue - current;
    if (Math.abs(diff) < 0.1) {
        elem.textContent = newValue.toFixed(suffix === '%' ? 0 : 1) + suffix;
        return;
    }

    const steps = 20;
    const stepValue = diff / steps;
    let step = 0;

    const animate = () => {
        step++;
        const val = current + (stepValue * step);
        elem.textContent = val.toFixed(suffix === '%' ? 0 : 1) + suffix;
        if (step < steps) requestAnimationFrame(animate);
    };
    requestAnimationFrame(animate);
}

function setSensorsOffline() {
    document.getElementById('temperature').textContent = '--';
    document.getElementById('conductivity').textContent = '--';
    document.getElementById('flow-rate').textContent = '--';
    ['reading-temp', 'reading-cond', 'reading-flow'].forEach(id => {
        const el = document.getElementById(id);
        if (el) { el.classList.remove('stale', 'invalid'); }
    });
    const sysStatus = document.getElementById('system-status');
    if (sysStatus) { sysStatus.textContent = ''; sysStatus.className = 'system-status'; }
}

async function fetchStatus() {
    const textEl = document.getElementById('connection-text');
    if (!connected && textEl) textEl.textContent = 'Reconnecting...';

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);

    try {
        const res = await fetch('/api/status', { signal: controller.signal });
        clearTimeout(timeoutId);
        const data = await res.json();

        const health = data.health || {};
        const tempValid = health.temp_valid !== false;
        const condValid = health.cond_valid !== false;

        // System status / safe mode
        const sysStatus = document.getElementById('system-status');
        if (sysStatus) {
            if (health.in_safe_mode && health.safe_mode) {
                sysStatus.textContent = 'Safe mode: ' + health.safe_mode;
                sysStatus.className = 'system-status degraded';
            } else {
                sysStatus.textContent = 'Normal';
                sysStatus.className = 'system-status normal';
            }
        }

        // Temperature: show value only if valid, else -- or Stale
        const tempElem = document.getElementById('temperature');
        const tempReading = document.getElementById('reading-temp');
        if (tempValid) {
            if (tempElem.textContent !== '--') {
                animateValue(tempElem, data.temperature);
            } else {
                tempElem.textContent = data.temperature.toFixed(1);
            }
            if (tempReading) { tempReading.classList.remove('stale', 'invalid'); }
        } else {
            tempElem.textContent = '--';
            if (tempReading) tempReading.classList.add('invalid');
        }

        // Conductivity
        const condElem = document.getElementById('conductivity');
        const condReading = document.getElementById('reading-cond');
        if (condElem) {
            condElem.textContent = (data.conductivity != null) ? Number(data.conductivity).toFixed(0) : '--';
            if (condReading) {
                condReading.classList.toggle('invalid', !condValid);
                condReading.classList.toggle('stale', condValid && health.conductivity && health.conductivity.stale);
            }
        }

        // Flow rate
        const flowElem = document.getElementById('flow-rate');
        const flowReading = document.getElementById('reading-flow');
        if (flowElem) {
            flowElem.textContent = (data.flow_rate != null) ? Number(data.flow_rate).toFixed(2) : '--';
            if (flowReading) flowReading.classList.remove('stale', 'invalid');
        }

        // Update test ages with styling
        updateTestAge('tds-age', data.manual_tests.tds);
        updateTestAge('alk-age', data.manual_tests.alkalinity);
        updateTestAge('sulf-age', data.manual_tests.sulfite);
        updateTestAge('ph-age', data.manual_tests.ph);

        // Fill inputs with current values if valid and empty
        const tdsInput = document.getElementById('tds');
        const alkInput = document.getElementById('alkalinity');
        const sulfInput = document.getElementById('sulfite');
        const phInput = document.getElementById('ph');

        if (data.manual_tests.tds.valid && !tdsInput.value) {
            tdsInput.value = data.manual_tests.tds.value;
        }
        if (data.manual_tests.alkalinity.valid && !alkInput.value) {
            alkInput.value = data.manual_tests.alkalinity.value;
        }
        if (data.manual_tests.sulfite.valid && !sulfInput.value) {
            sulfInput.value = data.manual_tests.sulfite.value;
        }
        if (data.manual_tests.ph.valid && !phInput.value) {
            phInput.value = data.manual_tests.ph.value;
        }

        if (!connected) showToast('Connected to controller', 'success');
        setConnected(true, health.in_safe_mode === true);
    } catch (e) {
        clearTimeout(timeoutId);
        if (e.name === 'AbortError') showToast('Connection timed out', 'error');
        else if (connected) showToast('Connection lost', 'error');
        setSensorsOffline();
        setConnected(false);
    }
}

async function fetchFuzzy() {
    try {
        const res = await fetch('/api/fuzzy');
        const data = await res.json();

        // Animate output bars
        setOutput('blowdown', data.outputs.blowdown);
        setOutput('caustic', data.outputs.caustic);
        setOutput('sulfite', data.outputs.sulfite);
        setOutput('acid', data.outputs.acid);

        // Update confidence badge with animation
        const badge = document.getElementById('confidence');
        const oldConf = badge.textContent;
        if (oldConf !== data.confidence && oldConf !== '--') {
            badge.style.transform = 'scale(1.2)';
            setTimeout(() => badge.style.transform = 'scale(1)', 200);
        }
        badge.textContent = data.confidence;
        badge.className = 'confidence-badge ' + data.confidence;

        document.getElementById('active-rules').textContent = data.active_rules;

        // Update setpoints (show — when config not loaded)
        const spIds = ['sp-tds', 'sp-alk', 'sp-sulf', 'sp-ph'];
        if (data.setpoints) {
            document.getElementById('sp-tds').textContent = data.setpoints.tds;
            document.getElementById('sp-alk').textContent = data.setpoints.alkalinity;
            document.getElementById('sp-sulf').textContent = data.setpoints.sulfite;
            document.getElementById('sp-ph').textContent = data.setpoints.ph;
        } else {
            spIds.forEach(id => {
                const el = document.getElementById(id);
                if (el) el.textContent = '\u2014';
            });
        }
    } catch (e) {
        console.error('Fuzzy fetch error:', e);
    }
}

function setOutput(name, value) {
    const bar = document.getElementById(name + '-bar');
    const val = document.getElementById(name + '-val');
    const pct = Math.round(Number(value) || 0);

    bar.style.width = pct + '%';
    const progressBar = bar.closest('.progress-bar');
    if (progressBar) progressBar.setAttribute('aria-valuenow', pct);
    animateValue(val, pct, '%');

    if (pct > 70) {
        bar.style.boxShadow = '0 0 15px currentColor';
    } else {
        bar.style.boxShadow = '';
    }
}

function updateTestAge(elemId, test) {
    const elem = document.getElementById(elemId);
    if (test.valid && test.age_min >= 0) {
        let text, cls = '';
        if (test.age_min < 60) {
            text = test.age_min + 'm ago';
        } else {
            text = Math.floor(test.age_min / 60) + 'h ago';
        }

        if (test.age_min > 480) {
            cls = 'expired';
        } else if (test.age_min > 240) {
            cls = 'stale';
        }

        elem.textContent = text;
        elem.className = 'test-age ' + cls;
    } else {
        elem.textContent = '';
        elem.className = 'test-age';
    }
}

function setConnected(state, degraded) {
    connected = state;
    const indicator = document.getElementById('wifi-status');
    const text = document.getElementById('connection-text');
    if (!state) {
        indicator.className = 'status-indicator offline';
        text.textContent = 'Disconnected';
    } else if (degraded) {
        indicator.className = 'status-indicator degraded';
        text.textContent = 'Degraded';
    } else {
        indicator.className = 'status-indicator online';
        text.textContent = 'Connected';
    }
}

document.getElementById('test-form').addEventListener('submit', async (e) => {
    e.preventDefault();

    const form = e.target;
    const formError = document.getElementById('form-error');
    const btn = form.querySelector('.btn-primary');
    const data = {};
    const tds = document.getElementById('tds').value;
    const alk = document.getElementById('alkalinity').value;
    const sulf = document.getElementById('sulfite').value;
    const ph = document.getElementById('ph').value;

    if (formError) formError.textContent = '';
    form.setAttribute('aria-invalid', 'false');

    if (tds) data.tds = parseFloat(tds);
    if (alk) data.alkalinity = parseFloat(alk);
    if (sulf) data.sulfite = parseFloat(sulf);
    if (ph) data.ph = parseFloat(ph);

    if (Object.keys(data).length === 0) {
        if (formError) formError.textContent = 'Enter at least one test value.';
        form.setAttribute('aria-invalid', 'true');
        showToast('Enter at least one test value', 'error');
        return;
    }

    btn.disabled = true;
    btn.textContent = 'Submitting...';

    try {
        const res = await fetch('/api/tests', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });

        let errMsg = '';
        if (!res.ok) {
            const errBody = await res.json().catch(() => ({}));
            errMsg = errBody.error || '';
        }

        if (res.ok) {
            btn.textContent = 'Submitted!';
            btn.classList.add('success');
            showToast('Test results saved successfully', 'success');
            if (formError) formError.textContent = '';

            setTimeout(() => {
                btn.textContent = 'Submit Tests';
                btn.classList.remove('success');
                btn.disabled = false;
            }, 2000);

            fetchStatus();
            fetchFuzzy();
        } else {
            const msg = res.status >= 500 ? 'Server error' : (errMsg || 'Invalid input');
            if (formError) formError.textContent = msg;
            form.setAttribute('aria-invalid', 'true');
            showToast(msg, 'error');
            btn.textContent = 'Submit Tests';
            btn.disabled = false;
        }
    } catch (err) {
        const msg = 'Connection error';
        if (formError) formError.textContent = msg;
        form.setAttribute('aria-invalid', 'true');
        showToast(msg, 'error');
        btn.textContent = 'Submit Tests';
        btn.disabled = false;
    }
});

async function clearTests() {
    if (!confirm('Clear all manual test values?')) return;

    const formError = document.getElementById('form-error');
    const form = document.getElementById('test-form');
    if (formError) formError.textContent = '';
    if (form) form.setAttribute('aria-invalid', 'false');

    try {
        await fetch('/api/tests', { method: 'DELETE' });
        document.getElementById('tds').value = '';
        document.getElementById('alkalinity').value = '';
        document.getElementById('sulfite').value = '';
        document.getElementById('ph').value = '';
        showToast('Test values cleared', 'success');
        fetchStatus();
        fetchFuzzy();
    } catch (err) {
        showToast('Connection error', 'error');
    }
}

function toggleCard(header) {
    const card = header.parentElement;
    card.classList.toggle('collapsed');
    const arrow = header.textContent.includes('▼') ? '▶' : '▼';
    header.textContent = header.textContent.replace(/[▼▶]/, arrow);
}

// Haptic feedback for mobile (if supported)
function haptic() {
    if (navigator.vibrate) navigator.vibrate(10);
}
document.querySelectorAll('.btn').forEach(btn => {
    btn.addEventListener('click', haptic);
});

// Initial fetch and polling
fetchStatus();
fetchFuzzy();
setInterval(fetchStatus, 5000);
setInterval(fetchFuzzy, 5000);

// Add pull-to-refresh hint
let touchStartY = 0;
document.addEventListener('touchstart', e => touchStartY = e.touches[0].clientY);
document.addEventListener('touchend', e => {
    if (window.scrollY === 0 && e.changedTouches[0].clientY - touchStartY > 100) {
        showToast('Refreshing...', 'info');
        fetchStatus();
        fetchFuzzy();
    }
});
)rawliteral";
}

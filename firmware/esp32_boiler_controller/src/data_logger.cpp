/**
 * @file data_logger.cpp
 * @brief Data Logging Module Implementation for TimescaleDB
 */

#include "data_logger.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Global instance
DataLogger dataLogger;

// NTP client for time sync
static WiFiUDP ntpUDP;
static NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// ============================================================================
// DATA LOGGER IMPLEMENTATION
// ============================================================================

DataLogger::DataLogger()
    : _config(nullptr)
    , _enabled(false)
    , _wifi_connected(false)
    , _server_connected(false)
    , _ap_mode(false)
    , _last_log_time(0)
    , _last_upload_time(0)
    , _log_interval(TSDB_LOG_INTERVAL_MS)
    , _last_http_status(0)
    , _buffer_head(0)
    , _buffer_tail(0)
    , _buffer_count(0)
{
}

bool DataLogger::begin(system_config_t* config) {
    _config = config;

    if (!_config) {
        Serial.println("DataLogger: No config provided");
        return false;
    }

    _log_interval = _config->log_interval_ms;
    _enabled = true;

    Serial.println("DataLogger initialized");
    return true;
}

bool DataLogger::connectWiFi() {
    if (!_config || strlen(_config->wifi_ssid) == 0) {
        Serial.println("DataLogger: No WiFi credentials configured");
        return false;
    }

    Serial.printf("DataLogger: Connecting to WiFi '%s'...\n", _config->wifi_ssid);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(_config->wifi_ssid, _config->wifi_password);

    uint32_t start_time = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start_time >= WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("DataLogger: WiFi connection timeout");
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.printf("DataLogger: Connected! IP: %s\n", WiFi.localIP().toString().c_str());

    _wifi_connected = true;

    // Sync time
    syncTime();

    return true;
}

void DataLogger::disconnectWiFi() {
    WiFi.disconnect(true);
    _wifi_connected = false;
    _server_connected = false;
}

bool DataLogger::isWiFiConnected() {
    _wifi_connected = (WiFi.status() == WL_CONNECTED);
    return _wifi_connected;
}

void DataLogger::update() {
    if (!_enabled) return;

    // Check WiFi connection
    handleWiFiEvents();

    // Try to upload buffered data
    if (_wifi_connected && _buffer_count > 0) {
        uploadBuffered();
    }

    // Log at configured interval
    uint32_t now = millis();
    if (now - _last_log_time >= _log_interval) {
        _last_log_time = now;
        // Note: Actual logging is triggered from main loop
    }
}

void DataLogger::logReading(sensor_reading_t* reading) {
    if (!_enabled || !reading) return;

    // Add timestamp if not set
    if (reading->timestamp == 0) {
        reading->timestamp = getTimestamp();
    }

    // Try to upload immediately if connected
    if (_wifi_connected) {
        if (uploadReading(reading)) {
            _server_connected = true;
            return;
        } else {
            _server_connected = false;
        }
    }

    // Buffer for later upload
    bufferReading(reading);
}

void DataLogger::logEvent(const char* event_type, const char* description, int32_t value) {
    if (!_enabled) return;

    event_log_t event;
    event.timestamp = getTimestamp();
    strncpy(event.event_type, event_type, sizeof(event.event_type) - 1);
    strncpy(event.description, description, sizeof(event.description) - 1);
    event.value = value;

    if (_wifi_connected) {
        uploadEvent(&event);
    }

    Serial.printf("Event: %s - %s (%d)\n", event_type, description, value);
}

void DataLogger::logAlarm(uint16_t alarm_code, const char* alarm_name,
                          bool active, float trigger_value) {
    if (!_enabled) return;

    alarm_log_t alarm;
    alarm.timestamp = getTimestamp();
    alarm.alarm_code = alarm_code;
    strncpy(alarm.alarm_name, alarm_name, sizeof(alarm.alarm_name) - 1);
    alarm.active = active;
    alarm.trigger_value = trigger_value;

    if (_wifi_connected) {
        uploadAlarm(&alarm);
    }

    Serial.printf("Alarm: %s - %s (%.2f)\n", alarm_name,
                  active ? "ACTIVE" : "CLEARED", trigger_value);
}

int DataLogger::forceUpload() {
    if (!_wifi_connected) return 0;

    int uploaded = 0;
    while (_buffer_count > 0 && uploadBuffered()) {
        uploaded++;
    }
    return uploaded;
}

int DataLogger::getPendingCount() {
    return _buffer_count;
}

int DataLogger::getLastUploadStatus() {
    return _last_http_status;
}

int DataLogger::getWiFiRSSI() {
    if (_wifi_connected) {
        return WiFi.RSSI();
    }
    return -100;
}

bool DataLogger::isServerConnected() {
    return _server_connected;
}

void DataLogger::setLogInterval(uint32_t interval_ms) {
    _log_interval = interval_ms;
    if (_config) {
        _config->log_interval_ms = interval_ms;
    }
}

void DataLogger::setEnabled(bool enable) {
    _enabled = enable;
}

void DataLogger::startAPMode() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL);

    _ap_mode = true;
    _wifi_connected = false;

    Serial.printf("AP Mode started: %s\n", WIFI_AP_SSID);
    Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void DataLogger::stopAPMode() {
    WiFi.softAPdisconnect(true);
    _ap_mode = false;
}

bool DataLogger::isAPMode() {
    return _ap_mode;
}

bool DataLogger::syncTime() {
    if (!_wifi_connected) return false;

    timeClient.begin();

    int retries = 5;
    while (retries-- > 0) {
        if (timeClient.update()) {
            Serial.printf("NTP time synced: %s\n", timeClient.getFormattedTime().c_str());
            return true;
        }
        delay(1000);
    }

    Serial.println("NTP sync failed");
    return false;
}

uint32_t DataLogger::getTimestamp() {
    if (_wifi_connected) {
        return timeClient.getEpochTime();
    }
    return millis() / 1000;  // Fallback to uptime
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

bool DataLogger::uploadReading(sensor_reading_t* reading) {
    if (!_config || strlen(_config->tsdb_host) == 0) return false;

    String url = "http://" + String(_config->tsdb_host) + ":" +
                 String(_config->tsdb_port) + "/api/readings";

    String json = buildReadingJSON(reading);

    _http.begin(url);
    _http.addHeader("Content-Type", "application/json");

    _last_http_status = _http.POST(json);

    _http.end();

    if (_last_http_status == 200 || _last_http_status == 201) {
        return true;
    }

    Serial.printf("Upload failed: HTTP %d\n", _last_http_status);
    return false;
}

bool DataLogger::uploadEvent(event_log_t* event) {
    if (!_config || strlen(_config->tsdb_host) == 0) return false;

    String url = "http://" + String(_config->tsdb_host) + ":" +
                 String(_config->tsdb_port) + "/api/events";

    String json = buildEventJSON(event);

    _http.begin(url);
    _http.addHeader("Content-Type", "application/json");

    _last_http_status = _http.POST(json);

    _http.end();

    return (_last_http_status == 200 || _last_http_status == 201);
}

bool DataLogger::uploadAlarm(alarm_log_t* alarm) {
    if (!_config || strlen(_config->tsdb_host) == 0) return false;

    String url = "http://" + String(_config->tsdb_host) + ":" +
                 String(_config->tsdb_port) + "/api/alarms";

    String json = buildAlarmJSON(alarm);

    _http.begin(url);
    _http.addHeader("Content-Type", "application/json");

    _last_http_status = _http.POST(json);

    _http.end();

    return (_last_http_status == 200 || _last_http_status == 201);
}

void DataLogger::bufferReading(sensor_reading_t* reading) {
    // Add to circular buffer
    _reading_buffer[_buffer_head] = *reading;
    _buffer_head = (_buffer_head + 1) % BUFFER_SIZE;

    if (_buffer_count < BUFFER_SIZE) {
        _buffer_count++;
    } else {
        // Buffer full - overwrite oldest
        _buffer_tail = (_buffer_tail + 1) % BUFFER_SIZE;
    }
}

bool DataLogger::uploadBuffered() {
    if (_buffer_count == 0) return false;

    // Get oldest entry
    sensor_reading_t* reading = &_reading_buffer[_buffer_tail];

    if (uploadReading(reading)) {
        _buffer_tail = (_buffer_tail + 1) % BUFFER_SIZE;
        _buffer_count--;
        return true;
    }

    return false;
}

String DataLogger::buildReadingJSON(sensor_reading_t* reading) {
    JsonDocument doc;

    doc["timestamp"] = reading->timestamp;
    doc["conductivity"] = reading->conductivity;
    doc["temperature"] = reading->temperature;
    doc["water_meter1"] = reading->water_meter1;
    doc["water_meter2"] = reading->water_meter2;
    doc["flow_rate"] = reading->flow_rate;
    doc["blowdown_active"] = reading->blowdown_active;
    doc["pump1_active"] = reading->pump1_active;
    doc["pump2_active"] = reading->pump2_active;
    doc["pump3_active"] = reading->pump3_active;
    doc["active_alarms"] = reading->active_alarms;

    String output;
    serializeJson(doc, output);
    return output;
}

String DataLogger::buildEventJSON(event_log_t* event) {
    JsonDocument doc;

    doc["timestamp"] = event->timestamp;
    doc["event_type"] = event->event_type;
    doc["description"] = event->description;
    doc["value"] = event->value;

    String output;
    serializeJson(doc, output);
    return output;
}

String DataLogger::buildAlarmJSON(alarm_log_t* alarm) {
    JsonDocument doc;

    doc["timestamp"] = alarm->timestamp;
    doc["alarm_code"] = alarm->alarm_code;
    doc["alarm_name"] = alarm->alarm_name;
    doc["active"] = alarm->active;
    doc["trigger_value"] = alarm->trigger_value;

    String output;
    serializeJson(doc, output);
    return output;
}

void DataLogger::handleWiFiEvents() {
    static uint32_t last_check = 0;
    static uint32_t reconnect_time = 0;
    uint32_t now = millis();

    // Check every 5 seconds
    if (now - last_check < 5000) return;
    last_check = now;

    bool connected = (WiFi.status() == WL_CONNECTED);

    if (_wifi_connected && !connected) {
        // Lost connection
        Serial.println("WiFi connection lost");
        _wifi_connected = false;
        _server_connected = false;
        reconnect_time = now;
    }

    if (!_wifi_connected && !_ap_mode && _config && strlen(_config->wifi_ssid) > 0) {
        // Try to reconnect
        if (now - reconnect_time >= WIFI_RECONNECT_DELAY_MS) {
            Serial.println("Attempting WiFi reconnection...");
            connectWiFi();
        }
    }
}

/**
 * @file mqtt_telemetry.cpp
 * @brief MQTT telemetry implementation (Modern IoT Stack Phase C)
 */

#include "mqtt_telemetry.h"
#include "device_identity.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient s_wifiClient;
static PubSubClient s_mqtt(s_wifiClient);

// Global instance
MqttTelemetry mqttTelemetry;

// ============================================================================
// CONSTRUCTOR / BEGIN
// ============================================================================

MqttTelemetry::MqttTelemetry()
    : _config(nullptr)
    , _connected(false)
    , _seq(0)
    , _last_connect_attempt(0)
    , _reconnect_backoff_ms(RECONNECT_INITIAL_MS)
    , _pending_count(0)
{
    memset(_device_id, 0, sizeof(_device_id));
    memset(_topic_base, 0, sizeof(_topic_base));
    memset(_client_id, 0, sizeof(_client_id));
    memset(_pending_readings, 0, sizeof(_pending_readings));
}

bool MqttTelemetry::begin(system_config_t* config) {
    _config = config;
    if (!_config) return false;

    device_id_get(_device_id, sizeof(_device_id));
    snprintf(_topic_base, sizeof(_topic_base), "device/%s", _device_id);
    snprintf(_client_id, sizeof(_client_id), "boiler_%s", _device_id);

    if (!s_mqtt.setBufferSize(MQTT_BUFFER_SIZE)) {
        Serial.println("MqttTelemetry: setBufferSize failed");
    }
    s_mqtt.setServer(_config->mqtt_host, _config->mqtt_port);
    s_mqtt.setKeepAlive(60);
    s_mqtt.setSocketTimeout(10);

    _last_connect_attempt = 0;
    _reconnect_backoff_ms = RECONNECT_INITIAL_MS;
    _connected = false;

    Serial.printf("MqttTelemetry: device_id=%s, broker=%s:%u\n",
                  _device_id, _config->mqtt_host, _config->mqtt_port);
    return true;
}

// ============================================================================
// CONNECTION
// ============================================================================

bool MqttTelemetry::tryConnect() {
    if (!_config || !_config->use_mqtt_telemetry) return false;
    if (strlen(_config->mqtt_host) == 0) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    char will_topic[48];
    snprintf(will_topic, sizeof(will_topic), "%s/health", _topic_base);
    const char* will_msg = "offline";

    bool ok;
    if (strlen(_config->mqtt_user) > 0) {
        ok = s_mqtt.connect(_client_id, _config->mqtt_user, _config->mqtt_pass,
                            will_topic, 1, true, will_msg);
    } else {
        ok = s_mqtt.connect(_client_id, will_topic, 1, true, will_msg);
    }

    if (ok) {
        _connected = true;
        _reconnect_backoff_ms = RECONNECT_INITIAL_MS;
        Serial.println("MqttTelemetry: connected");
    } else {
        _connected = false;
        Serial.printf("MqttTelemetry: connect failed (%d)\n", s_mqtt.state());
    }
    return ok;
}

void MqttTelemetry::disconnect() {
    if (_connected) {
        s_mqtt.disconnect();
        _connected = false;
    }
}

bool MqttTelemetry::publish(const char* topic_suffix, const char* payload) {
    if (!_connected || !s_mqtt.connected()) return false;
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/%s", _topic_base, topic_suffix);
    bool ok = s_mqtt.publish(topic, payload, false);
    if (!ok) Serial.printf("MqttTelemetry: publish %s failed\n", topic_suffix);
    return ok;
}

// ============================================================================
// UPDATE (call from logging task)
// ============================================================================

void MqttTelemetry::update() {
    if (!_config || !_config->use_mqtt_telemetry) return;

    if (s_mqtt.connected()) {
        _connected = true;
        s_mqtt.loop();
        flushPendingReadings();
        return;
    }

    _connected = false;
    uint32_t now = millis();
    if (now - _last_connect_attempt >= _reconnect_backoff_ms) {
        _last_connect_attempt = now;
        if (tryConnect()) {
            flushPendingReadings();
        } else {
            if (_reconnect_backoff_ms < RECONNECT_MAX_MS) {
                _reconnect_backoff_ms += 2000;
            }
        }
    }
}

// ============================================================================
// METRICS (Sparkplug-style JSON)
// ============================================================================

void MqttTelemetry::publishReading(const sensor_reading_t* reading) {
    if (!_config || !_config->use_mqtt_telemetry || !reading) return;

    if (!_connected || !s_mqtt.connected()) {
        bufferReading(reading);
        return;
    }

    JsonDocument doc;
    doc["timestamp"] = reading->timestamp;
    doc["device_id"] = _device_id;
    doc["sequence"] = ++_seq;

    JsonArray metrics = doc["metrics"].to<JsonArray>();
    auto addMetric = [&metrics](const char* id, float value, const char* unit, const char* quality) {
        JsonObject m = metrics.add<JsonObject>();
        m["id"] = id;
        m["value"] = value;
        m["unit"] = unit;
        m["quality"] = quality;
    };

    const char* cond_q = reading->cond_sensor_valid ? "good" : "bad";
    const char* temp_q = reading->temp_sensor_valid ? "good" : "bad";
    addMetric("conductivity", reading->conductivity, "uS/cm", cond_q);
    addMetric("temperature", reading->temperature, "C", temp_q);
    addMetric("flow_rate", reading->flow_rate, "GPM", "good");
    addMetric("water_meter1", (float)reading->water_meter1, "gal", "good");
    addMetric("water_meter2", (float)reading->water_meter2, "gal", "good");
    addMetric("valve_position_mA", reading->valve_position_mA, "mA", "good");
    addMetric("blowdown_active", reading->blowdown_active ? 1.0f : 0.0f, "", "good");
    addMetric("pump1_active", reading->pump1_active ? 1.0f : 0.0f, "", "good");
    addMetric("pump2_active", reading->pump2_active ? 1.0f : 0.0f, "", "good");
    addMetric("pump3_active", reading->pump3_active ? 1.0f : 0.0f, "", "good");
    addMetric("active_alarms", (float)reading->active_alarms, "", "good");
    addMetric("safe_mode", (float)reading->safe_mode, "", "good");
    addMetric("devices_operational", (float)reading->devices_operational, "", "good");
    addMetric("devices_faulted", (float)reading->devices_faulted, "", "good");

    String payload;
    serializeJson(doc, payload);
    if (publish("metrics", payload.c_str())) {
        // Optional: also publish full state snapshot less often if desired
    }
}

void MqttTelemetry::bufferReading(const sensor_reading_t* reading) {
    if (_pending_count >= PENDING_READINGS_MAX) return;
    for (int i = 0; i < PENDING_READINGS_MAX; i++) {
        if (!_pending_readings[i].used) {
            _pending_readings[i].reading = *reading;
            _pending_readings[i].used = true;
            _pending_count++;
            return;
        }
    }
}

void MqttTelemetry::flushPendingReadings() {
    for (int i = 0; i < PENDING_READINGS_MAX; i++) {
        if (!_pending_readings[i].used) continue;
        if (!_connected || !s_mqtt.connected()) break;
        publishReading(&_pending_readings[i].reading);
        _pending_readings[i].used = false;
        _pending_count--;
    }
}

// ============================================================================
// EVENT / ALARM
// ============================================================================

void MqttTelemetry::publishEvent(const char* event_type, const char* description, int32_t value,
                                 uint32_t timestamp_sec) {
    if (!_config || !_config->use_mqtt_telemetry) return;

    if (timestamp_sec == 0) timestamp_sec = (uint32_t)(millis() / 1000);
    JsonDocument doc;
    doc["timestamp"] = timestamp_sec;
    doc["device_id"] = _device_id;
    doc["sequence"] = ++_seq;
    doc["type"] = "event";
    doc["event_type"] = event_type;
    doc["description"] = description;
    doc["value"] = value;

    String payload;
    serializeJson(doc, payload);
    if (_connected && s_mqtt.connected()) {
        publish("alarm", payload.c_str());  // events go to alarm topic per plan (event/alarm stream)
    }
}

void MqttTelemetry::publishAlarm(uint16_t alarm_code, const char* alarm_name,
                                 bool active, float trigger_value, uint32_t timestamp_sec) {
    if (!_config || !_config->use_mqtt_telemetry) return;

    if (timestamp_sec == 0) timestamp_sec = (uint32_t)(millis() / 1000);
    JsonDocument doc;
    doc["timestamp"] = timestamp_sec;
    doc["device_id"] = _device_id;
    doc["sequence"] = ++_seq;
    doc["type"] = "alarm";
    doc["alarm_code"] = alarm_code;
    doc["alarm_name"] = alarm_name;
    doc["active"] = active;
    doc["trigger_value"] = trigger_value;

    String payload;
    serializeJson(doc, payload);
    if (_connected && s_mqtt.connected()) {
        publish("alarm", payload.c_str());
    }
}

// ============================================================================
// HEALTH
// ============================================================================

void MqttTelemetry::publishHealth(uint32_t uptime_sec, uint32_t free_heap,
                                  bool wifi_connected, uint16_t active_alarms) {
    if (!_config || !_config->use_mqtt_telemetry) return;
    if (!_connected || !s_mqtt.connected()) return;

    JsonDocument doc;
    doc["timestamp"] = (uint32_t)millis() / 1000;
    doc["device_id"] = _device_id;
    doc["sequence"] = ++_seq;
    doc["uptime_sec"] = uptime_sec;
    doc["free_heap"] = free_heap;
    doc["wifi_connected"] = wifi_connected;
    doc["mqtt_connected"] = true;
    doc["active_alarms"] = active_alarms;
    doc["firmware"] = FIRMWARE_VERSION_STRING;

    String payload;
    serializeJson(doc, payload);
    publish("health", payload.c_str());
}

void MqttTelemetry::publishCommandResult(const char* request_id, const char* result, const char* message) {
    if (!_config || !_config->use_mqtt_telemetry) return;
    if (!_connected || !s_mqtt.connected()) return;

    JsonDocument doc;
    doc["timestamp"] = (uint32_t)(millis() / 1000);
    doc["device_id"] = _device_id;
    doc["sequence"] = ++_seq;
    doc["request_id"] = request_id;
    doc["result"] = result;
    if (message && message[0] != '\0') doc["message"] = message;

    String payload;
    serializeJson(doc, payload);
    publish("command_result", payload.c_str());
}

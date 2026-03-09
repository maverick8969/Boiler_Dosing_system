/**
 * @file mqtt_telemetry.cpp
 * @brief MQTT Sparkplug-style telemetry implementation
 */

#include "mqtt_telemetry.h"
#include "device_identity.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient s_mqttWifiClient;
static PubSubClient s_mqttClient(s_mqttWifiClient);

MqttTelemetry mqttTelemetry;

#define MQTT_RECONNECT_INITIAL_MS  5000
#define MQTT_RECONNECT_MAX_MS      60000
#define MQTT_KEEPALIVE_SEC         60
#define MQTT_PAYLOAD_MAX           512

MqttTelemetry::MqttTelemetry()
    : _config(nullptr)
    , _connected(false)
    , _sequence(0)
    , _last_reconnect(0)
    , _backoff_ms(MQTT_RECONNECT_INITIAL_MS)
{
    memset(_device_id, 0, sizeof(_device_id));
    memset(_topic_buf, 0, sizeof(_topic_buf));
}

bool MqttTelemetry::begin(system_config_t* config) {
    _config = config;
    if (!_config || strlen(_config->mqtt_host) == 0) {
        return false;
    }
    device_id_get(_device_id, sizeof(_device_id));
    s_mqttClient.setServer(_config->mqtt_host, _config->mqtt_port);
    s_mqttClient.setBufferSize(MQTT_PAYLOAD_MAX);
    s_mqttClient.setKeepAlive(MQTT_KEEPALIVE_SEC);
    if (strlen(_config->mqtt_user) > 0) {
        s_mqttClient.setCredentials(_config->mqtt_user, _config->mqtt_pass);
    }
    _last_reconnect = 0;
    _backoff_ms = MQTT_RECONNECT_INITIAL_MS;
    return true;
}

void MqttTelemetry::update() {
    if (!_config || strlen(_config->mqtt_host) == 0 || !_config->use_mqtt_telemetry) {
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        _connected = false;
        return;
    }
    if (s_mqttClient.connected()) {
        s_mqttClient.loop();
        _connected = true;
        return;
    }
    _connected = false;
    uint32_t now = millis();
    if (now - _last_reconnect < _backoff_ms) {
        return;
    }
    _last_reconnect = now;
    if (connect()) {
        _backoff_ms = MQTT_RECONNECT_INITIAL_MS;
    } else {
        if (_backoff_ms < MQTT_RECONNECT_MAX_MS) {
            _backoff_ms += 5000;
        }
    }
}

bool MqttTelemetry::connect() {
    String clientId = String("boiler-") + _device_id;
    if (s_mqttClient.connect(clientId.c_str())) {
        _connected = true;
        return true;
    }
    return false;
}

void MqttTelemetry::disconnect() {
    s_mqttClient.disconnect();
    _connected = false;
}

bool MqttTelemetry::publish(const char* topic_suffix, const char* payload) {
    if (!_connected && !s_mqttClient.connected()) return false;
    snprintf(_topic_buf, sizeof(_topic_buf), "device/%s/%s", _device_id, topic_suffix);
    return s_mqttClient.publish(_topic_buf, payload, false);
}

void MqttTelemetry::buildMetricsPayload(const sensor_reading_t* reading, char* buf, size_t buf_len) {
    JsonDocument doc;
    doc["timestamp"] = reading->timestamp;
    doc["device_id"] = _device_id;
    doc["sequence"] = _sequence++;
    JsonArray metrics = doc["metrics"].to<JsonArray>();
    JsonObject m;
    m = metrics.add<JsonObject>();
    m["id"] = "conductivity";
    m["value"] = reading->conductivity;
    m["unit"] = "uS_cm";
    m["quality"] = reading->cond_sensor_valid ? "good" : "bad";
    m = metrics.add<JsonObject>();
    m["id"] = "temperature";
    m["value"] = reading->temperature;
    m["unit"] = "C";
    m["quality"] = reading->temp_sensor_valid ? "good" : "bad";
    m = metrics.add<JsonObject>();
    m["id"] = "water_meter1";
    m["value"] = (uint32_t)reading->water_meter1;
    m["unit"] = "gal";
    m["quality"] = "good";
    m = metrics.add<JsonObject>();
    m["id"] = "water_meter2";
    m["value"] = (uint32_t)reading->water_meter2;
    m["unit"] = "gal";
    m["quality"] = "good";
    m = metrics.add<JsonObject>();
    m["id"] = "flow_rate";
    m["value"] = reading->flow_rate;
    m["unit"] = "GPM";
    m["quality"] = "good";
    m = metrics.add<JsonObject>();
    m["id"] = "blowdown_active";
    m["value"] = reading->blowdown_active ? 1 : 0;
    m["unit"] = "";
    m["quality"] = "good";
    m = metrics.add<JsonObject>();
    m["id"] = "pump1_active";
    m["value"] = reading->pump1_active ? 1 : 0;
    m["unit"] = "";
    m["quality"] = "good";
    m = metrics.add<JsonObject>();
    m["id"] = "pump2_active";
    m["value"] = reading->pump2_active ? 1 : 0;
    m["unit"] = "";
    m["quality"] = "good";
    m = metrics.add<JsonObject>();
    m["id"] = "pump3_active";
    m["value"] = reading->pump3_active ? 1 : 0;
    m["unit"] = "";
    m["quality"] = "good";
    m = metrics.add<JsonObject>();
    m["id"] = "active_alarms";
    m["value"] = reading->active_alarms;
    m["unit"] = "";
    m["quality"] = "good";
    serializeJson(doc, buf, buf_len);
}

void MqttTelemetry::publishReading(const sensor_reading_t* reading) {
    if (!_config || !_config->use_mqtt_telemetry || !reading) return;
    char payload[MQTT_PAYLOAD_MAX];
    buildMetricsPayload(reading, payload, sizeof(payload));
    if (publish("metrics", payload)) {
        /* published */
    }
}

void MqttTelemetry::publishAlarm(uint16_t alarm_code, const char* alarm_name, bool active, float trigger_value, uint32_t timestamp) {
    if (!_config || !_config->use_mqtt_telemetry) return;
    JsonDocument doc;
    doc["type"] = "alarm";
    doc["timestamp"] = timestamp;
    doc["device_id"] = _device_id;
    doc["sequence"] = _sequence++;
    doc["alarm_code"] = alarm_code;
    doc["alarm_name"] = alarm_name;
    doc["active"] = active;
    doc["trigger_value"] = trigger_value;
    String pl;
    serializeJson(doc, pl);
    publish("alarm", pl.c_str());
}

void MqttTelemetry::publishHealth(uint32_t uptime_sec, int free_heap, bool wifi_ok, uint16_t active_alarms) {
    if (!_config || !_config->use_mqtt_telemetry) return;
    JsonDocument doc;
    doc["timestamp"] = (uint32_t)(millis() / 1000);
    doc["device_id"] = _device_id;
    doc["sequence"] = _sequence++;
    doc["uptime_sec"] = uptime_sec;
    doc["free_heap"] = free_heap;
    doc["wifi_connected"] = wifi_ok;
    doc["mqtt_connected"] = _connected;
    doc["active_alarms"] = active_alarms;
    String pl;
    serializeJson(doc, pl);
    publish("health", pl.c_str());
}

void MqttTelemetry::publishCommandResult(const char* request_id, const char* result, const char* message) {
    if (!_config || !_config->use_mqtt_telemetry) return;
    JsonDocument doc;
    doc["request_id"] = request_id;
    doc["result"] = result;
    if (message && message[0]) doc["message"] = message;
    doc["timestamp"] = (uint32_t)(millis() / 1000);
    doc["device_id"] = _device_id;
    doc["sequence"] = _sequence++;
    String pl;
    serializeJson(doc, pl);
    publish("command_result", pl.c_str());
}

void MqttTelemetry::publishEvent(const char* event_type, const char* description, int32_t value, uint32_t timestamp) {
    if (!_config || !_config->use_mqtt_telemetry) return;
    JsonDocument doc;
    doc["timestamp"] = timestamp;
    doc["device_id"] = _device_id;
    doc["sequence"] = _sequence++;
    doc["event_type"] = event_type;
    doc["description"] = description;
    doc["value"] = value;
    String pl;
    serializeJson(doc, pl);
    publish("state", pl.c_str());
}

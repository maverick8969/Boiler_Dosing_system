/**
 * @file mqtt_telemetry.h
 * @brief MQTT + Sparkplug-compatible telemetry (Modern IoT Stack)
 *
 * Publishes to device/{id}/state, metrics, alarm, health.
 * Payload: timestamp, device_id, sequence_number, metrics (id, value, unit, quality).
 * Offline buffering; non-blocking.
 */

#ifndef MQTT_TELEMETRY_H
#define MQTT_TELEMETRY_H

#include "config.h"
#include "data_logger.h"

class MqttTelemetry {
public:
    MqttTelemetry();

    bool begin(system_config_t* config);
    void update();

    bool isConnected() const { return _connected; }
    void publishReading(const sensor_reading_t* reading);
    void publishAlarm(uint16_t alarm_code, const char* alarm_name, bool active, float trigger_value, uint32_t timestamp);
    void publishHealth(uint32_t uptime_sec, int free_heap, bool wifi_ok, uint16_t active_alarms);
    void publishCommandResult(const char* request_id, const char* result, const char* message);
    void publishEvent(const char* event_type, const char* description, int32_t value, uint32_t timestamp);

private:
    system_config_t* _config;
    bool _connected;
    uint32_t _sequence;
    uint32_t _last_reconnect;
    uint32_t _backoff_ms;
    char _device_id[DEVICE_ID_MAX_LEN];
    char _topic_buf[96];

    bool connect();
    void disconnect();
    bool publish(const char* topic_suffix, const char* payload);
    void buildMetricsPayload(const sensor_reading_t* reading, char* buf, size_t buf_len);
};

extern MqttTelemetry mqttTelemetry;

#endif /* MQTT_TELEMETRY_H */

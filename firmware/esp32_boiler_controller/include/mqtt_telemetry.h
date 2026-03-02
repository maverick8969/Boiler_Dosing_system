/**
 * @file mqtt_telemetry.h
 * @brief MQTT telemetry module (Modern IoT Stack Phase C)
 *
 * Sparkplug-style topics and JSON payloads:
 *   device/{device_id}/metrics  - time-series (conductivity, temp, flow, etc.)
 *   device/{device_id}/state   - full state snapshot
 *   device/{device_id}/alarm   - alarm state changes
 *   device/{device_id}/health - heartbeat (uptime, connectivity)
 *
 * Connects when WiFi STA is up; reconnect with backoff. Offline buffer for
 * readings/events/alarms; does not block control loop.
 */

#ifndef MQTT_TELEMETRY_H
#define MQTT_TELEMETRY_H

#include "config.h"
#include "data_logger.h"

// ============================================================================
// MQTT TELEMETRY CLASS
// ============================================================================

class MqttTelemetry {
public:
    MqttTelemetry();

    /**
     * @brief Initialize with config; call once after config is loaded.
     */
    bool begin(system_config_t* config);

    /**
     * @brief Call from logging task loop; maintains connection and flushes buffer.
     */
    void update();

    /**
     * @brief Publish sensor reading to device/{id}/metrics (or buffer if offline).
     */
    void publishReading(const sensor_reading_t* reading);

    /**
     * @brief Publish event to device/{id}/alarm (or buffer if offline).
     * @param timestamp_sec Unix timestamp; 0 = use uptime seconds
     */
    void publishEvent(const char* event_type, const char* description, int32_t value,
                      uint32_t timestamp_sec = 0);

    /**
     * @brief Publish alarm to device/{id}/alarm (or buffer if offline).
     * @param timestamp_sec Unix timestamp; 0 = use uptime seconds
     */
    void publishAlarm(uint16_t alarm_code, const char* alarm_name,
                     bool active, float trigger_value, uint32_t timestamp_sec = 0);

    /**
     * @brief Publish health/heartbeat to device/{id}/health.
     */
    void publishHealth(uint32_t uptime_sec, uint32_t free_heap,
                       bool wifi_connected, uint16_t active_alarms);

    /**
     * @brief Publish command result to device/{id}/command_result.
     */
    void publishCommandResult(const char* request_id, const char* result, const char* message);

    /**
     * @brief Whether MQTT is enabled in config and broker is connected.
     */
    bool isConnected() const { return _connected; }

    /**
     * @brief Number of messages pending in offline buffer.
     */
    int getPendingCount() const { return _pending_count; }

private:
    system_config_t* _config;
    bool _connected;
    uint32_t _seq;
    uint32_t _last_connect_attempt;
    uint32_t _reconnect_backoff_ms;
    char _device_id[DEVICE_ID_MAX_LEN];
    char _topic_base[32];   // "device/XXXXXXXXXXXX"
    char _client_id[24];    // "boiler_XXXXXXXXXXXX"

    static const int MQTT_BUFFER_SIZE = 1024;
    static const int PENDING_READINGS_MAX = 20;
    static const int RECONNECT_INITIAL_MS = 2000;
    static const int RECONNECT_MAX_MS = 60000;

    struct pending_reading_t {
        sensor_reading_t reading;
        bool used;
    };
    pending_reading_t _pending_readings[PENDING_READINGS_MAX];
    int _pending_count;

    bool tryConnect();
    void disconnect();
    bool publish(const char* topic_suffix, const char* payload);
    void flushPendingReadings();
    void bufferReading(const sensor_reading_t* reading);
};

extern MqttTelemetry mqttTelemetry;

#endif /* MQTT_TELEMETRY_H */

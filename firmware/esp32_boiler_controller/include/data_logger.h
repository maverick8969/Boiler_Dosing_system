/**
 * @file data_logger.h
 * @brief Data Logging Module for TimescaleDB and Grafana
 *
 * Provides:
 * - WiFi connectivity management
 * - HTTP POST to TimescaleDB REST endpoint
 * - Event logging
 * - Alarm history
 * - Buffered uploads for network resilience
 */

#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================================
// LOG ENTRY TYPES
// ============================================================================

typedef enum {
    LOG_TYPE_READING = 0,   // Sensor readings
    LOG_TYPE_EVENT,         // System events
    LOG_TYPE_ALARM,         // Alarms
    LOG_TYPE_CONFIG         // Configuration changes
} log_type_t;

// ============================================================================
// SENSOR READING STRUCTURE
// ============================================================================

typedef struct {
    uint32_t timestamp;             // Unix timestamp
    float conductivity;             // uS/cm
    float temperature;              // Celsius
    uint32_t water_meter1;          // Total gallons
    uint32_t water_meter2;          // Total gallons
    float flow_rate;                // GPM
    bool blowdown_active;
    bool pump1_active;
    bool pump2_active;
    bool pump3_active;
    uint16_t active_alarms;
} sensor_reading_t;

// ============================================================================
// EVENT STRUCTURE
// ============================================================================

typedef struct {
    uint32_t timestamp;
    char event_type[32];
    char description[128];
    int32_t value;
} event_log_t;

// ============================================================================
// ALARM STRUCTURE
// ============================================================================

typedef struct {
    uint32_t timestamp;
    uint16_t alarm_code;
    char alarm_name[32];
    bool active;
    float trigger_value;
} alarm_log_t;

// ============================================================================
// DATA LOGGER CLASS
// ============================================================================

class DataLogger {
public:
    DataLogger();

    /**
     * @brief Initialize data logger
     * @param config Pointer to system configuration
     * @return true if successful
     */
    bool begin(system_config_t* config);

    /**
     * @brief Connect to WiFi
     * @return true if connected
     */
    bool connectWiFi();

    /**
     * @brief Disconnect WiFi
     */
    void disconnectWiFi();

    /**
     * @brief Check WiFi connection status
     * @return true if connected
     */
    bool isWiFiConnected();

    /**
     * @brief Main update - call frequently
     */
    void update();

    /**
     * @brief Log sensor readings
     * @param reading Sensor reading structure
     */
    void logReading(sensor_reading_t* reading);

    /**
     * @brief Log event
     * @param event_type Event type string
     * @param description Event description
     * @param value Optional numeric value
     */
    void logEvent(const char* event_type, const char* description, int32_t value = 0);

    /**
     * @brief Log alarm
     * @param alarm_code Alarm code
     * @param alarm_name Alarm name
     * @param active True if alarm became active
     * @param trigger_value Value that triggered alarm
     */
    void logAlarm(uint16_t alarm_code, const char* alarm_name,
                  bool active, float trigger_value);

    /**
     * @brief Force upload of buffered data
     * @return Number of records uploaded
     */
    int forceUpload();

    /**
     * @brief Get number of pending records
     * @return Pending record count
     */
    int getPendingCount();

    /**
     * @brief Get last upload status
     * @return HTTP status code (200 = success)
     */
    int getLastUploadStatus();

    /**
     * @brief Get WiFi signal strength
     * @return RSSI in dBm
     */
    int getWiFiRSSI();

    /**
     * @brief Get server connection status
     * @return true if last upload was successful
     */
    bool isServerConnected();

    /**
     * @brief Set log interval
     * @param interval_ms Interval in milliseconds
     */
    void setLogInterval(uint32_t interval_ms);

    /**
     * @brief Enable/disable logging
     * @param enable True to enable
     */
    void setEnabled(bool enable);

    /**
     * @brief Start AP mode for configuration
     */
    void startAPMode();

    /**
     * @brief Stop AP mode
     */
    void stopAPMode();

    /**
     * @brief Check if in AP mode
     * @return true if AP mode active
     */
    bool isAPMode();

    /**
     * @brief Sync time with NTP
     * @return true if successful
     */
    bool syncTime();

    /**
     * @brief Get current timestamp
     * @return Unix timestamp
     */
    uint32_t getTimestamp();

private:
    system_config_t* _config;
    bool _enabled;
    bool _wifi_connected;
    bool _server_connected;
    bool _ap_mode;

    uint32_t _last_log_time;
    uint32_t _last_upload_time;
    uint32_t _log_interval;
    int _last_http_status;

    // Circular buffer for readings (store when offline)
    static const int BUFFER_SIZE = 100;
    sensor_reading_t _reading_buffer[BUFFER_SIZE];
    int _buffer_head;
    int _buffer_tail;
    int _buffer_count;

    // HTTP client
    HTTPClient _http;

    // Internal methods
    bool uploadReading(sensor_reading_t* reading);
    bool uploadEvent(event_log_t* event);
    bool uploadAlarm(alarm_log_t* alarm);
    void bufferReading(sensor_reading_t* reading);
    bool uploadBuffered();
    String buildReadingJSON(sensor_reading_t* reading);
    String buildEventJSON(event_log_t* event);
    String buildAlarmJSON(alarm_log_t* alarm);
    void handleWiFiEvents();
};

extern DataLogger dataLogger;

#endif // DATA_LOGGER_H

/**
 * @file sensor_health.h
 * @brief Runtime Sensor Health Monitor
 *
 * Tracks reading freshness, consecutive-failure counts, and safe-mode
 * transitions for all sensor inputs.  Integrates with DeviceManager
 * for fault reporting and with the alarm system for operator notification.
 *
 * Key features:
 *   - Stale reading detection (configurable max age)
 *   - Consecutive-failure hysteresis (N failures to fault, M good to clear)
 *   - Zero/out-of-range rejection for conductivity
 *   - I2C bus recovery (clock stretching reset)
 *   - Safe-mode entry/exit with valve-close + pump-stop
 *   - Boot reason tracking and abnormal-restart counter
 */

#ifndef SENSOR_HEALTH_H
#define SENSOR_HEALTH_H

#include <Arduino.h>
#include "device_manager.h"

// ============================================================================
// SAFE MODE DEFINITIONS
// ============================================================================

typedef enum {
    SAFE_MODE_NONE = 0,         // Normal operation
    SAFE_MODE_SENSOR_FAIL,      // Conductivity probe down
    SAFE_MODE_STALE_DATA,       // Measurement task stalled
    SAFE_MODE_BUS_FAIL,         // I2C or SPI bus hung
    SAFE_MODE_WATCHDOG_RESTART, // Post-watchdog recovery
    SAFE_MODE_CRITICAL_FAULT    // Multiple required devices failed
} safe_mode_t;

// ============================================================================
// HEALTH THRESHOLDS (configurable)
// ============================================================================

#define HEALTH_STALE_READING_MS         5000    // 5 seconds max age for conductivity
#define HEALTH_STALE_FEEDBACK_MS        3000    // 3 seconds max age for valve feedback
#define HEALTH_CONSECUTIVE_FAIL_LIMIT   3       // Failures before declaring fault
#define HEALTH_CONSECUTIVE_OK_LIMIT     3       // Good reads before clearing fault
#define HEALTH_COND_MINIMUM_VALID       0.1f    // Below this = probe disconnected (uS/cm)
#define HEALTH_COND_MAXIMUM_VALID       15000.0f // Above this = sensor error (uS/cm)
#define HEALTH_TEMP_SENTINEL            -999.0f // RTD error sentinel
#define HEALTH_I2C_RECOVERY_INTERVAL_MS 30000   // Try I2C recovery every 30s if faulted
#define HEALTH_SAFE_MODE_HOLD_MS        10000   // Min time in safe mode before auto-exit

// Alarm codes ALARM_STALE_DATA (0x4000) and ALARM_SAFE_MODE (0x8000)
// are defined in config.h alongside the other alarm bitmask values.

// ============================================================================
// SENSOR HEALTH TRACKER (per-sensor)
// ============================================================================

typedef struct {
    uint32_t last_good_time;        // millis() of last valid reading
    uint32_t last_read_time;        // millis() of last read attempt
    uint16_t consecutive_failures;  // Current streak of bad reads
    uint16_t consecutive_ok;        // Current streak of good reads
    uint16_t total_failures;        // Lifetime failure counter
    bool     stale;                 // Reading age > threshold
    bool     faulted;               // Declared faulted (after N failures)
    bool     value_suspect;         // Reading is out of expected range
} sensor_health_t;

// ============================================================================
// SENSOR HEALTH MONITOR CLASS
// ============================================================================

class SensorHealthMonitor {
public:
    SensorHealthMonitor();

    /** Initialize with current system state */
    void begin();

    /**
     * Called every control cycle (100ms) to evaluate sensor health.
     * Checks staleness, updates fault counters, and triggers safe mode
     * if necessary.
     */
    void update();

    // --- Conductivity Sensor ---

    /** Report a successful conductivity reading */
    void reportConductivityOK(float value);

    /** Report a failed conductivity reading */
    void reportConductivityFail();

    /** Check if conductivity reading is valid (not stale, not faulted) */
    bool isConductivityValid();

    /** Get conductivity health info */
    const sensor_health_t* getConductivityHealth();

    // --- Temperature Sensor ---

    void reportTemperatureOK(float value);
    void reportTemperatureFail();
    bool isTemperatureValid();
    const sensor_health_t* getTemperatureHealth();

    // --- Valve Feedback (ADS1115) ---

    void reportFeedbackOK(float mA);
    void reportFeedbackFail();
    bool isFeedbackValid();
    const sensor_health_t* getFeedbackHealth();

    // --- Measurement Freshness ---

    /**
     * Report that the measurement task completed a cycle.
     * Call from taskMeasurementLoop after successful read.
     */
    void reportMeasurementCycle();

    /**
     * Check if measurement data is fresh (not stale).
     * Call from taskControlLoop before acting on readings.
     */
    bool isMeasurementFresh();

    /** Get age of last measurement in milliseconds */
    uint32_t getMeasurementAge();

    // --- Safe Mode ---

    /** Get current safe mode state */
    safe_mode_t getSafeMode();

    /** Check if system is in any safe mode */
    bool isInSafeMode();

    /** Manually exit safe mode (operator acknowledgment) */
    void exitSafeMode();

    /** Get safe mode reason as string */
    const char* getSafeModeString();

    // --- I2C Bus Recovery ---

    /**
     * Attempt I2C bus recovery by sending 9 clock pulses.
     * Call when I2C operations are timing out.
     */
    bool attemptI2CRecovery();

    // --- Statistics ---

    /** Print health summary to Serial */
    void printStatus();

private:
    sensor_health_t _cond_health;
    sensor_health_t _temp_health;
    sensor_health_t _feedback_health;

    uint32_t    _last_measurement_time;   // Updated by measurement task
    safe_mode_t _safe_mode;
    uint32_t    _safe_mode_enter_time;
    uint32_t    _last_i2c_recovery_time;

    // Internal helpers
    void checkStaleness();
    void evaluateSafeMode();
    void enterSafeMode(safe_mode_t reason);
    void handleSensorFault(sensor_health_t* health, device_id_t dev_id);
    void handleSensorOK(sensor_health_t* health, device_id_t dev_id);
    bool isValueInRange(float value, float min, float max);
    void initHealth(sensor_health_t* health);
};

// Global instance
extern SensorHealthMonitor sensorHealth;

#endif // SENSOR_HEALTH_H

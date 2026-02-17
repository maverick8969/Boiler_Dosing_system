/**
 * @file sensor_health.cpp
 * @brief Runtime Sensor Health Monitor Implementation
 */

#include "sensor_health.h"
#include "pin_definitions.h"
#include <Wire.h>

// Global instance
SensorHealthMonitor sensorHealth;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SensorHealthMonitor::SensorHealthMonitor()
    : _last_measurement_time(0)
    , _safe_mode(SAFE_MODE_NONE)
    , _safe_mode_enter_time(0)
    , _last_i2c_recovery_time(0)
{
    initHealth(&_cond_health);
    initHealth(&_temp_health);
    initHealth(&_feedback_health);
}

void SensorHealthMonitor::initHealth(sensor_health_t* health) {
    health->last_good_time       = 0;
    health->last_read_time       = 0;
    health->consecutive_failures = 0;
    health->consecutive_ok       = 0;
    health->total_failures       = 0;
    health->stale                = true;   // Stale until first good read
    health->faulted              = false;
    health->value_suspect        = false;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void SensorHealthMonitor::begin() {
    uint32_t now = millis();
    _last_measurement_time = now;

    // Reset all health trackers
    initHealth(&_cond_health);
    initHealth(&_temp_health);
    initHealth(&_feedback_health);

    _safe_mode = SAFE_MODE_NONE;

    Serial.println("SensorHealthMonitor initialized");
}

// ============================================================================
// UPDATE (called every control cycle)
// ============================================================================

void SensorHealthMonitor::update() {
    checkStaleness();
    evaluateSafeMode();
}

// ============================================================================
// CONDUCTIVITY SENSOR
// ============================================================================

void SensorHealthMonitor::reportConductivityOK(float value) {
    _cond_health.last_read_time = millis();

    // Range check
    if (!isValueInRange(value, HEALTH_COND_MINIMUM_VALID, HEALTH_COND_MAXIMUM_VALID)) {
        _cond_health.value_suspect = true;
        // Treat out-of-range as a failure for fault counting
        handleSensorFault(&_cond_health, DEV_CONDUCTIVITY_PROBE);
        Serial.printf("SensorHealth: Conductivity %.1f out of range [%.1f - %.1f]\n",
                       value, HEALTH_COND_MINIMUM_VALID, HEALTH_COND_MAXIMUM_VALID);
        return;
    }

    _cond_health.value_suspect = false;
    handleSensorOK(&_cond_health, DEV_CONDUCTIVITY_PROBE);
}

void SensorHealthMonitor::reportConductivityFail() {
    _cond_health.last_read_time = millis();
    handleSensorFault(&_cond_health, DEV_CONDUCTIVITY_PROBE);
}

bool SensorHealthMonitor::isConductivityValid() {
    return !_cond_health.faulted && !_cond_health.stale && !_cond_health.value_suspect;
}

const sensor_health_t* SensorHealthMonitor::getConductivityHealth() {
    return &_cond_health;
}

// ============================================================================
// TEMPERATURE SENSOR
// ============================================================================

void SensorHealthMonitor::reportTemperatureOK(float value) {
    _temp_health.last_read_time = millis();

    // Sentinel check
    if (value <= HEALTH_TEMP_SENTINEL + 1.0f) {
        handleSensorFault(&_temp_health, DEV_TEMP_RTD);
        return;
    }

    _temp_health.value_suspect = false;
    handleSensorOK(&_temp_health, DEV_TEMP_RTD);
}

void SensorHealthMonitor::reportTemperatureFail() {
    _temp_health.last_read_time = millis();
    handleSensorFault(&_temp_health, DEV_TEMP_RTD);
}

bool SensorHealthMonitor::isTemperatureValid() {
    return !_temp_health.faulted && !_temp_health.stale;
}

const sensor_health_t* SensorHealthMonitor::getTemperatureHealth() {
    return &_temp_health;
}

// ============================================================================
// VALVE FEEDBACK (ADS1115)
// ============================================================================

void SensorHealthMonitor::reportFeedbackOK(float mA) {
    _feedback_health.last_read_time = millis();
    _feedback_health.value_suspect = false;
    handleSensorOK(&_feedback_health, DEV_VALVE_FEEDBACK);
}

void SensorHealthMonitor::reportFeedbackFail() {
    _feedback_health.last_read_time = millis();
    handleSensorFault(&_feedback_health, DEV_VALVE_FEEDBACK);
}

bool SensorHealthMonitor::isFeedbackValid() {
    return !_feedback_health.faulted && !_feedback_health.stale;
}

const sensor_health_t* SensorHealthMonitor::getFeedbackHealth() {
    return &_feedback_health;
}

// ============================================================================
// MEASUREMENT FRESHNESS
// ============================================================================

void SensorHealthMonitor::reportMeasurementCycle() {
    _last_measurement_time = millis();
}

bool SensorHealthMonitor::isMeasurementFresh() {
    return getMeasurementAge() < HEALTH_STALE_READING_MS;
}

uint32_t SensorHealthMonitor::getMeasurementAge() {
    return millis() - _last_measurement_time;
}

// ============================================================================
// SAFE MODE
// ============================================================================

safe_mode_t SensorHealthMonitor::getSafeMode() {
    return _safe_mode;
}

bool SensorHealthMonitor::isInSafeMode() {
    return _safe_mode != SAFE_MODE_NONE;
}

void SensorHealthMonitor::exitSafeMode() {
    if (_safe_mode == SAFE_MODE_NONE) return;

    uint32_t duration = millis() - _safe_mode_enter_time;

    // Require minimum hold time before allowing exit
    if (duration < HEALTH_SAFE_MODE_HOLD_MS) {
        Serial.printf("SensorHealth: Safe mode hold — %lu ms remaining\n",
                       HEALTH_SAFE_MODE_HOLD_MS - duration);
        return;
    }

    Serial.printf("SensorHealth: Exiting safe mode (was: %s, duration: %lu ms)\n",
                   getSafeModeString(), duration);
    _safe_mode = SAFE_MODE_NONE;
}

const char* SensorHealthMonitor::getSafeModeString() {
    switch (_safe_mode) {
        case SAFE_MODE_NONE:             return "NORMAL";
        case SAFE_MODE_SENSOR_FAIL:      return "SENSOR FAIL";
        case SAFE_MODE_STALE_DATA:       return "STALE DATA";
        case SAFE_MODE_BUS_FAIL:         return "BUS FAIL";
        case SAFE_MODE_WATCHDOG_RESTART: return "WDT RESTART";
        case SAFE_MODE_CRITICAL_FAULT:   return "CRITICAL";
        default:                         return "UNKNOWN";
    }
}

// ============================================================================
// I2C BUS RECOVERY
// ============================================================================

bool SensorHealthMonitor::attemptI2CRecovery() {
    uint32_t now = millis();

    // Rate-limit recovery attempts
    if (now - _last_i2c_recovery_time < HEALTH_I2C_RECOVERY_INTERVAL_MS) {
        return false;
    }
    _last_i2c_recovery_time = now;

    Serial.println("SensorHealth: Attempting I2C bus recovery...");

    // Step 1: End the current Wire session
    Wire.end();
    delay(10);

    // Step 2: Manually toggle SCL 9 times to release stuck SDA
    // This frees a slave that is holding SDA low mid-transfer
    pinMode(I2C_SCL_PIN, OUTPUT);
    pinMode(I2C_SDA_PIN, INPUT_PULLUP);

    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL_PIN, LOW);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL_PIN, HIGH);
        delayMicroseconds(5);
    }

    // Step 3: Generate a STOP condition
    pinMode(I2C_SDA_PIN, OUTPUT);
    digitalWrite(I2C_SDA_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C_SCL_PIN, HIGH);
    delayMicroseconds(5);
    digitalWrite(I2C_SDA_PIN, HIGH);
    delayMicroseconds(5);

    // Step 4: Re-initialize Wire
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ);

    // Step 5: Verify by scanning for known devices
    bool recovered = false;

    Wire.beginTransmission(LCD_I2C_ADDR);
    if (Wire.endTransmission() == 0) recovered = true;

    Wire.beginTransmission(ADS1115_I2C_ADDR);
    if (Wire.endTransmission() == 0) recovered = true;

    if (recovered) {
        Serial.println("SensorHealth: I2C bus recovered successfully");
    } else {
        Serial.println("SensorHealth: I2C bus recovery FAILED — no devices found");
    }

    return recovered;
}

// ============================================================================
// STATUS
// ============================================================================

void SensorHealthMonitor::printStatus() {
    Serial.println("=== Sensor Health Status ===");

    Serial.printf("  Conductivity: %s (fails: %u/%u, age: %lu ms)\n",
        isConductivityValid() ? "VALID" : "INVALID",
        _cond_health.consecutive_failures, _cond_health.total_failures,
        millis() - _cond_health.last_good_time);

    Serial.printf("  Temperature:  %s (fails: %u/%u, age: %lu ms)\n",
        isTemperatureValid() ? "VALID" : "INVALID",
        _temp_health.consecutive_failures, _temp_health.total_failures,
        millis() - _temp_health.last_good_time);

    Serial.printf("  Feedback:     %s (fails: %u/%u, age: %lu ms)\n",
        isFeedbackValid() ? "VALID" : "INVALID",
        _feedback_health.consecutive_failures, _feedback_health.total_failures,
        millis() - _feedback_health.last_good_time);

    Serial.printf("  Measurement:  %s (age: %lu ms)\n",
        isMeasurementFresh() ? "FRESH" : "STALE",
        getMeasurementAge());

    Serial.printf("  Safe Mode:    %s\n", getSafeModeString());

    Serial.println("============================");
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void SensorHealthMonitor::checkStaleness() {
    uint32_t now = millis();

    // Conductivity staleness
    if (_cond_health.last_good_time > 0) {
        _cond_health.stale = (now - _cond_health.last_good_time > HEALTH_STALE_READING_MS);
    }

    // Temperature staleness
    if (_temp_health.last_good_time > 0) {
        _temp_health.stale = (now - _temp_health.last_good_time > HEALTH_STALE_READING_MS);
    }

    // Feedback staleness
    if (_feedback_health.last_good_time > 0) {
        _feedback_health.stale = (now - _feedback_health.last_good_time > HEALTH_STALE_FEEDBACK_MS);
    }
}

void SensorHealthMonitor::evaluateSafeMode() {
    // Already in safe mode — check if we can auto-exit
    if (_safe_mode != SAFE_MODE_NONE) {
        // Auto-exit if all required sensors recovered
        bool cond_ok = isConductivityValid() || !deviceManager.isEnabled(DEV_CONDUCTIVITY_PROBE);
        bool meas_ok = isMeasurementFresh();

        if (cond_ok && meas_ok) {
            uint32_t hold_time = millis() - _safe_mode_enter_time;
            if (hold_time >= HEALTH_SAFE_MODE_HOLD_MS) {
                Serial.println("SensorHealth: Auto-exiting safe mode — sensors recovered");
                _safe_mode = SAFE_MODE_NONE;
            }
        }
        return;
    }

    // Check for conditions that should trigger safe mode

    // 1. Conductivity probe faulted (required device)
    if (_cond_health.faulted && deviceManager.isEnabled(DEV_CONDUCTIVITY_PROBE)) {
        enterSafeMode(SAFE_MODE_SENSOR_FAIL);
        return;
    }

    // 2. Measurement task stalled
    if (!isMeasurementFresh() && _last_measurement_time > 0) {
        enterSafeMode(SAFE_MODE_STALE_DATA);
        return;
    }
}

void SensorHealthMonitor::enterSafeMode(safe_mode_t reason) {
    if (_safe_mode != SAFE_MODE_NONE) return;  // Already in safe mode

    _safe_mode = reason;
    _safe_mode_enter_time = millis();

    Serial.printf("*** SAFE MODE ENTERED: %s ***\n", getSafeModeString());

    // NOTE: The caller (control loop in main.cpp) is responsible for
    // actually closing the valve and stopping pumps when it detects
    // sensorHealth.isInSafeMode() == true. This module only tracks state;
    // it does not directly control hardware to avoid layering violations.
}

void SensorHealthMonitor::handleSensorFault(sensor_health_t* health, device_id_t dev_id) {
    health->consecutive_failures++;
    health->consecutive_ok = 0;
    health->total_failures++;

    if (health->consecutive_failures >= HEALTH_CONSECUTIVE_FAIL_LIMIT) {
        if (!health->faulted) {
            health->faulted = true;
            Serial.printf("SensorHealth: %s FAULTED after %d consecutive failures\n",
                deviceManager.getDeviceInfo(dev_id)->name,
                health->consecutive_failures);
        }
        deviceManager.reportFault(dev_id);
    }
}

void SensorHealthMonitor::handleSensorOK(sensor_health_t* health, device_id_t dev_id) {
    health->last_good_time = millis();
    health->consecutive_ok++;
    health->stale = false;

    // Require N consecutive good reads to clear fault
    if (health->consecutive_ok >= HEALTH_CONSECUTIVE_OK_LIMIT) {
        health->consecutive_failures = 0;

        if (health->faulted) {
            health->faulted = false;
            Serial.printf("SensorHealth: %s recovered after %d consecutive good reads\n",
                deviceManager.getDeviceInfo(dev_id)->name,
                health->consecutive_ok);
        }
        deviceManager.reportOK(dev_id);
    }
}

bool SensorHealthMonitor::isValueInRange(float value, float min, float max) {
    return (value >= min && value <= max);
}

/**
 * @file device_manager.cpp
 * @brief Centralized Hardware Device Registry Implementation
 */

#include "device_manager.h"
#include "config.h"

// Global instance
DeviceManager deviceManager;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

DeviceManager::DeviceManager()
    : _enabled_ptr(nullptr)
{
    // Initialize all device slots
    initDevice(DEV_CONDUCTIVITY_PROBE, "Cond Probe",      true);   // Required
    initDevice(DEV_TEMP_RTD,           "Temp RTD",         false);
    initDevice(DEV_BLOWDOWN_VALVE,     "Blowdown Valve",  true);   // Required
    initDevice(DEV_VALVE_FEEDBACK,     "Valve Feedback",   false);
    initDevice(DEV_PUMP_H2SO3,         "Pump H2SO3",      false);
    initDevice(DEV_PUMP_NAOH,          "Pump NaOH",       false);
    initDevice(DEV_PUMP_AMINE,         "Pump Amine",      false);
    initDevice(DEV_WATER_METER_1,      "Water Meter 1",   false);
    initDevice(DEV_WATER_METER_2,      "Water Meter 2",   false);
    initDevice(DEV_LCD_DISPLAY,        "LCD Display",      false);
    initDevice(DEV_SD_CARD,            "SD Card",          false);
    initDevice(DEV_WIFI,               "WiFi",             false);
    initDevice(DEV_AUX_INPUT_1,        "Drum Level Sw",   false);
    initDevice(DEV_FEEDWATER_MONITOR,  "FW Pump Monitor",  false);
}

void DeviceManager::initDevice(device_id_t id, const char* name, bool required) {
    if (id >= DEV_COUNT) return;
    _devices[id].name          = name;
    _devices[id].state         = DEVSTATE_DISABLED;
    _devices[id].required      = required;
    _devices[id].enabled       = required;  // Required devices always enabled
    _devices[id].installed     = false;
    _devices[id].healthy       = false;
    _devices[id].fault_count   = 0;
    _devices[id].total_faults  = 0;
    _devices[id].last_ok_time  = 0;
    _devices[id].last_fault_time = 0;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void DeviceManager::begin(uint16_t* enabled_devices_ptr) {
    _enabled_ptr = enabled_devices_ptr;

    if (_enabled_ptr) {
        // Apply stored enable flags
        for (int i = 0; i < DEV_COUNT; i++) {
            if (_devices[i].required) {
                _devices[i].enabled = true;  // Always force required ON
            } else {
                _devices[i].enabled = (*_enabled_ptr >> i) & 0x01;
            }
            updateState((device_id_t)i);
        }
    } else {
        // No config — enable all
        for (int i = 0; i < DEV_COUNT; i++) {
            _devices[i].enabled = true;
            updateState((device_id_t)i);
        }
    }

    Serial.println("DeviceManager initialized");
}

// ============================================================================
// ENABLE / DISABLE
// ============================================================================

bool DeviceManager::setEnabled(device_id_t id, bool enable) {
    if (id >= DEV_COUNT) return false;

    // Cannot disable required devices
    if (!enable && _devices[id].required) {
        Serial.printf("DeviceManager: %s is REQUIRED, cannot disable\n",
                       _devices[id].name);
        return false;
    }

    _devices[id].enabled = enable;

    // Update stored config bitmask
    if (_enabled_ptr) {
        if (enable) {
            *_enabled_ptr |= (1 << id);
        } else {
            *_enabled_ptr &= ~(1 << id);
        }
    }

    updateState(id);

    Serial.printf("DeviceManager: %s %s\n",
                   _devices[id].name, enable ? "ENABLED" : "DISABLED");
    return true;
}

bool DeviceManager::isEnabled(device_id_t id) {
    if (id >= DEV_COUNT) return false;
    return _devices[id].enabled;
}

bool DeviceManager::isOperational(device_id_t id) {
    if (id >= DEV_COUNT) return false;
    return _devices[id].enabled && _devices[id].installed && _devices[id].healthy;
}

bool DeviceManager::isRequired(device_id_t id) {
    if (id >= DEV_COUNT) return false;
    return _devices[id].required;
}

// ============================================================================
// POST RESULTS
// ============================================================================

void DeviceManager::setInstalled(device_id_t id, bool found) {
    if (id >= DEV_COUNT) return;
    _devices[id].installed = found;
    _devices[id].healthy = found;  // Assume healthy if found at POST
    if (found) {
        _devices[id].last_ok_time = millis();
    }
    updateState(id);
}

bool DeviceManager::isInstalled(device_id_t id) {
    if (id >= DEV_COUNT) return false;
    return _devices[id].installed;
}

// ============================================================================
// RUNTIME FAULT TRACKING
// ============================================================================

void DeviceManager::reportFault(device_id_t id) {
    if (id >= DEV_COUNT) return;

    _devices[id].fault_count++;
    _devices[id].total_faults++;
    _devices[id].last_fault_time = millis();

    // Declare faulted after N consecutive failures
    if (_devices[id].fault_count >= FAULT_CONSECUTIVE_THRESHOLD) {
        _devices[id].healthy = false;
        updateState(id);
    }
}

void DeviceManager::reportOK(device_id_t id) {
    if (id >= DEV_COUNT) return;

    _devices[id].last_ok_time = millis();

    // A single good operation resets the consecutive fault counter.
    // The caller (SensorHealthMonitor) handles N-of-M hysteresis;
    // DeviceManager trusts the caller's judgment on recovery.
    _devices[id].fault_count = 0;

    if (!_devices[id].healthy) {
        _devices[id].healthy = true;
        _devices[id].installed = true;  // Re-mark as installed if recovered
        updateState(id);
        Serial.printf("DeviceManager: %s recovered from fault\n",
                       _devices[id].name);
    }
}

bool DeviceManager::isFaulted(device_id_t id) {
    if (id >= DEV_COUNT) return true;
    return _devices[id].enabled && _devices[id].installed && !_devices[id].healthy;
}

uint16_t DeviceManager::getFaultCount(device_id_t id) {
    if (id >= DEV_COUNT) return 0;
    return _devices[id].fault_count;
}

void DeviceManager::clearFault(device_id_t id) {
    if (id >= DEV_COUNT) return;
    _devices[id].fault_count = 0;
    _devices[id].healthy = _devices[id].installed;
    updateState(id);
}

// ============================================================================
// DEPENDENCY CHECKING
// ============================================================================

bool DeviceManager::checkPumpModeDependency(uint8_t pump_id, uint8_t feed_mode,
                                              const char** dep_name) {
    *dep_name = nullptr;

    switch (feed_mode) {
        case 1: // FEED_MODE_A_BLOWDOWN_FEED
            if (!isOperational(DEV_BLOWDOWN_VALVE)) {
                *dep_name = _devices[DEV_BLOWDOWN_VALVE].name;
                return false;
            }
            break;

        case 4: // FEED_MODE_D_WATER_CONTACT
            if (!isOperational(DEV_WATER_METER_1) &&
                !isOperational(DEV_WATER_METER_2)) {
                *dep_name = "Water Meter";
                return false;
            }
            break;

        case 5: // FEED_MODE_E_PADDLEWHEEL
            if (!isOperational(DEV_WATER_METER_1) &&
                !isOperational(DEV_WATER_METER_2)) {
                *dep_name = "Water Meter";
                return false;
            }
            break;

        case 7: // FEED_MODE_F_FUZZY
            // Needs conductivity probe + at least one water meter
            if (!isOperational(DEV_CONDUCTIVITY_PROBE)) {
                *dep_name = _devices[DEV_CONDUCTIVITY_PROBE].name;
                return false;
            }
            if (!isOperational(DEV_WATER_METER_1) &&
                !isOperational(DEV_WATER_METER_2)) {
                *dep_name = "Water Meter";
                return false;
            }
            break;
    }

    // Check that the pump itself is operational
    device_id_t pump_dev = (device_id_t)(DEV_PUMP_H2SO3 + pump_id);
    if (pump_dev < DEV_COUNT && !isOperational(pump_dev)) {
        *dep_name = _devices[pump_dev].name;
        return false;
    }

    return true;
}

bool DeviceManager::isBlowdownFeedbackAvailable() {
    return isOperational(DEV_VALVE_FEEDBACK);
}

// ============================================================================
// STATUS QUERIES
// ============================================================================

const device_info_t* DeviceManager::getDeviceInfo(device_id_t id) {
    if (id >= DEV_COUNT) return nullptr;
    return &_devices[id];
}

device_state_t DeviceManager::getDeviceState(device_id_t id) {
    if (id >= DEV_COUNT) return DEVSTATE_DISABLED;
    return _devices[id].state;
}

const char* DeviceManager::getStateString(device_id_t id) {
    if (id >= DEV_COUNT) return "INVALID";

    switch (_devices[id].state) {
        case DEVSTATE_DISABLED:  return "DISABLED";
        case DEVSTATE_NOT_FOUND: return "NOT FOUND";
        case DEVSTATE_FAULTED:   return "FAULTED";
        case DEVSTATE_OK:        return "OK";
        default:                 return "UNKNOWN";
    }
}

uint16_t DeviceManager::getEnabledMask() {
    uint16_t mask = 0;
    for (int i = 0; i < DEV_COUNT; i++) {
        if (_devices[i].enabled) mask |= (1 << i);
    }
    return mask;
}

uint16_t DeviceManager::getFaultedMask() {
    uint16_t mask = 0;
    for (int i = 0; i < DEV_COUNT; i++) {
        if (isFaulted((device_id_t)i)) mask |= (1 << i);
    }
    return mask;
}

uint8_t DeviceManager::countOperational() {
    uint8_t count = 0;
    for (int i = 0; i < DEV_COUNT; i++) {
        if (isOperational((device_id_t)i)) count++;
    }
    return count;
}

uint8_t DeviceManager::countFaulted() {
    uint8_t count = 0;
    for (int i = 0; i < DEV_COUNT; i++) {
        if (isFaulted((device_id_t)i)) count++;
    }
    return count;
}

void DeviceManager::printStatus() {
    Serial.println("=== Device Manager Status ===");
    for (int i = 0; i < DEV_COUNT; i++) {
        Serial.printf("  [%2d] %-16s  %s%s  %s",
            i,
            _devices[i].name,
            _devices[i].enabled ? "EN " : "DIS",
            _devices[i].required ? " (REQ)" : "      ",
            getStateString((device_id_t)i)
        );
        if (_devices[i].total_faults > 0) {
            Serial.printf("  (faults: %u/%u)",
                _devices[i].fault_count, _devices[i].total_faults);
        }
        Serial.println();
    }
    Serial.printf("  Operational: %d/%d   Faulted: %d\n",
        countOperational(), DEV_COUNT, countFaulted());
    Serial.println("=============================");
}

// ============================================================================
// PRIVATE
// ============================================================================

void DeviceManager::updateState(device_id_t id) {
    if (id >= DEV_COUNT) return;

    device_info_t& d = _devices[id];

    if (!d.enabled) {
        d.state = DEVSTATE_DISABLED;
    } else if (!d.installed) {
        d.state = DEVSTATE_NOT_FOUND;
    } else if (!d.healthy) {
        d.state = DEVSTATE_FAULTED;
    } else {
        d.state = DEVSTATE_OK;
    }
}

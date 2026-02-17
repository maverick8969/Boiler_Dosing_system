/**
 * @file device_manager.h
 * @brief Centralized Hardware Device Registry
 *
 * Manages enable/disable state, fault tracking, and dependency enforcement
 * for all optional and required hardware peripherals.
 *
 * Usage:
 *   deviceManager.setEnabled(DEV_PUMP_NAOH, false);   // Disable pump 2
 *   if (deviceManager.isOperational(DEV_WATER_METER_1)) { ... }
 *   if (deviceManager.checkDependency(DEV_PUMP_H2SO3, FEED_MODE_D)) { ... }
 */

#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>

// ============================================================================
// DEVICE IDENTIFIERS (bit positions in the 16-bit flags)
// ============================================================================

typedef enum {
    DEV_CONDUCTIVITY_PROBE = 0,
    DEV_TEMP_RTD           = 1,
    DEV_BLOWDOWN_VALVE     = 2,
    DEV_VALVE_FEEDBACK     = 3,
    DEV_PUMP_H2SO3         = 4,
    DEV_PUMP_NAOH          = 5,
    DEV_PUMP_AMINE         = 6,
    DEV_WATER_METER_1      = 7,
    DEV_WATER_METER_2      = 8,
    DEV_LCD_DISPLAY        = 9,
    DEV_SD_CARD            = 10,
    DEV_WIFI               = 11,
    DEV_AUX_INPUT_1        = 12,
    DEV_FEEDWATER_MONITOR  = 13,
    DEV_COUNT              = 14
} device_id_t;

// ============================================================================
// DEVICE STATE
// ============================================================================

typedef enum {
    DEVSTATE_DISABLED       = 0,   // User-disabled in config
    DEVSTATE_NOT_FOUND      = 1,   // Enabled but not detected at POST
    DEVSTATE_FAULTED        = 2,   // Detected but currently faulted
    DEVSTATE_OK             = 3    // Operational
} device_state_t;

// ============================================================================
// DEVICE INFO STRUCTURE (per device)
// ============================================================================

typedef struct {
    const char*    name;           // Human-readable name
    device_state_t state;          // Current state
    bool           required;       // True = cannot be disabled by user
    bool           enabled;        // User-configurable enable flag
    bool           installed;      // Detected at POST
    bool           healthy;        // Runtime health (no faults)
    uint16_t       fault_count;    // Consecutive fault counter
    uint16_t       total_faults;   // Lifetime fault counter (for diagnostics)
    uint32_t       last_ok_time;   // millis() of last successful operation
    uint32_t       last_fault_time;// millis() of last fault
} device_info_t;

// ============================================================================
// HARDWARE CONFIG (stored in NVS as part of system_config_t)
// ============================================================================

typedef struct {
    uint16_t enabled_devices;      // Bitmask: bit N = device N enabled by user
} hardware_config_t;

// Default: all devices enabled
#define HW_CONFIG_DEFAULT_ENABLED   0x3FFF  // bits 0-13 set

// ============================================================================
// FAULT THRESHOLDS
// ============================================================================

#define FAULT_CONSECUTIVE_THRESHOLD  3     // Faults before declaring device faulted
#define FAULT_RECOVERY_THRESHOLD     3     // Good reads before clearing fault
#define STALE_READING_MS             5000  // Max age before reading is stale

// ============================================================================
// DEVICE MANAGER CLASS
// ============================================================================

class DeviceManager {
public:
    DeviceManager();

    /**
     * Initialize the device manager with stored configuration.
     * Call after loadConfiguration() so enabled_devices is populated.
     */
    void begin(hardware_config_t* config);

    // --- Enable / Disable ---

    /** Enable or disable a device. Returns false if device is required. */
    bool setEnabled(device_id_t id, bool enable);

    /** Check if device is enabled by user */
    bool isEnabled(device_id_t id);

    /** Check if device is enabled AND installed AND healthy */
    bool isOperational(device_id_t id);

    /** Check if device is required (cannot be disabled) */
    bool isRequired(device_id_t id);

    // --- POST (Power-On Self-Test) Results ---

    /** Mark a device as installed (found at POST) */
    void setInstalled(device_id_t id, bool found);

    /** Check if device was detected at POST */
    bool isInstalled(device_id_t id);

    // --- Runtime Fault Tracking ---

    /** Report a fault on a device (increments consecutive counter) */
    void reportFault(device_id_t id);

    /** Report a successful operation (decrements consecutive counter) */
    void reportOK(device_id_t id);

    /** Check if device is currently faulted */
    bool isFaulted(device_id_t id);

    /** Get consecutive fault count */
    uint16_t getFaultCount(device_id_t id);

    /** Clear fault state (manual reset) */
    void clearFault(device_id_t id);

    // --- Dependency Checking ---

    /**
     * Check if a pump feed mode's dependencies are met.
     * Returns true if all required devices for the mode are operational.
     * If not met, sets dep_name to the name of the missing device.
     */
    bool checkPumpModeDependency(uint8_t pump_id, uint8_t feed_mode,
                                  const char** dep_name);

    /**
     * Check if blowdown feedback dependency is met.
     * Returns true if ADS1115 valve feedback is operational.
     */
    bool isBlowdownFeedbackAvailable();

    // --- Status ---

    /** Get device info struct (read-only) */
    const device_info_t* getDeviceInfo(device_id_t id);

    /** Get overall state enum for a device */
    device_state_t getDeviceState(device_id_t id);

    /** Get human-readable state string */
    const char* getStateString(device_id_t id);

    /** Get the enabled_devices bitmask for NVS storage */
    uint16_t getEnabledMask();

    /** Count of operational devices */
    uint8_t countOperational();

    /** Count of faulted devices */
    uint8_t countFaulted();

    /** Print status of all devices to Serial */
    void printStatus();

private:
    device_info_t       _devices[DEV_COUNT];
    hardware_config_t*  _config;

    void updateState(device_id_t id);
    void initDevice(device_id_t id, const char* name, bool required);
};

// Global instance
extern DeviceManager deviceManager;

#endif // DEVICE_MANAGER_H

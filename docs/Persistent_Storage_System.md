# Persistent Storage System

## Overview

The CT-6 Boiler Controller uses the ESP32's Non-Volatile Storage (NVS) system to persist all configuration settings and operational data across power cycles. This document explains the storage architecture, what is saved, and how to implement additional persistent data.

---

## ESP32 NVS (Non-Volatile Storage)

### What is NVS?

NVS is a key-value storage system built into the ESP32 that uses a portion of flash memory. It provides:

- **Persistence**: Data survives power cycles and resets
- **Wear leveling**: Automatic distribution of writes to extend flash life
- **Atomic operations**: Safe write operations even during power loss
- **Type safety**: Supports integers, strings, and binary blobs

### NVS Partition

The default NVS partition is typically 20KB, which is more than sufficient for this application. The partition is defined in the partition table:

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
```

---

## Storage Architecture

### Namespace Structure

All controller data is stored in the `boiler_cfg` namespace:

```c
#define NVS_NAMESPACE  "boiler_cfg"
```

### Data Categories

| Category | Storage Method | Update Frequency |
|----------|---------------|------------------|
| Configuration | Binary blob | On change |
| Water meter totals | Individual uint32 | Every 5 minutes |
| Pump statistics | Individual uint32 | On shutdown |
| Calibration data | Part of config blob | On calibration |
| Network credentials | Part of config blob | On change |

---

## Configuration Structure

The main configuration is stored as a single binary blob for efficient read/write operations:

```c
typedef struct {
    // Magic number and version for validation
    uint32_t magic;                 // CONFIG_MAGIC (0x43543630 = "CT60")
    uint16_t version;               // Configuration version
    uint16_t checksum;              // CRC16 checksum

    // All subsystem configurations...
    conductivity_config_t conductivity;
    blowdown_config_t blowdown;
    pump_config_t pumps[3];
    water_meter_config_t meters[2];
    alarm_config_t alarms;
    feed_schedule_entry_t schedules[12];

    // Network configuration
    char wifi_ssid[32];
    char wifi_password[64];
    char tsdb_host[64];
    uint16_t tsdb_port;
    uint32_t log_interval_ms;

    // Security and display
    uint16_t access_code;
    bool access_code_enabled;
    uint8_t lcd_contrast;
    uint8_t led_brightness;
    // ...
} system_config_t;
```

### Configuration Validation

On load, the configuration is validated:

1. **Magic number check**: Ensures data is valid configuration
2. **Version check**: Handles firmware upgrades
3. **Checksum verification**: Detects data corruption

```c
bool isConfigValid(system_config_t* config) {
    // Check magic number
    if (config->magic != CONFIG_MAGIC) {
        return false;
    }

    // Check version compatibility
    if (config->version > CONFIG_VERSION) {
        return false;  // Downgrade not supported
    }

    // Verify checksum
    uint16_t calculated = calculateCRC16(config, sizeof(system_config_t) - 2);
    if (calculated != config->checksum) {
        return false;
    }

    return true;
}
```

---

## Storage Keys

### Main Configuration
| Key | Type | Size | Description |
|-----|------|------|-------------|
| `config` | Blob | ~1KB | Complete system configuration |

### Operational Data
| Key | Type | Size | Description |
|-----|------|------|-------------|
| `wm1_total` | uint32 | 4 bytes | Water meter 1 totalizer |
| `wm2_total` | uint32 | 4 bytes | Water meter 2 totalizer |
| `pump1_tot` | uint32 | 4 bytes | Pump 1 total runtime (seconds) |
| `pump2_tot` | uint32 | 4 bytes | Pump 2 total runtime (seconds) |
| `pump3_tot` | uint32 | 4 bytes | Pump 3 total runtime (seconds) |
| `blow_total` | uint32 | 4 bytes | Total blowdown time (seconds) |
| `last_cal` | uint32 | 4 bytes | Last calibration timestamp |

### Event Counters
| Key | Type | Size | Description |
|-----|------|------|-------------|
| `boot_count` | uint32 | 4 bytes | Number of boots |
| `alarm_count` | uint32 | 4 bytes | Total alarm events |

---

## Implementation Code

### Loading Configuration

```c
#include <Preferences.h>

Preferences preferences;

void loadConfiguration() {
    preferences.begin(NVS_NAMESPACE, true);  // Read-only mode

    // Check if config exists
    size_t config_size = preferences.getBytesLength(NVS_KEY_CONFIG);

    if (config_size == sizeof(system_config_t)) {
        // Load existing configuration
        preferences.getBytes(NVS_KEY_CONFIG, &systemConfig, sizeof(system_config_t));

        // Validate configuration
        if (!isConfigValid(&systemConfig)) {
            Serial.println("Invalid config - loading defaults");
            initializeDefaults();
        }
    } else {
        // No configuration found
        Serial.println("No config found - loading defaults");
        initializeDefaults();
    }

    preferences.end();
}
```

### Saving Configuration

```c
void saveConfiguration() {
    // Update checksum before saving
    systemConfig.checksum = calculateCRC16(&systemConfig,
                                           sizeof(system_config_t) - 2);

    preferences.begin(NVS_NAMESPACE, false);  // Read-write mode
    preferences.putBytes(NVS_KEY_CONFIG, &systemConfig, sizeof(system_config_t));
    preferences.end();

    Serial.println("Configuration saved to NVS");
}
```

### Saving Individual Values

```c
void saveWaterMeterTotal(uint8_t meter_id, uint32_t total) {
    preferences.begin(NVS_NAMESPACE, false);

    const char* key = (meter_id == 0) ? "wm1_total" : "wm2_total";
    preferences.putUInt(key, total);

    preferences.end();
}

void loadWaterMeterTotal(uint8_t meter_id, uint32_t* total) {
    preferences.begin(NVS_NAMESPACE, true);

    const char* key = (meter_id == 0) ? "wm1_total" : "wm2_total";
    *total = preferences.getUInt(key, 0);

    preferences.end();
}
```

---

## Auto-Save Strategy

### Configuration Changes

Settings are saved immediately when:
- User exits edit mode in menu
- Network connection changes
- Calibration is performed

```c
void onSettingChanged() {
    saveConfiguration();
    Serial.println("Settings auto-saved");
}
```

### Operational Data

Frequently changing data is saved periodically to reduce flash wear:

```c
// In main loop or timer task
static uint32_t lastSaveTime = 0;
const uint32_t SAVE_INTERVAL_MS = 300000;  // 5 minutes

void periodicSave() {
    if (millis() - lastSaveTime >= SAVE_INTERVAL_MS) {
        saveWaterMeterTotals();
        savePumpStatistics();
        lastSaveTime = millis();
    }
}
```

### Shutdown Save

Statistics are saved on graceful shutdown:

```c
void onShutdown() {
    Serial.println("Saving data before shutdown...");
    saveConfiguration();
    saveWaterMeterTotals();
    savePumpStatistics();
    saveBlowdownTotal();
}
```

---

## Factory Reset

Factory reset clears all NVS data and restores defaults:

```c
void factoryReset() {
    Serial.println("Performing factory reset...");

    preferences.begin(NVS_NAMESPACE, false);
    preferences.clear();  // Erase all keys in namespace
    preferences.end();

    // Reinitialize with defaults
    initializeDefaults();

    Serial.println("Factory reset complete - rebooting");
    ESP.restart();
}
```

---

## Version Migration

When firmware is updated, configuration version may change:

```c
void migrateConfiguration() {
    if (systemConfig.version < CONFIG_VERSION) {
        Serial.printf("Migrating config from v%d to v%d\n",
                     systemConfig.version, CONFIG_VERSION);

        // Add new fields with defaults
        switch (systemConfig.version) {
            case 1:
                // v1 -> v2: Add new feature X
                systemConfig.newFeatureX = DEFAULT_FEATURE_X;
                // Fall through for chained migrations

            case 2:
                // v2 -> v3: Add new feature Y
                systemConfig.newFeatureY = DEFAULT_FEATURE_Y;
                break;
        }

        systemConfig.version = CONFIG_VERSION;
        saveConfiguration();
    }
}
```

---

## Flash Wear Considerations

### Write Limits

ESP32 flash memory typically supports:
- **100,000** write cycles per sector
- NVS uses wear leveling across multiple sectors

### Best Practices

1. **Batch writes**: Combine multiple changes into single save operations
2. **Periodic saves**: Don't save on every small change
3. **Use appropriate types**: uint32 is more efficient than blobs for counters
4. **Avoid redundant writes**: Check if value changed before writing

```c
void saveValueIfChanged(const char* key, uint32_t newValue) {
    preferences.begin(NVS_NAMESPACE, false);

    uint32_t currentValue = preferences.getUInt(key, 0);
    if (currentValue != newValue) {
        preferences.putUInt(key, newValue);
    }

    preferences.end();
}
```

---

## Troubleshooting

### Configuration Won't Load

1. Check for correct magic number
2. Verify structure size matches
3. Check for checksum errors
4. Try factory reset

### Data Corruption

If data appears corrupted:

```c
void verifyNVSHealth() {
    preferences.begin(NVS_NAMESPACE, true);

    // Get stats
    size_t freeEntries = preferences.freeEntries();
    Serial.printf("NVS free entries: %d\n", freeEntries);

    if (freeEntries < 10) {
        Serial.println("WARNING: NVS nearly full!");
    }

    preferences.end();
}
```

### NVS Full

If NVS runs out of space:

1. Clear unnecessary keys
2. Reduce blob sizes
3. Consider using SPIFFS for large data

---

## Required Library

The Preferences library is built into the ESP32 Arduino core:

```cpp
#include <Preferences.h>
```

No additional libraries needed for basic NVS functionality.

---

## Summary

| What | Where | When Saved |
|------|-------|------------|
| All settings | `config` blob | On change |
| Water totals | Individual keys | Every 5 minutes |
| Pump runtime | Individual keys | Every 5 min + shutdown |
| Blowdown time | Individual key | Every 5 min + shutdown |
| Calibration | In config blob | After calibration |
| WiFi credentials | In config blob | On change |
| Access code | In config blob | On change |

This system ensures all critical data survives power cycles while minimizing flash wear.

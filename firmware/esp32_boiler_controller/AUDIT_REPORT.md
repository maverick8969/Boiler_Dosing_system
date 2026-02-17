# Columbia CT-6 Boiler Controller — Error Handling & Input Validation Audit

**Date:** 2026-02-17
**Firmware Version:** 1.0.0
**Platform:** ESP32-WROOM-32, Arduino + FreeRTOS, PlatformIO
**Auditor:** Automated Code Audit

---

## Executive Summary

The CT-6 firmware has a solid foundation: FreeRTOS task isolation, SPI mutex
protection, an alarm bitmask with rising-edge detection, and graceful RTD
fallback.  However, the codebase has **no hardware watchdog**, **no startup
self-test**, **no stale-reading detection**, **no I2C bus recovery**, and
**no way for the operator to disable optional hardware** in software.  A
single stuck I2C transaction can freeze the control task, and a disconnected
conductivity probe is indistinguishable from a 0 µS/cm reading after one
failed cycle.

This report identifies 22 gaps grouped into four categories, proposes
concrete fixes with code references, and delivers three implementation
artifacts:

1. `device_manager.h/.cpp` — Centralized hardware registry with enable/
   disable, fault tracking, and dependency safeguards.
2. `self_test.h/.cpp` — Power-on self-test (POST) for every bus and device.
3. `sensor_health.h/.cpp` — Runtime staleness detector and fault counter.

---

## 1. Error-Checking Gap Inventory

### 1.1 Startup Self-Checks (Missing)

| # | Gap | File : Line | Risk | Impact |
|---|-----|-------------|------|--------|
| S1 | **No I2C bus scan at startup** — LCD at 0x27 and ADS1115 at 0x48 are initialized without verifying the bus is alive. A shorted SDA line hangs `Wire.begin()`. | `main.cpp:102-103` | HIGH | Controller freezes on boot; no display, no valve feedback |
| S2 | **No hardware watchdog armed** — ESP32's TWDT (Task Watchdog Timer) is never configured. A hung task (e.g., SPI stall) runs forever. | `main.cpp:88-247` (entire `setup()`) | CRITICAL | Infinite hang with no recovery; valve could remain open |
| S3 | **SPI mutex creation not verified** — `xSemaphoreCreateMutex()` returns `NULL` on heap exhaustion but the result is never checked. | `main.cpp:107` | MEDIUM | NULL dereference in `xSemaphoreTake()` crashes system |
| S4 | **FreeRTOS task creation not verified** — `xTaskCreatePinnedToCore()` return values are ignored. If heap is low, tasks silently fail to start. | `main.cpp:202-240` | HIGH | Control or measurement task doesn't run; pumps or valve uncontrolled |
| S5 | **No ADS1115 register read-back** — `blowdown.begin()` only checks I2C ACK, not that the device responds with valid register data. A bus glitch can produce a false ACK. | `blowdown.cpp:57-58` | MEDIUM | False "feedback enabled" → stale 0 mA readings → VALVE_FAULT alarm spam |
| S6 | **NVS config CRC16 field defined but never computed or verified** — `config.h:397` declares `checksum` but `loadConfiguration()` only checks magic number. A bit-flip in NVS goes undetected. | `main.cpp:393-416`, `config.h:397` | MEDIUM | Corrupted setpoints, pump rates, or alarm thresholds loaded silently |

### 1.2 Runtime Fault Detection (Weak/Missing)

| # | Gap | File : Line | Risk | Impact |
|---|-----|-------------|------|--------|
| R1 | **No stale-reading detection** — If `taskMeasurementLoop` fails to acquire the SPI mutex (line 335), `systemState` retains previous values. The control task has no timestamp or freshness check; it acts on arbitrarily old data. | `main.cpp:330-344` | CRITICAL | Blowdown decisions based on hours-old conductivity; valve opens/closes incorrectly |
| R2 | **Conductivity 0.0 is not flagged as suspect** — A disconnected probe returns 0.0 from the EZO which passes `parseReadingResponse` (line 618: `ec >= 0`). The system treats 0.0 µS/cm as valid, triggers LOW CONDUCTIVITY alarm at best, but feed modes act on it. | `conductivity.cpp:618` | HIGH | Pumps dose chemicals based on false 0 reading; blowdown never activates |
| R3 | **ADS1115 read returns 0 on failure with no error flag** — `ads1115ReadChannel()` returns 0 when `Wire.available() != 2`. Zero voltage maps to 0 mA which is below FAULT_LOW (3.0 mA), triggering a fault — but only if feedback is enabled. If feedback is disabled, 0 is silently used. | `blowdown.cpp:624-630` | MEDIUM | Intermittent I2C failures produce false valve-fault alarms |
| R4 | **No I2C bus recovery** — If SDA is held low by a slave (common with I2C), `Wire` calls hang until internal timeout. There is no clock-stretching recovery or bus reset (`Wire.end()` + 9 clock pulses + `Wire.begin()`). | N/A (missing) | HIGH | I2C hangs propagate to control task (ADS1115 read) or display task (LCD) |
| R5 | **`millis()` overflow not handled in timers** — `updateFeedwaterPumpMonitor()` and multiple state machines use `now - start_time >= duration` which wraps correctly for unsigned arithmetic, but `_mode_c_cycle_start` is initialized to 0 (line 392), causing a ~49.7-day false trigger on first boot. | `chemical_pump.cpp:392` | LOW | One-time spurious pump activation after 49.7 days of uptime |
| R6 | **Pump `updateStats()` double-counts runtime** — `_status.total_runtime_ms += delta` is called every `update()` cycle (100ms), but `delta` is `now - start_time` (total since start), not the incremental 100ms. Total runtime inflates by ~N×. | `chemical_pump.cpp:516-532` | MEDIUM | Logged pump runtimes and volume dispensed are wildly inaccurate |
| R7 | **No consecutive-failure counter for EZO-EC** — A single failed read sets `_ezo_ok = false`, and a single good read clears it. Intermittent noise causes alarm flicker. No hysteresis or N-of-M voting. | `conductivity.cpp:237-250` | MEDIUM | Alarm flicker on noisy UART; operator alarm fatigue |
| R8 | **Conductivity alarm evaluates against sensor error** — `checkAlarms()` reads `conductivity_calibrated` even when `ALARM_SENSOR_ERROR` is active, generating misleading HIGH/LOW conductivity alarms from a stale or zero value. | `main.cpp:526-549` | HIGH | False HIGH/LOW CONDUCTIVITY alarms alongside SENSOR ERROR; confusing |
| R9 | **Water meter totalizer accumulates drift** — `update()` recalculates `volume_delta` from floating-point `pulsesToVolume()` every cycle. Rounding errors accumulate over days, and the delta can go negative if float precision is lost. | `water_meter.cpp:84-94` | LOW | Totalizer slowly drifts from true value over weeks |

### 1.3 Graceful Degradation (Insufficient)

| # | Gap | File : Line | Risk | Impact |
|---|-----|-------------|------|--------|
| G1 | **No device enable/disable registry** — All hardware is unconditionally polled. A user who doesn't install the second water meter, or the ADS1115, has no way to suppress polling or related alarms. | N/A (missing) | MEDIUM | Spurious alarms, wasted bus cycles, user confusion |
| G2 | **Pump feed modes D/E don't check if assigned water meter is enabled** — If `assigned_meter` points to a disabled meter, `water_contacts` is always 0. The pump never runs, but no warning is shown. | `chemical_pump.cpp:416-469` | MEDIUM | Silent dosing failure; chemical under-dosing |
| G3 | **No safe-mode concept** — If conductivity sensor is down, the system continues blowdown logic with stale data. There is no "fail-safe" posture (e.g., close valve, stop pumps, flash alarm). | N/A (missing) | CRITICAL | Uncontrolled blowdown or chemical dosing during sensor failure |
| G4 | **SD card removal at runtime not detected** — `_available` is set once at init. If the card is removed, `SD.open()` fails silently; `writeCSVRow()` returns false but `logReading()` does not retry or re-detect. | `sd_logger.cpp:89-91, 311-314` | LOW | Silent data loss; operator unaware logging stopped |

### 1.4 Logging / Telemetry Gaps

| # | Gap | File : Line | Risk | Impact |
|---|-----|-------------|------|--------|
| L1 | **Alarm cleared events not logged individually** — Only rising alarms are logged with `dataLogger.logAlarm()`. Falling edges just call `display.clearAlarm()` with no event log. | `main.cpp:600-605` | MEDIUM | No audit trail of alarm durations; can't reconstruct incident timeline |
| L2 | **Events and alarms not buffered for offline** — `logEvent()` and `logAlarm()` in `data_logger.cpp` only upload if WiFi is connected; no circular buffer like `logReading()` uses. Events are lost during WiFi outages. | `data_logger.cpp:139-172` | MEDIUM | Lost alarm history during network failures |
| L3 | **No boot-reason logging** — ESP32 provides `esp_reset_reason()` (watchdog, brownout, panic, software). This is never logged, making it impossible to diagnose field resets. | N/A (missing) | MEDIUM | Can't distinguish power cycle from watchdog reset from crash |

---

## 2. Recommended Fixes

### Fix S1 + S5: Startup I2C/SPI Device Scan

```cpp
// self_test.h — Power-On Self-Test
bool selfTest_I2C() {
    bool lcd_ok = false, ads_ok = false;

    Wire.beginTransmission(LCD_I2C_ADDR);
    lcd_ok = (Wire.endTransmission() == 0);

    Wire.beginTransmission(ADS1115_I2C_ADDR);
    ads_ok = (Wire.endTransmission() == 0);

    // Read-back ADS1115 config register to verify real device
    if (ads_ok) {
        Wire.beginTransmission(ADS1115_I2C_ADDR);
        Wire.write(0x01); // Config register
        Wire.endTransmission();
        Wire.requestFrom((uint8_t)ADS1115_I2C_ADDR, (uint8_t)2);
        if (Wire.available() == 2) {
            uint16_t cfg = (Wire.read() << 8) | Wire.read();
            ads_ok = (cfg != 0x0000 && cfg != 0xFFFF);
        } else {
            ads_ok = false;
        }
    }
    return lcd_ok && ads_ok;
}
```

### Fix S2: Hardware Watchdog

```cpp
#include <esp_task_wdt.h>

void setup() {
    // 30-second watchdog, panic on timeout
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL); // Subscribe main task
}

void taskControlLoop(void* param) {
    esp_task_wdt_add(NULL); // Subscribe this task
    while (true) {
        esp_task_wdt_reset();
        // ... existing control logic ...
    }
}
```

### Fix S6: NVS CRC16 Verification

```cpp
uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

void saveConfiguration() {
    systemConfig.magic = CONFIG_MAGIC;
    systemConfig.version = CONFIG_VERSION;
    systemConfig.checksum = 0; // Zero before computing
    systemConfig.checksum = crc16_ccitt(
        (uint8_t*)&systemConfig, sizeof(system_config_t));
    // ... NVS write ...
}

bool loadConfiguration() {
    // ... NVS read ...
    uint16_t stored = systemConfig.checksum;
    systemConfig.checksum = 0;
    uint16_t computed = crc16_ccitt(
        (uint8_t*)&systemConfig, sizeof(system_config_t));
    if (stored != computed) {
        Serial.println("CONFIG CRC MISMATCH - loading defaults");
        initializeDefaults();
        return false;
    }
    return true;
}
```

### Fix R1: Stale-Reading Detection

```cpp
// In system_state_t_runtime:
uint32_t last_measurement_time;  // Updated by measurement task
bool     measurement_stale;       // Set when age > threshold

// In taskMeasurementLoop after successful read:
systemState.last_measurement_time = millis();

// In taskControlLoop before using conductivity:
uint32_t reading_age = millis() - systemState.last_measurement_time;
systemState.measurement_stale = (reading_age > STALE_READING_MS); // e.g. 5000

if (systemState.measurement_stale) {
    // Enter safe mode: close valve, stop pumps
    blowdownController.closeValve();
    pumpManager.stopAll();
    new_alarms |= ALARM_SENSOR_ERROR;
}
```

### Fix R2: Zero-Reading Rejection

```cpp
// conductivity.cpp parseReadingResponse, after line 618:
// Reject readings below noise floor as probe-disconnected
if (enabled[0] && ec < COND_MINIMUM_VALID) { // e.g., 0.1 µS/cm
    return false;
}
```

### Fix R6: Pump Stats Double-Counting

```cpp
void ChemicalPump::updateStats() {
    if (!_status.running) return;

    uint32_t now = millis();
    uint32_t delta_ms = now - _last_stats_time; // Track INCREMENTAL time
    _last_stats_time = now;

    _status.runtime_ms = now - _status.start_time;  // Current run duration
    _status.total_runtime_ms += delta_ms;            // Cumulative

    long steps = abs(_stepper.currentPosition());
    _status.total_steps += steps;
    _stepper.setCurrentPosition(0);

    if (_config && _config->steps_per_ml > 0) {
        _status.volume_dispensed_ml =
            (float)_status.total_steps / _config->steps_per_ml;
    }
}
```

### Fix R8: Suppress Conductivity Alarms During Sensor Failure

```cpp
void checkAlarms() {
    uint16_t new_alarms = ALARM_NONE;
    float cond = systemState.conductivity_calibrated;

    bool sensor_ok = conductivitySensor.isSensorOK() &&
                     !systemState.measurement_stale;

    // Only evaluate conductivity alarms if sensor is healthy
    if (sensor_ok) {
        // ... existing HIGH/LOW conductivity alarm logic ...
    }

    if (!conductivitySensor.isSensorOK()) {
        new_alarms |= ALARM_SENSOR_ERROR;
    }
    // ... rest of alarm logic ...
}
```

### Fix G3: Safe-Mode Definition

```cpp
typedef enum {
    SAFE_MODE_NONE = 0,
    SAFE_MODE_SENSOR_FAIL,   // Conductivity probe down
    SAFE_MODE_BUS_FAIL,      // I2C or SPI hung
    SAFE_MODE_WATCHDOG        // Post-watchdog restart
} safe_mode_t;

void enterSafeMode(safe_mode_t reason) {
    blowdownController.closeValve();
    pumpManager.stopAll();
    systemState.safe_mode = reason;
    dataLogger.logAlarm(ALARM_SAFE_MODE, "SAFE MODE", true, reason);
    display.showAlarm("SAFE MODE");
    // Set power LED to RED
}
```

### Fix L1: Log Alarm-Clear Events

```cpp
// main.cpp, after line 601:
if (falling_alarms & ALARM_COND_HIGH) {
    dataLogger.logAlarm(ALARM_COND_HIGH, "HIGH CONDUCTIVITY", false, cond);
}
if (falling_alarms & ALARM_COND_LOW) {
    dataLogger.logAlarm(ALARM_COND_LOW, "LOW CONDUCTIVITY", false, cond);
}
// ... repeat for all alarm types ...
```

### Fix L3: Boot-Reason Logging

```cpp
#include <esp_system.h>

void logBootReason() {
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reason_str;
    switch (reason) {
        case ESP_RST_POWERON:  reason_str = "POWER_ON"; break;
        case ESP_RST_SW:       reason_str = "SOFTWARE"; break;
        case ESP_RST_PANIC:    reason_str = "PANIC"; break;
        case ESP_RST_INT_WDT:  reason_str = "INT_WATCHDOG"; break;
        case ESP_RST_TASK_WDT: reason_str = "TASK_WATCHDOG"; break;
        case ESP_RST_WDT:      reason_str = "OTHER_WATCHDOG"; break;
        case ESP_RST_BROWNOUT: reason_str = "BROWNOUT"; break;
        default:               reason_str = "UNKNOWN"; break;
    }
    Serial.printf("Boot reason: %s\n", reason_str);
    sdLogger.logEvent("BOOT", reason_str, (int32_t)reason);
}
```

---

## 3. Proposed Device Capability / Config Model

### 3.1 DeviceManager Registry

A central `DeviceManager` tracks every hardware peripheral with:
- **enabled** — User-configurable via menu / NVS
- **installed** — Detected at startup (POST)
- **healthy** — Runtime fault status
- **required** — Cannot be disabled (e.g., blowdown relay)
- **dependencies** — "Pump Mode D requires Water Meter 1"

```
DeviceManager
  ├── DEV_CONDUCTIVITY_PROBE   [required, I2C/UART]
  ├── DEV_TEMP_RTD             [optional, SPI]
  ├── DEV_BLOWDOWN_VALVE       [required, GPIO]
  ├── DEV_VALVE_FEEDBACK       [optional, I2C(ADS1115)]
  ├── DEV_PUMP_H2SO3           [optional, GPIO]
  ├── DEV_PUMP_NAOH            [optional, GPIO]
  ├── DEV_PUMP_AMINE           [optional, GPIO]
  ├── DEV_WATER_METER_1        [optional, GPIO interrupt]
  ├── DEV_WATER_METER_2        [optional, GPIO interrupt]
  ├── DEV_LCD_DISPLAY          [optional, I2C]
  ├── DEV_SD_CARD              [optional, SPI]
  ├── DEV_WIFI                 [optional, internal]
  ├── DEV_AUX_INPUT_1          [optional, GPIO]
  └── DEV_FEEDWATER_MONITOR    [optional, GPIO]
```

### 3.2 DeviceInfo Structure (stored in NVS)

```cpp
typedef struct {
    uint16_t device_flags;     // Bitmask: bit N = device N enabled
    uint16_t installed_flags;  // Bitmask: bit N = device N detected at POST
    uint16_t fault_flags;      // Bitmask: bit N = device N currently faulted
} device_registry_t;
```

### 3.3 Dependency Matrix

| Device | Depends On | Safeguard |
|--------|-----------|-----------|
| Pump Mode A | Blowdown Valve | Block mode A if valve disabled |
| Pump Mode D | Water Meter (assigned) | Block mode D if meter disabled |
| Pump Mode E | Water Meter (paddlewheel) | Block mode E if meter disabled |
| Pump Mode F | Water Meter + Fuzzy Logic | Block if either disabled |
| Blowdown Feedback | ADS1115 | Fall back to time-based if disabled |
| Temp Compensation | MAX31865 RTD | Fall back to manual temp |
| SD Logging | SD Card | Continue with WiFi buffer only |
| Conductivity Alarms | Conductivity Probe | Suppress alarms if probe faulted |

---

## 4. Proposed Menu Flow

### 4.1 Screen Hierarchy

```
MAIN STATUS (rotary encoder cycles screens)
  │
  ├── Conductivity Screen
  ├── Temperature Screen
  ├── Blowdown Screen
  ├── Pump 1/2/3 Screens
  ├── Water Meter 1/2 Screens
  ├── Alarms Screen
  ├── Network Screen
  │
  └── [LONG PRESS] ─► MENU ROOT
       │
       ├── 1. Hardware Config ─────────────────────────────┐
       │    ├── Conductivity Probe  [Enabled ✓] [OK]       │
       │    ├── Temperature RTD     [Enabled ✓] [OK]       │
       │    ├── Blowdown Valve      [REQUIRED]  [OK]       │
       │    ├── Valve Feedback ADC  [Enabled ✓] [OK]       │
       │    ├── Pump 1 (H2SO3)     [Enabled ✓] [OK]       │
       │    ├── Pump 2 (NaOH)      [Disabled]  [--]       │
       │    ├── Pump 3 (Amine)     [Disabled]  [--]       │
       │    ├── Water Meter 1      [Enabled ✓] [OK]       │
       │    ├── Water Meter 2      [Disabled]  [--]       │
       │    ├── LCD Display        [Enabled ✓] [OK]       │
       │    ├── SD Card Logger     [Enabled ✓] [NO CARD]  │
       │    ├── WiFi / Network     [Enabled ✓] [CONNECTED]│
       │    ├── Drum Level Switch  [Disabled]  [--]       │
       │    └── Feedwater Monitor  [Enabled ✓] [OK]       │
       │                                                    │
       ├── 2. Conductivity Setup                            │
       │    ├── Setpoint                                    │
       │    ├── Cell Constant                               │
       │    ├── Calibration                                 │
       │    └── Anti-Flash                                  │
       │                                                    │
       ├── 3. Blowdown Setup                                │
       │    ├── Setpoint / Deadband                         │
       │    ├── Time Limit                                  │
       │    └── Valve Delay                                 │
       │                                                    │
       ├── 4. Pump 1/2/3 Setup                              │
       │    ├── Feed Mode (A-F/Disabled)                    │
       │    ├── Mode Parameters                             │
       │    ├── Calibration                                 │
       │    └── Time Limits                                 │
       │                                                    │
       ├── 5. Alarm Setup                                   │
       │    ├── High/Low Thresholds                         │
       │    ├── Enable/Disable per alarm                    │
       │    └── Alarm History                               │
       │                                                    │
       ├── 6. Network Setup                                 │
       │    ├── WiFi SSID/Password                          │
       │    ├── Server Host/Port                            │
       │    └── Log Interval                                │
       │                                                    │
       ├── 7. System                                        │
       │    ├── Self-Test (run POST)                        │
       │    ├── Factory Reset                               │
       │    ├── Firmware Version                            │
       │    └── Boot History                                │
       │                                                    │
       └── [LONG PRESS] ─► EXIT MENU                       │
```

### 4.2 Hardware Config Screen Behavior

Each device row shows:
```
Line 1: "== HW Config 1/14 =="
Line 2: "Conductivity Probe"
Line 3: "State: Enabled"         ← Rotary selects Enabled/Disabled
Line 4: "Status: OK (2501 uS)"  ← Live status or "NOT FOUND"
```

- Rotating encoder scrolls through devices.
- Short press toggles enable/disable (unless `required`).
- If disabling a device has dependents, show warning:
  ```
  "Disable WM1?"
  "Pump1 Mode D needs WM1"
  ```
- Confirmed disable: saves to NVS, stops polling that device.

### 4.3 NVS Schema Addition

```cpp
// Added to system_config_t:
typedef struct {
    uint16_t enabled_devices;   // Bitmask per DEVICE_ID
    // Future: per-device fault thresholds, retry counts
} hardware_config_t;

// In system_config_t:
hardware_config_t hardware;
```

---

## 5. Test Cases

### 5.1 Probe Missing at Boot

| Step | Action | Expected |
|------|--------|----------|
| 1 | Disconnect EZO-EC UART cable | - |
| 2 | Power on controller | POST reports "EZO-EC: FAIL" on Serial |
| 3 | Observe LCD | "SENSOR ERROR" alarm displayed |
| 4 | Observe LED strip | LED_POWER = RED, LED_ALARM = RED flash |
| 5 | Check blowdown valve | Valve CLOSED (safe state) |
| 6 | Check pumps | All pumps STOPPED |
| 7 | Reconnect cable, press RESET | POST passes; normal operation resumes |

### 5.2 Probe Intermittent at Runtime

| Step | Action | Expected |
|------|--------|----------|
| 1 | System running normally | Conductivity reading valid |
| 2 | Introduce noise on UART (loose connector) | Occasional EZO read failures |
| 3 | After 3 consecutive failures | `_ezo_ok` transitions to `false` |
| 4 | ALARM_SENSOR_ERROR raised | Logged with timestamp |
| 5 | Conductivity alarms suppressed | No false HIGH/LOW alarms |
| 6 | Noise clears, 3 consecutive good reads | `_ezo_ok` transitions back to `true` |
| 7 | ALARM_SENSOR_ERROR cleared | Cleared event logged with duration |

### 5.3 Sensor Disabled via Menu

| Step | Action | Expected |
|------|--------|----------|
| 1 | Enter Menu → Hardware Config → Water Meter 2 | Show "Enabled: Yes, Status: OK" |
| 2 | Toggle to "Disabled", press confirm | NVS saved with WM2 bit cleared |
| 3 | WM2 screen shows "DISABLED" | No flow rate displayed |
| 4 | Pump 1 in Mode D assigned to WM2 | Warning: "WM2 disabled — Mode D inactive" |
| 5 | Re-enable WM2 | Polling resumes, Mode D re-activates |

### 5.4 Motor Disabled via Menu

| Step | Action | Expected |
|------|--------|----------|
| 1 | Enter Menu → Hardware Config → Pump 2 (NaOH) | Show "Enabled: Yes" |
| 2 | Toggle to "Disabled", confirm | Pump immediately stops if running |
| 3 | Stepper driver disabled (GPIO13 HIGH for that axis) | No step pulses on GPIO27 |
| 4 | Pump 2 screen shows "DISABLED" | HOA locked to OFF |
| 5 | Fuzzy logic output for caustic | Still computed but not applied |
| 6 | Web UI shows Pump 2 grayed out | Cannot submit HOA=HAND for Pump 2 |

### 5.5 Re-Enable at Runtime

| Step | Action | Expected |
|------|--------|----------|
| 1 | Pump 3 disabled, system running | Pump 3 idle |
| 2 | Enter Menu → Hardware Config → Pump 3 | Show "Disabled" |
| 3 | Toggle "Enabled", confirm | NVS updated, driver enabled |
| 4 | Pump 3 resumes configured feed mode | HOA returns to AUTO |
| 5 | Verify no lost feed time | Accumulated feed counters preserved |

### 5.6 Stale Reading Detection

| Step | Action | Expected |
|------|--------|----------|
| 1 | System running, SPI bus healthy | Readings update every 500ms |
| 2 | Force SPI mutex held (simulate SD card hang) | Measurement task blocked |
| 3 | After STALE_READING_MS (5s) | `measurement_stale = true` |
| 4 | Control task detects staleness | Safe mode entered |
| 5 | Valve closes, pumps stop | ALARM_STALE_DATA raised |
| 6 | SPI releases, readings resume | Safe mode exits, normal operation |

### 5.7 Watchdog Recovery

| Step | Action | Expected |
|------|--------|----------|
| 1 | Simulate infinite loop in control task | Task watchdog fires at 30s |
| 2 | ESP32 reboots | `esp_reset_reason() == ESP_RST_TASK_WDT` |
| 3 | Boot reason logged to SD + Serial | "BOOT: TASK_WATCHDOG" |
| 4 | System re-initializes | Valve starts CLOSED, POST runs |

---

## 6. Priority Ranking

| Priority | Gap IDs | Rationale |
|----------|---------|-----------|
| **P0 — Immediate** | S2, R1, G3, R8 | No watchdog = unrecoverable hang. Stale data + no safe mode = uncontrolled blowdown |
| **P1 — Next Sprint** | S1, S4, R2, R6, R7, L1 | Startup verification, zero-reading rejection, stats accuracy, alarm logging |
| **P2 — Planned** | G1, G2, S5, S6, R4, L2, L3 | Device registry, dependency checks, CRC, bus recovery, event buffering |
| **P3 — Enhancement** | S3, R3, R5, R9, G4 | Edge cases with lower field impact |

---

## 7. Implementation Files Delivered

| File | Purpose |
|------|---------|
| `include/device_manager.h` | Device registry with enable/disable, fault tracking, dependency enforcement |
| `src/device_manager.cpp` | Implementation of DeviceManager |
| `include/self_test.h` | Power-on self-test (POST) declarations |
| `src/self_test.cpp` | POST implementation: I2C scan, SPI verify, UART probe, GPIO check |
| `include/sensor_health.h` | Runtime staleness detection, consecutive-failure tracking |
| `src/sensor_health.cpp` | SensorHealth implementation with safe-mode trigger |
| `test_programs/test_fault_scenarios.cpp` | Automated test harness for fault injection |

---

*End of Audit Report*

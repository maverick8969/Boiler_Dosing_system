/**
 * @file test_fault_scenarios.cpp
 * @brief Fault Injection Test Harness for CT-6 Boiler Controller
 *
 * Standalone test program that exercises the DeviceManager, SelfTest,
 * and SensorHealthMonitor modules with simulated fault scenarios.
 *
 * Upload to ESP32 independently (not part of the main firmware build).
 * Results are printed to Serial at 115200 baud.
 *
 * Test Scenarios:
 *   T1: Probe missing at boot (EZO-EC not responding)
 *   T2: Probe intermittent at runtime (simulated UART failures)
 *   T3: Sensor disabled via DeviceManager
 *   T4: Motor disabled via DeviceManager
 *   T5: Re-enable device at runtime
 *   T6: Stale reading detection
 *   T7: I2C bus recovery
 *   T8: Dependency checking (pump mode vs. water meter)
 *   T9: Safe mode entry and exit
 *   T10: Zero-reading rejection for conductivity
 *   T11: Device status reporting
 *   T12: Temperature sensor fault/recovery hysteresis
 *   T13: Valve feedback sensor fault/recovery hysteresis
 *   T14: Multiple concurrent sensor faults trigger safe mode
 *   T15: DeviceManager boundary checks (invalid device IDs)
 *   T16: Safe mode STALE_DATA entry path
 *   T17: Unknown feed mode dependency (silent pass behavior)
 *   T18: Fault counter near overflow limits
 *   T19: Temperature sentinel value detection
 *   T20: Conductivity range boundary values
 *   T21: Safe mode hold time enforcement
 *   T22: Device fault then clear then re-fault cycle
 */

#include <Arduino.h>
#include "device_manager.h"
#include "sensor_health.h"
#include "self_test.h"
#include "config.h"

// ============================================================================
// TEST FRAMEWORK (minimal, no external dependencies)
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
        Serial.printf("  PASS: %s\n", msg); \
    } else { \
        tests_failed++; \
        Serial.printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
    } \
} while(0)

#define TEST_BEGIN(name) do { \
    Serial.println(); \
    Serial.printf("--- %s ---\n", name); \
} while(0)

// ============================================================================
// TEST FIXTURES
// ============================================================================

static uint16_t test_enabled_devices;

void resetTestState() {
    // Reset DeviceManager
    test_enabled_devices = HW_CONFIG_DEFAULT_ENABLED;
    deviceManager.begin(&test_enabled_devices);

    // Reset SensorHealth
    sensorHealth.begin();
}

// ============================================================================
// T1: PROBE MISSING AT BOOT
// ============================================================================

void test_probe_missing_at_boot() {
    TEST_BEGIN("T1: Probe Missing at Boot");
    resetTestState();

    // Simulate POST: EZO-EC not found
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, false);

    TEST_ASSERT(!deviceManager.isOperational(DEV_CONDUCTIVITY_PROBE),
                "Conductivity probe not operational when not installed");

    TEST_ASSERT(deviceManager.getDeviceState(DEV_CONDUCTIVITY_PROBE) == DEVSTATE_NOT_FOUND,
                "State is NOT_FOUND");

    // Required device failed — should be flagged
    TEST_ASSERT(deviceManager.isEnabled(DEV_CONDUCTIVITY_PROBE),
                "Required device stays enabled even when not found");

    // Cannot disable a required device
    bool result = deviceManager.setEnabled(DEV_CONDUCTIVITY_PROBE, false);
    TEST_ASSERT(!result, "Cannot disable required device (conductivity probe)");

    // Blowdown valve is also required
    result = deviceManager.setEnabled(DEV_BLOWDOWN_VALVE, false);
    TEST_ASSERT(!result, "Cannot disable required device (blowdown valve)");
}

// ============================================================================
// T2: PROBE INTERMITTENT AT RUNTIME
// ============================================================================

void test_probe_intermittent() {
    TEST_BEGIN("T2: Probe Intermittent at Runtime");
    resetTestState();

    // Simulate initial good state
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2501.0);
    sensorHealth.reportConductivityOK(2499.0);
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isConductivityValid(),
                "Conductivity valid after 3 good reads");

    // Simulate 2 failures (below threshold of 3)
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isConductivityValid(),
                "Still valid after 2 failures (threshold is 3)");

    // Third failure trips the fault
    sensorHealth.reportConductivityFail();
    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isConductivityValid(),
                "Faulted after 3 consecutive failures");

    TEST_ASSERT(sensorHealth.getConductivityHealth()->faulted,
                "Fault flag is set");

    // Recovery: 2 good reads (not enough)
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isConductivityValid(),
                "Still faulted after 2 good reads (need 3)");

    // Third good read clears fault
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isConductivityValid(),
                "Recovered after 3 consecutive good reads");

    TEST_ASSERT(!sensorHealth.getConductivityHealth()->faulted,
                "Fault flag cleared");
}

// ============================================================================
// T3: SENSOR DISABLED VIA DEVICE MANAGER
// ============================================================================

void test_sensor_disabled() {
    TEST_BEGIN("T3: Sensor Disabled via Menu");
    resetTestState();

    // Water Meter 2 starts enabled and installed
    deviceManager.setInstalled(DEV_WATER_METER_2, true);

    TEST_ASSERT(deviceManager.isOperational(DEV_WATER_METER_2),
                "WM2 operational when enabled and installed");

    // Disable it
    bool result = deviceManager.setEnabled(DEV_WATER_METER_2, false);
    TEST_ASSERT(result, "Can disable optional device (WM2)");

    TEST_ASSERT(!deviceManager.isEnabled(DEV_WATER_METER_2),
                "WM2 no longer enabled");

    TEST_ASSERT(!deviceManager.isOperational(DEV_WATER_METER_2),
                "WM2 no longer operational");

    TEST_ASSERT(deviceManager.getDeviceState(DEV_WATER_METER_2) == DEVSTATE_DISABLED,
                "State is DISABLED");

    // Verify bitmask updated
    TEST_ASSERT(!(test_enabled_devices & (1 << DEV_WATER_METER_2)),
                "NVS config bitmask cleared for WM2");
}

// ============================================================================
// T4: MOTOR DISABLED VIA DEVICE MANAGER
// ============================================================================

void test_motor_disabled() {
    TEST_BEGIN("T4: Motor Disabled via Menu");
    resetTestState();

    // All pumps start enabled and installed
    deviceManager.setInstalled(DEV_PUMP_NAOH, true);

    TEST_ASSERT(deviceManager.isOperational(DEV_PUMP_NAOH),
                "Pump NaOH operational initially");

    // Disable it
    deviceManager.setEnabled(DEV_PUMP_NAOH, false);

    TEST_ASSERT(!deviceManager.isOperational(DEV_PUMP_NAOH),
                "Pump NaOH not operational when disabled");

    // Dependency check: a feed mode requiring this pump should fail
    const char* dep_name = nullptr;
    bool dep_ok = deviceManager.checkPumpModeDependency(
        1,  // pump_id = PUMP_NAOH
        3,  // FEED_MODE_C_PERCENT_TIME (needs the pump itself)
        &dep_name
    );

    TEST_ASSERT(!dep_ok, "Feed mode dependency fails for disabled pump");
    TEST_ASSERT(dep_name != nullptr, "Dependency name is set");
}

// ============================================================================
// T5: RE-ENABLE DEVICE AT RUNTIME
// ============================================================================

void test_reenable_runtime() {
    TEST_BEGIN("T5: Re-enable Device at Runtime");
    resetTestState();

    deviceManager.setInstalled(DEV_PUMP_AMINE, true);

    // Disable
    deviceManager.setEnabled(DEV_PUMP_AMINE, false);
    TEST_ASSERT(!deviceManager.isOperational(DEV_PUMP_AMINE),
                "Pump Amine disabled");

    // Re-enable
    deviceManager.setEnabled(DEV_PUMP_AMINE, true);
    TEST_ASSERT(deviceManager.isOperational(DEV_PUMP_AMINE),
                "Pump Amine re-enabled and operational");

    TEST_ASSERT(deviceManager.getDeviceState(DEV_PUMP_AMINE) == DEVSTATE_OK,
                "State is OK after re-enable");

    // Verify NVS bitmask
    TEST_ASSERT(test_enabled_devices & (1 << DEV_PUMP_AMINE),
                "NVS config bitmask set for Pump Amine");
}

// ============================================================================
// T6: STALE READING DETECTION
// ============================================================================

void test_stale_reading() {
    TEST_BEGIN("T6: Stale Reading Detection");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);

    // Fresh measurement
    sensorHealth.reportMeasurementCycle();
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isMeasurementFresh(),
                "Measurement fresh immediately after report");

    TEST_ASSERT(sensorHealth.getMeasurementAge() < 100,
                "Measurement age < 100ms");

    // Simulate time passing beyond stale threshold
    // NOTE: In a real test we'd use a mock clock. Here we use delay().
    Serial.println("  (Waiting for stale threshold...)");
    delay(HEALTH_STALE_READING_MS + 500);

    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isMeasurementFresh(),
                "Measurement stale after threshold exceeded");

    TEST_ASSERT(sensorHealth.getMeasurementAge() > HEALTH_STALE_READING_MS,
                "Measurement age exceeds stale threshold");

    // This should trigger safe mode evaluation
    // (Only if conductivity probe is enabled and the system is past init)
}

// ============================================================================
// T7: I2C BUS RECOVERY
// ============================================================================

void test_i2c_recovery() {
    TEST_BEGIN("T7: I2C Bus Recovery");
    // Note: This test actually exercises Wire hardware.
    // On a bench with I2C devices, it verifies recovery works.
    // Without devices, it just checks the function doesn't crash.

    bool recovered = sensorHealth.attemptI2CRecovery();

    // We can't assert much without actual I2C devices, but we verify
    // the function doesn't crash or hang
    TEST_ASSERT(true, "I2C recovery function completed without crash");

    Serial.printf("  I2C recovery result: %s\n",
                  recovered ? "devices found" : "no devices (expected if no I2C hw)");
}

// ============================================================================
// T8: DEPENDENCY CHECKING
// ============================================================================

void test_dependency_checking() {
    TEST_BEGIN("T8: Dependency Checking");
    resetTestState();

    // Install all devices
    for (int i = 0; i < DEV_COUNT; i++) {
        deviceManager.setInstalled((device_id_t)i, true);
    }

    const char* dep = nullptr;

    // Mode D (water contact) with WM1 enabled — should pass
    bool ok = deviceManager.checkPumpModeDependency(0, 4, &dep);
    TEST_ASSERT(ok, "Mode D passes when WM1 operational");

    // Disable both water meters
    deviceManager.setEnabled(DEV_WATER_METER_1, false);
    deviceManager.setEnabled(DEV_WATER_METER_2, false);

    // Mode D should now fail
    ok = deviceManager.checkPumpModeDependency(0, 4, &dep);
    TEST_ASSERT(!ok, "Mode D fails when both water meters disabled");
    TEST_ASSERT(dep != nullptr, "Dependency name populated");
    Serial.printf("  Missing dependency: %s\n", dep ? dep : "(null)");

    // Mode F (fuzzy) needs conductivity + water meter
    ok = deviceManager.checkPumpModeDependency(0, 7, &dep);
    TEST_ASSERT(!ok, "Mode F fails when water meters disabled");

    // Re-enable WM1
    deviceManager.setEnabled(DEV_WATER_METER_1, true);
    ok = deviceManager.checkPumpModeDependency(0, 7, &dep);
    TEST_ASSERT(ok, "Mode F passes when WM1 re-enabled");

    // Mode A needs blowdown valve (required, always operational)
    ok = deviceManager.checkPumpModeDependency(0, 1, &dep);
    TEST_ASSERT(ok, "Mode A passes (blowdown valve is required)");

    // Blowdown feedback check
    TEST_ASSERT(deviceManager.isBlowdownFeedbackAvailable(),
                "Blowdown feedback available when ADS1115 installed");

    deviceManager.setEnabled(DEV_VALVE_FEEDBACK, false);
    TEST_ASSERT(!deviceManager.isBlowdownFeedbackAvailable(),
                "Blowdown feedback unavailable when disabled");
}

// ============================================================================
// T9: SAFE MODE ENTRY AND EXIT
// ============================================================================

void test_safe_mode() {
    TEST_BEGIN("T9: Safe Mode Entry and Exit");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);

    TEST_ASSERT(!sensorHealth.isInSafeMode(),
                "Not in safe mode initially");

    // Simulate sensor failure (3 consecutive failures)
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isInSafeMode(),
                "Safe mode entered after sensor fault");

    TEST_ASSERT(sensorHealth.getSafeMode() == SAFE_MODE_SENSOR_FAIL,
                "Safe mode reason is SENSOR_FAIL");

    Serial.printf("  Safe mode: %s\n", sensorHealth.getSafeModeString());

    // Try immediate exit — should fail (hold time not elapsed)
    sensorHealth.exitSafeMode();
    TEST_ASSERT(sensorHealth.isInSafeMode(),
                "Cannot exit safe mode immediately (hold time)");

    // Simulate recovery: 3 good reads
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportMeasurementCycle();

    // Wait for hold time
    Serial.println("  (Waiting for safe mode hold time...)");
    delay(HEALTH_SAFE_MODE_HOLD_MS + 500);

    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isInSafeMode(),
                "Safe mode auto-exited after sensor recovery + hold time");
}

// ============================================================================
// T10: ZERO-READING REJECTION
// ============================================================================

void test_zero_reading_rejection() {
    TEST_BEGIN("T10: Zero-Reading Rejection");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);

    // Good reading first
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isConductivityValid(),
                "Valid after normal readings");

    // Zero reading — should be treated as suspect/fault
    sensorHealth.reportConductivityOK(0.0);  // Below HEALTH_COND_MINIMUM_VALID
    sensorHealth.update();

    const sensor_health_t* h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(h->value_suspect || h->consecutive_failures > 0,
                "Zero reading flagged as suspect or failure");

    // Reading above maximum
    sensorHealth.reportConductivityOK(20000.0);  // Above HEALTH_COND_MAXIMUM_VALID

    h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(h->value_suspect || h->consecutive_failures > 0,
                "Over-range reading flagged as suspect or failure");

    // Normal reading recovers
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isConductivityValid(),
                "Recovery after normal readings resume");
}

// ============================================================================
// T11: DEVICE MANAGER STATUS REPORTING
// ============================================================================

void test_status_reporting() {
    TEST_BEGIN("T11: Device Manager Status Reporting");
    resetTestState();

    // Install some devices, leave others not installed
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    deviceManager.setInstalled(DEV_TEMP_RTD, true);
    deviceManager.setInstalled(DEV_BLOWDOWN_VALVE, true);
    deviceManager.setInstalled(DEV_VALVE_FEEDBACK, false);
    deviceManager.setInstalled(DEV_PUMP_H2SO3, true);
    deviceManager.setInstalled(DEV_LCD_DISPLAY, true);
    deviceManager.setEnabled(DEV_WATER_METER_2, false);

    uint8_t operational = deviceManager.countOperational();
    uint8_t faulted = deviceManager.countFaulted();

    TEST_ASSERT(operational > 0, "At least some devices operational");
    TEST_ASSERT(faulted == 0, "No faulted devices initially");

    // Inject a fault
    for (int i = 0; i < FAULT_CONSECUTIVE_THRESHOLD; i++) {
        deviceManager.reportFault(DEV_TEMP_RTD);
    }

    faulted = deviceManager.countFaulted();
    TEST_ASSERT(faulted == 1, "One device faulted after fault injection");

    TEST_ASSERT(deviceManager.isFaulted(DEV_TEMP_RTD),
                "RTD is faulted");

    TEST_ASSERT(strcmp(deviceManager.getStateString(DEV_TEMP_RTD), "FAULTED") == 0,
                "State string is FAULTED");

    TEST_ASSERT(strcmp(deviceManager.getStateString(DEV_WATER_METER_2), "DISABLED") == 0,
                "State string for disabled device is DISABLED");

    TEST_ASSERT(strcmp(deviceManager.getStateString(DEV_VALVE_FEEDBACK), "NOT FOUND") == 0,
                "State string for not-installed device is NOT FOUND");

    // Print full status report
    deviceManager.printStatus();
    sensorHealth.printStatus();
}

// ============================================================================
// T12: TEMPERATURE SENSOR FAULT AND RECOVERY
// ============================================================================

void test_temperature_fault_recovery() {
    TEST_BEGIN("T12: Temperature Sensor Fault/Recovery");
    resetTestState();

    deviceManager.setInstalled(DEV_TEMP_RTD, true);

    // Establish healthy baseline with 3 good reads
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportTemperatureOK(85.5);
    sensorHealth.reportTemperatureOK(86.0);
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isTemperatureValid(),
                "Temperature valid after 3 good reads");

    // 2 failures (below threshold)
    sensorHealth.reportTemperatureFail();
    sensorHealth.reportTemperatureFail();
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isTemperatureValid(),
                "Still valid after 2 failures (threshold is 3)");

    // 3rd failure trips fault
    sensorHealth.reportTemperatureFail();
    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isTemperatureValid(),
                "Temperature faulted after 3 consecutive failures");

    TEST_ASSERT(sensorHealth.getTemperatureHealth()->faulted,
                "Temperature fault flag set");

    TEST_ASSERT(deviceManager.isFaulted(DEV_TEMP_RTD),
                "DeviceManager reports RTD faulted");

    // Recovery: need 3 good reads
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isTemperatureValid(),
                "Still faulted after 2 good reads");

    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isTemperatureValid(),
                "Temperature recovered after 3 consecutive good reads");

    TEST_ASSERT(!deviceManager.isFaulted(DEV_TEMP_RTD),
                "DeviceManager reports RTD no longer faulted");
}

// ============================================================================
// T13: VALVE FEEDBACK FAULT AND RECOVERY
// ============================================================================

void test_feedback_fault_recovery() {
    TEST_BEGIN("T13: Valve Feedback Fault/Recovery");
    resetTestState();

    deviceManager.setInstalled(DEV_VALVE_FEEDBACK, true);

    // Establish healthy baseline
    sensorHealth.reportFeedbackOK(12.0);
    sensorHealth.reportFeedbackOK(12.0);
    sensorHealth.reportFeedbackOK(12.0);
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isFeedbackValid(),
                "Feedback valid after 3 good reads");

    // Trip fault with 3 failures
    sensorHealth.reportFeedbackFail();
    sensorHealth.reportFeedbackFail();
    sensorHealth.reportFeedbackFail();
    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isFeedbackValid(),
                "Feedback faulted after 3 consecutive failures");

    TEST_ASSERT(deviceManager.isFaulted(DEV_VALVE_FEEDBACK),
                "DeviceManager reports valve feedback faulted");

    // Recover with 3 good reads
    sensorHealth.reportFeedbackOK(12.0);
    sensorHealth.reportFeedbackOK(12.0);
    sensorHealth.reportFeedbackOK(12.0);
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isFeedbackValid(),
                "Feedback recovered after 3 good reads");

    TEST_ASSERT(!deviceManager.isFaulted(DEV_VALVE_FEEDBACK),
                "DeviceManager reports valve feedback recovered");
}

// ============================================================================
// T14: MULTIPLE CONCURRENT SENSOR FAULTS
// ============================================================================

void test_concurrent_faults() {
    TEST_BEGIN("T14: Multiple Concurrent Sensor Faults");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    deviceManager.setInstalled(DEV_TEMP_RTD, true);
    deviceManager.setInstalled(DEV_VALVE_FEEDBACK, true);

    // Establish all sensors healthy
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportFeedbackOK(12.0);
    sensorHealth.reportFeedbackOK(12.0);
    sensorHealth.reportFeedbackOK(12.0);
    sensorHealth.reportMeasurementCycle();
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isConductivityValid(), "Cond valid initially");
    TEST_ASSERT(sensorHealth.isTemperatureValid(), "Temp valid initially");
    TEST_ASSERT(sensorHealth.isFeedbackValid(), "Feedback valid initially");
    TEST_ASSERT(!sensorHealth.isInSafeMode(), "Not in safe mode initially");

    // Fail ALL sensors simultaneously
    for (int i = 0; i < HEALTH_CONSECUTIVE_FAIL_LIMIT; i++) {
        sensorHealth.reportConductivityFail();
        sensorHealth.reportTemperatureFail();
        sensorHealth.reportFeedbackFail();
    }
    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isConductivityValid(), "Cond faulted");
    TEST_ASSERT(!sensorHealth.isTemperatureValid(), "Temp faulted");
    TEST_ASSERT(!sensorHealth.isFeedbackValid(), "Feedback faulted");

    // Conductivity is a required device — safe mode should trigger
    TEST_ASSERT(sensorHealth.isInSafeMode(),
                "Safe mode entered when conductivity + others faulted");

    uint8_t faulted = deviceManager.countFaulted();
    TEST_ASSERT(faulted >= 3, "At least 3 devices faulted simultaneously");

    Serial.printf("  Faulted devices: %d, Safe mode: %s\n",
                  faulted, sensorHealth.getSafeModeString());
}

// ============================================================================
// T15: DEVICE MANAGER BOUNDARY CHECKS
// ============================================================================

void test_boundary_checks() {
    TEST_BEGIN("T15: DeviceManager Boundary Checks");
    resetTestState();

    // Test with invalid device ID (>= DEV_COUNT)
    device_id_t invalid_id = (device_id_t)DEV_COUNT;
    device_id_t large_id = (device_id_t)255;

    TEST_ASSERT(!deviceManager.isEnabled(invalid_id),
                "isEnabled(DEV_COUNT) returns false");

    TEST_ASSERT(!deviceManager.isOperational(invalid_id),
                "isOperational(DEV_COUNT) returns false");

    TEST_ASSERT(!deviceManager.isRequired(invalid_id),
                "isRequired(DEV_COUNT) returns false");

    TEST_ASSERT(!deviceManager.isInstalled(invalid_id),
                "isInstalled(DEV_COUNT) returns false");

    TEST_ASSERT(deviceManager.getDeviceState(invalid_id) == DEVSTATE_DISABLED,
                "getDeviceState(invalid) returns DISABLED");

    TEST_ASSERT(strcmp(deviceManager.getStateString(invalid_id), "INVALID") == 0,
                "getStateString(invalid) returns INVALID");

    TEST_ASSERT(deviceManager.getDeviceInfo(invalid_id) == nullptr,
                "getDeviceInfo(invalid) returns nullptr");

    // isFaulted with invalid ID returns true (documented behavior)
    // This is questionable — a non-existent device shouldn't appear faulted
    bool faulted_result = deviceManager.isFaulted(invalid_id);
    TEST_ASSERT(faulted_result == true,
                "isFaulted(invalid) returns true (KNOWN ISSUE: should be false)");

    // setEnabled with invalid ID returns false
    TEST_ASSERT(!deviceManager.setEnabled(invalid_id, true),
                "setEnabled(invalid) returns false");

    TEST_ASSERT(!deviceManager.setEnabled(large_id, true),
                "setEnabled(255) returns false");

    // getFaultCount for invalid ID returns 0
    TEST_ASSERT(deviceManager.getFaultCount(invalid_id) == 0,
                "getFaultCount(invalid) returns 0");

    // Operations on invalid IDs don't crash
    deviceManager.reportFault(invalid_id);
    deviceManager.reportOK(invalid_id);
    deviceManager.clearFault(invalid_id);
    deviceManager.setInstalled(invalid_id, true);
    TEST_ASSERT(true, "Operations on invalid device IDs don't crash");
}

// ============================================================================
// T16: SAFE MODE STALE_DATA ENTRY PATH
// ============================================================================

void test_safe_mode_stale_data() {
    TEST_BEGIN("T16: Safe Mode STALE_DATA Entry");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);

    // Start with valid measurements
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportMeasurementCycle();
    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isInSafeMode(), "Not in safe mode initially");
    TEST_ASSERT(sensorHealth.isMeasurementFresh(), "Measurement is fresh");

    // Simulate measurement task stall: stop calling reportMeasurementCycle()
    // and wait for stale threshold
    Serial.println("  (Waiting for measurement stale threshold...)");
    delay(HEALTH_STALE_READING_MS + 500);

    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isMeasurementFresh(),
                "Measurement is now stale");

    TEST_ASSERT(sensorHealth.isInSafeMode(),
                "Safe mode entered due to stale data");

    TEST_ASSERT(sensorHealth.getSafeMode() == SAFE_MODE_STALE_DATA,
                "Safe mode reason is STALE_DATA");

    Serial.printf("  Safe mode: %s, measurement age: %lu ms\n",
                  sensorHealth.getSafeModeString(),
                  sensorHealth.getMeasurementAge());
}

// ============================================================================
// T17: UNKNOWN FEED MODE DEPENDENCY CHECK
// ============================================================================

void test_unknown_feed_mode() {
    TEST_BEGIN("T17: Unknown Feed Mode Dependency Check");
    resetTestState();

    // Install all devices
    for (int i = 0; i < DEV_COUNT; i++) {
        deviceManager.setInstalled((device_id_t)i, true);
    }

    const char* dep = nullptr;

    // Test known modes first
    bool ok_mode_a = deviceManager.checkPumpModeDependency(0, 1, &dep);
    TEST_ASSERT(ok_mode_a, "Mode A (blowdown feed) passes with all devices");

    // Test unknown mode values — these silently pass (no switch case)
    ok_mode_a = deviceManager.checkPumpModeDependency(0, 0, &dep);
    TEST_ASSERT(ok_mode_a,
                "Mode 0 (disabled/unknown) passes silently — no dependency");

    bool ok_unknown = deviceManager.checkPumpModeDependency(0, 6, &dep);
    TEST_ASSERT(ok_unknown,
                "Mode 6 (undefined) passes silently (KNOWN GAP: should reject?)");

    bool ok_large = deviceManager.checkPumpModeDependency(0, 255, &dep);
    TEST_ASSERT(ok_large,
                "Mode 255 (invalid) passes silently (KNOWN GAP)");

    // Test with pump_id that maps outside DEV_COUNT
    // pump_dev = DEV_PUMP_H2SO3 + pump_id = 4 + 10 = 14 >= DEV_COUNT
    bool ok_bad_pump = deviceManager.checkPumpModeDependency(10, 1, &dep);
    // With pump_id=10, pump_dev=14 which is >= DEV_COUNT
    // isOperational(14) returns false, so dependency should fail
    TEST_ASSERT(!ok_bad_pump,
                "Invalid pump_id=10 fails dependency (pump_dev >= DEV_COUNT)");
}

// ============================================================================
// T18: FAULT COUNTER NEAR LIMITS
// ============================================================================

void test_fault_counter_limits() {
    TEST_BEGIN("T18: Fault Counter Near Limits");
    resetTestState();

    deviceManager.setInstalled(DEV_TEMP_RTD, true);

    // Inject many faults to test counter behavior
    for (uint32_t i = 0; i < 1000; i++) {
        deviceManager.reportFault(DEV_TEMP_RTD);
    }

    TEST_ASSERT(deviceManager.isFaulted(DEV_TEMP_RTD),
                "Device faulted after 1000 fault reports");

    uint16_t fault_count = deviceManager.getFaultCount(DEV_TEMP_RTD);
    TEST_ASSERT(fault_count == 1000,
                "Fault count tracks correctly at 1000");

    const device_info_t* info = deviceManager.getDeviceInfo(DEV_TEMP_RTD);
    TEST_ASSERT(info != nullptr, "getDeviceInfo returns valid pointer");
    TEST_ASSERT(info->total_faults == 1000,
                "Total faults tracks correctly at 1000");

    // A single reportOK resets fault_count to 0
    deviceManager.reportOK(DEV_TEMP_RTD);
    TEST_ASSERT(deviceManager.getFaultCount(DEV_TEMP_RTD) == 0,
                "Fault count reset to 0 after single OK report");

    // But total_faults is a lifetime counter — it stays
    info = deviceManager.getDeviceInfo(DEV_TEMP_RTD);
    TEST_ASSERT(info->total_faults == 1000,
                "Total lifetime faults preserved after recovery");

    // Device should be healthy again
    TEST_ASSERT(!deviceManager.isFaulted(DEV_TEMP_RTD),
                "Device recovered after OK report");
}

// ============================================================================
// T19: TEMPERATURE SENTINEL VALUE DETECTION
// ============================================================================

void test_temperature_sentinel() {
    TEST_BEGIN("T19: Temperature Sentinel Value Detection");
    resetTestState();

    deviceManager.setInstalled(DEV_TEMP_RTD, true);

    // Establish healthy baseline
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isTemperatureValid(), "Temp valid initially");

    // Report sentinel value (-999) — should be treated as fault
    sensorHealth.reportTemperatureOK(-999.0);
    const sensor_health_t* h = sensorHealth.getTemperatureHealth();
    TEST_ASSERT(h->consecutive_failures > 0,
                "Sentinel -999 increments failure count");

    // Report value just above sentinel threshold (-998)
    // The check is: value <= HEALTH_TEMP_SENTINEL + 1.0f = -998.0
    sensorHealth.reportTemperatureOK(-998.0);
    h = sensorHealth.getTemperatureHealth();
    TEST_ASSERT(h->consecutive_failures > 1,
                "Value -998 (at sentinel+1 boundary) treated as fault");

    // Report value just above the boundary (-997) — should be accepted
    // Reset first to have clean state
    resetTestState();
    deviceManager.setInstalled(DEV_TEMP_RTD, true);
    sensorHealth.reportTemperatureOK(-997.0);
    h = sensorHealth.getTemperatureHealth();
    TEST_ASSERT(h->consecutive_failures == 0,
                "Value -997 (above sentinel+1) treated as OK");

    // NOTE: This means the sensor doesn't catch invalid temps like -50°C
    // (short-circuit RTD). Only the -999/-998 sentinel range is rejected.
    sensorHealth.reportTemperatureOK(-50.0);
    h = sensorHealth.getTemperatureHealth();
    TEST_ASSERT(h->consecutive_failures == 0,
                "-50°C passes validation (KNOWN GAP: no range check)");

    // Very high temps also pass (no upper bound validation)
    sensorHealth.reportTemperatureOK(500.0);
    h = sensorHealth.getTemperatureHealth();
    TEST_ASSERT(h->consecutive_failures == 0,
                "500°C passes validation (KNOWN GAP: no upper range check)");
}

// ============================================================================
// T20: CONDUCTIVITY RANGE BOUNDARY VALUES
// ============================================================================

void test_conductivity_boundaries() {
    TEST_BEGIN("T20: Conductivity Range Boundary Values");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);

    // Reset to clean state
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.update();
    TEST_ASSERT(sensorHealth.isConductivityValid(), "Valid after baseline");

    // Test exact minimum boundary (0.1 uS/cm) — should be valid
    resetTestState();
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    sensorHealth.reportConductivityOK(0.1);
    const sensor_health_t* h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(h->consecutive_failures == 0,
                "0.1 uS/cm (exact minimum) is accepted");

    // Test just below minimum (0.09 uS/cm) — should be rejected
    resetTestState();
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    sensorHealth.reportConductivityOK(0.09);
    h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(h->consecutive_failures > 0 || h->value_suspect,
                "0.09 uS/cm (below minimum) is rejected");

    // Test exact maximum boundary (15000 uS/cm) — should be valid
    resetTestState();
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    sensorHealth.reportConductivityOK(15000.0);
    h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(h->consecutive_failures == 0,
                "15000 uS/cm (exact maximum) is accepted");

    // Test just above maximum (15001 uS/cm) — should be rejected
    resetTestState();
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    sensorHealth.reportConductivityOK(15001.0);
    h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(h->consecutive_failures > 0 || h->value_suspect,
                "15001 uS/cm (above maximum) is rejected");

    // Test zero (0.0 uS/cm) — below minimum, should be rejected
    resetTestState();
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    sensorHealth.reportConductivityOK(0.0);
    h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(h->consecutive_failures > 0 || h->value_suspect,
                "0.0 uS/cm is rejected");

    // Test negative value — should be rejected
    resetTestState();
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    sensorHealth.reportConductivityOK(-1.0);
    h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(h->consecutive_failures > 0 || h->value_suspect,
                "Negative conductivity is rejected");
}

// ============================================================================
// T21: SAFE MODE HOLD TIME ENFORCEMENT
// ============================================================================

void test_safe_mode_hold_time() {
    TEST_BEGIN("T21: Safe Mode Hold Time Enforcement");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);

    // Trigger safe mode via sensor fault
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isInSafeMode(), "Safe mode entered");

    // Try to manually exit immediately — should be denied
    sensorHealth.exitSafeMode();
    TEST_ASSERT(sensorHealth.isInSafeMode(),
                "Manual exit denied before hold time");

    // Recover the sensor
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportMeasurementCycle();

    // update() should also deny auto-exit before hold time
    sensorHealth.update();
    TEST_ASSERT(sensorHealth.isInSafeMode(),
                "Auto-exit denied before hold time even with recovered sensors");

    // Wait for hold time to elapse
    Serial.println("  (Waiting for safe mode hold time to elapse...)");
    delay(HEALTH_SAFE_MODE_HOLD_MS + 500);

    // Now manual exit should work
    sensorHealth.exitSafeMode();
    TEST_ASSERT(!sensorHealth.isInSafeMode(),
                "Manual exit succeeds after hold time");
}

// ============================================================================
// T22: DEVICE FAULT-CLEAR-REFAULT CYCLE
// ============================================================================

void test_fault_clear_refault() {
    TEST_BEGIN("T22: Fault-Clear-Refault Cycle");
    resetTestState();

    deviceManager.setInstalled(DEV_PUMP_H2SO3, true);

    TEST_ASSERT(deviceManager.isOperational(DEV_PUMP_H2SO3),
                "Pump operational initially");

    // First fault cycle
    for (int i = 0; i < FAULT_CONSECUTIVE_THRESHOLD; i++) {
        deviceManager.reportFault(DEV_PUMP_H2SO3);
    }
    TEST_ASSERT(deviceManager.isFaulted(DEV_PUMP_H2SO3),
                "Pump faulted after first fault cycle");
    TEST_ASSERT(!deviceManager.isOperational(DEV_PUMP_H2SO3),
                "Pump not operational while faulted");

    // Clear fault manually
    deviceManager.clearFault(DEV_PUMP_H2SO3);
    TEST_ASSERT(!deviceManager.isFaulted(DEV_PUMP_H2SO3),
                "Pump cleared after clearFault()");
    TEST_ASSERT(deviceManager.isOperational(DEV_PUMP_H2SO3),
                "Pump operational after clearFault()");

    // Second fault cycle — should fault again
    for (int i = 0; i < FAULT_CONSECUTIVE_THRESHOLD; i++) {
        deviceManager.reportFault(DEV_PUMP_H2SO3);
    }
    TEST_ASSERT(deviceManager.isFaulted(DEV_PUMP_H2SO3),
                "Pump faulted again after second fault cycle");

    // Recovery via reportOK
    deviceManager.reportOK(DEV_PUMP_H2SO3);
    TEST_ASSERT(deviceManager.isOperational(DEV_PUMP_H2SO3),
                "Pump operational after reportOK()");

    // Verify total_faults accumulated across cycles
    const device_info_t* info = deviceManager.getDeviceInfo(DEV_PUMP_H2SO3);
    TEST_ASSERT(info->total_faults == (FAULT_CONSECUTIVE_THRESHOLD * 2),
                "Total faults accumulated across both cycles");
}

// ============================================================================
// MAIN
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println();
    Serial.println("=============================================");
    Serial.println("  CT-6 Fault Scenario Test Harness");
    Serial.println("  Testing: DeviceManager, SelfTest,");
    Serial.println("           SensorHealthMonitor");
    Serial.println("=============================================");

    // Run all tests — Original T1-T11
    test_probe_missing_at_boot();
    test_probe_intermittent();
    test_sensor_disabled();
    test_motor_disabled();
    test_reenable_runtime();
    test_stale_reading();
    test_i2c_recovery();
    test_dependency_checking();
    test_safe_mode();
    test_zero_reading_rejection();
    test_status_reporting();

    // New tests — T12-T22
    test_temperature_fault_recovery();
    test_feedback_fault_recovery();
    test_concurrent_faults();
    test_boundary_checks();
    test_safe_mode_stale_data();
    test_unknown_feed_mode();
    test_fault_counter_limits();
    test_temperature_sentinel();
    test_conductivity_boundaries();
    test_safe_mode_hold_time();
    test_fault_clear_refault();

    // Summary
    Serial.println();
    Serial.println("=============================================");
    Serial.printf("  RESULTS: %d/%d passed, %d failed\n",
                  tests_passed, tests_run, tests_failed);
    Serial.println("=============================================");

    if (tests_failed == 0) {
        Serial.println("  ALL TESTS PASSED");
    } else {
        Serial.println("  *** SOME TESTS FAILED ***");
    }
}

void loop() {
    // Nothing — tests run once in setup()
    delay(10000);
}

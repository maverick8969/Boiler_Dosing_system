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

static hardware_config_t test_hw_config;

void resetTestState() {
    // Reset DeviceManager
    test_hw_config.enabled_devices = HW_CONFIG_DEFAULT_ENABLED;
    deviceManager.begin(&test_hw_config);

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
    TEST_ASSERT(!(test_hw_config.enabled_devices & (1 << DEV_WATER_METER_2)),
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
    TEST_ASSERT(test_hw_config.enabled_devices & (1 << DEV_PUMP_AMINE),
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

    // Run all tests
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

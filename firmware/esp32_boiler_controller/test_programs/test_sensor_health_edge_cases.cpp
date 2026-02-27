/**
 * @file test_sensor_health_edge_cases.cpp
 * @brief Edge Case Tests for SensorHealthMonitor
 *
 * Standalone test program focused on SensorHealthMonitor edge cases,
 * safe mode transitions, and integration with DeviceManager.
 *
 * Upload to ESP32 independently (not part of the main firmware build).
 * Results are printed to Serial at 115200 baud.
 *
 * Test Scenarios:
 *   E1: Staleness detection across all sensor types
 *   E2: Safe mode auto-exit ignores temperature check (known gap)
 *   E3: Measurement freshness lifecycle
 *   E4: I2C recovery rate limiting
 *   E5: Rapid fault/recovery oscillation
 *   E6: Feedback mA value not range-validated (known gap)
 *   E7: Safe mode re-entry after exit
 *   E8: Sensor disabled suppresses fault counting
 *   E9: GetEnabledMask and GetFaultedMask consistency
 *   E10: Health struct initial state verification
 *   E11: Multiple safe mode reasons (priority order)
 *   E12: Conductivity OK with out-of-range resets on next valid read
 */

#include <Arduino.h>
#include "device_manager.h"
#include "sensor_health.h"
#include "self_test.h"
#include "config.h"

// ============================================================================
// TEST FRAMEWORK (minimal, matches test_fault_scenarios.cpp)
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
    test_enabled_devices = HW_CONFIG_DEFAULT_ENABLED;
    deviceManager.begin(&test_enabled_devices);
    sensorHealth.begin();
}

// ============================================================================
// E1: STALENESS DETECTION ACROSS ALL SENSOR TYPES
// ============================================================================

void test_staleness_all_sensors() {
    TEST_BEGIN("E1: Staleness Detection — All Sensors");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    deviceManager.setInstalled(DEV_TEMP_RTD, true);
    deviceManager.setInstalled(DEV_VALVE_FEEDBACK, true);

    // Report good readings to establish last_good_time
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportFeedbackOK(12.0);
    sensorHealth.update();

    // All should be fresh
    const sensor_health_t* cond_h = sensorHealth.getConductivityHealth();
    const sensor_health_t* temp_h = sensorHealth.getTemperatureHealth();
    const sensor_health_t* fb_h   = sensorHealth.getFeedbackHealth();

    TEST_ASSERT(!cond_h->stale, "Conductivity not stale after fresh read");
    TEST_ASSERT(!temp_h->stale, "Temperature not stale after fresh read");
    TEST_ASSERT(!fb_h->stale, "Feedback not stale after fresh read");

    // Wait for feedback stale threshold (3s) but not conductivity (5s)
    Serial.println("  (Waiting for feedback stale threshold: 3s...)");
    delay(HEALTH_STALE_FEEDBACK_MS + 500);
    sensorHealth.update();

    fb_h = sensorHealth.getFeedbackHealth();
    cond_h = sensorHealth.getConductivityHealth();
    temp_h = sensorHealth.getTemperatureHealth();

    TEST_ASSERT(fb_h->stale,
                "Feedback is stale after 3.5s");
    // Conductivity staleness uses 5s threshold, so it may or may not be stale
    // depending on timing. We check temp too (uses 5s).
    Serial.printf("  Cond stale: %s (age: %lu ms), Temp stale: %s, FB stale: %s\n",
        cond_h->stale ? "YES" : "NO",
        millis() - cond_h->last_good_time,
        temp_h->stale ? "YES" : "NO",
        fb_h->stale ? "YES" : "NO");

    // Wait for remaining time to hit conductivity/temp stale threshold
    delay(2500);
    sensorHealth.update();

    cond_h = sensorHealth.getConductivityHealth();
    temp_h = sensorHealth.getTemperatureHealth();

    TEST_ASSERT(cond_h->stale,
                "Conductivity stale after 6s total");
    TEST_ASSERT(temp_h->stale,
                "Temperature stale after 6s total");
}

// ============================================================================
// E2: SAFE MODE AUTO-EXIT IGNORES TEMPERATURE (KNOWN GAP)
// ============================================================================

void test_safe_mode_exit_temp_gap() {
    TEST_BEGIN("E2: Safe Mode Auto-Exit Temperature Gap");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    deviceManager.setInstalled(DEV_TEMP_RTD, true);

    // Establish all sensors healthy
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportTemperatureOK(85.0);
    sensorHealth.reportMeasurementCycle();
    sensorHealth.update();

    // Fail conductivity to enter safe mode
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isInSafeMode(), "Safe mode entered");
    TEST_ASSERT(sensorHealth.getSafeMode() == SAFE_MODE_SENSOR_FAIL,
                "Reason is SENSOR_FAIL");

    // Now recover conductivity BUT keep temperature faulted
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportMeasurementCycle();

    // Fail temperature
    sensorHealth.reportTemperatureFail();
    sensorHealth.reportTemperatureFail();
    sensorHealth.reportTemperatureFail();

    // Wait for hold time
    Serial.println("  (Waiting for safe mode hold time...)");
    delay(HEALTH_SAFE_MODE_HOLD_MS + 500);

    sensorHealth.update();

    // KNOWN GAP: evaluateSafeMode() only checks conductivity + measurement
    // freshness for auto-exit. Temperature is not checked.
    // So safe mode will auto-exit even though temperature is faulted.
    bool still_in_safe = sensorHealth.isInSafeMode();
    bool temp_valid = sensorHealth.isTemperatureValid();

    Serial.printf("  After recovery: safe_mode=%s, temp_valid=%s\n",
                  still_in_safe ? "YES" : "NO",
                  temp_valid ? "YES" : "NO");

    TEST_ASSERT(!still_in_safe,
                "Safe mode auto-exited (KNOWN GAP: temp not checked)");
    TEST_ASSERT(!temp_valid,
                "Temperature is still faulted after safe mode exit");
}

// ============================================================================
// E3: MEASUREMENT FRESHNESS LIFECYCLE
// ============================================================================

void test_measurement_freshness_lifecycle() {
    TEST_BEGIN("E3: Measurement Freshness Lifecycle");
    resetTestState();

    // After begin(), _last_measurement_time is set to current millis()
    TEST_ASSERT(sensorHealth.isMeasurementFresh(),
                "Measurement fresh immediately after begin()");

    uint32_t age = sensorHealth.getMeasurementAge();
    TEST_ASSERT(age < 500,
                "Measurement age < 500ms right after begin()");

    // Report a new cycle
    delay(100);
    sensorHealth.reportMeasurementCycle();
    age = sensorHealth.getMeasurementAge();
    TEST_ASSERT(age < 100,
                "Measurement age resets after reportMeasurementCycle()");

    // Let it go stale
    Serial.println("  (Waiting for stale threshold...)");
    delay(HEALTH_STALE_READING_MS + 500);

    TEST_ASSERT(!sensorHealth.isMeasurementFresh(),
                "Measurement stale after threshold");

    age = sensorHealth.getMeasurementAge();
    TEST_ASSERT(age > HEALTH_STALE_READING_MS,
                "Measurement age exceeds stale threshold");

    // Report cycle to make it fresh again
    sensorHealth.reportMeasurementCycle();
    TEST_ASSERT(sensorHealth.isMeasurementFresh(),
                "Measurement fresh again after new cycle report");
}

// ============================================================================
// E4: I2C RECOVERY RATE LIMITING
// ============================================================================

void test_i2c_recovery_rate_limit() {
    TEST_BEGIN("E4: I2C Recovery Rate Limiting");
    // Note: I2C recovery interacts with hardware. We test the rate-limit
    // logic, not actual bus recovery.

    resetTestState();

    // First attempt should proceed (cold start)
    bool result1 = sensorHealth.attemptI2CRecovery();
    Serial.printf("  First recovery attempt: %s\n",
                  result1 ? "devices found" : "no devices");

    // Second attempt immediately should be rate-limited (return false)
    bool result2 = sensorHealth.attemptI2CRecovery();
    TEST_ASSERT(!result2,
                "Second recovery attempt blocked by rate limiter");

    // We can't easily test the 30s rate-limit window expiry in a unit test
    // without a long delay, so we document the behavior.
    Serial.printf("  Rate limit interval: %d ms\n",
                  HEALTH_I2C_RECOVERY_INTERVAL_MS);
}

// ============================================================================
// E5: RAPID FAULT/RECOVERY OSCILLATION
// ============================================================================

void test_rapid_oscillation() {
    TEST_BEGIN("E5: Rapid Fault/Recovery Oscillation");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);

    // Establish healthy state
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.update();
    TEST_ASSERT(sensorHealth.isConductivityValid(), "Valid initially");

    // Rapidly alternate fail/ok — should NOT trip fault since
    // consecutive failures never reach threshold
    for (int cycle = 0; cycle < 20; cycle++) {
        sensorHealth.reportConductivityFail();
        sensorHealth.reportConductivityOK(2500.0);
    }
    sensorHealth.update();

    const sensor_health_t* h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(h->consecutive_failures == 0,
                "No consecutive failures after alternating pattern");
    TEST_ASSERT(!h->faulted,
                "Not faulted after alternating fault/ok pattern");

    // But total_failures should reflect all the failure reports
    TEST_ASSERT(h->total_failures == 20,
                "Total failures tracks all 20 failure events");

    // Now try: fail, fail, ok, fail, fail, ok — never reaches 3 consecutive
    resetTestState();
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);

    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.update();

    h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(!h->faulted,
                "Not faulted with fail-fail-ok pattern (never 3 consecutive)");
}

// ============================================================================
// E6: FEEDBACK mA NOT RANGE VALIDATED (KNOWN GAP)
// ============================================================================

void test_feedback_no_range_validation() {
    TEST_BEGIN("E6: Feedback mA No Range Validation");
    resetTestState();

    deviceManager.setInstalled(DEV_VALVE_FEEDBACK, true);

    // reportFeedbackOK accepts any mA value — no 4-20mA range check
    // This is a known gap: out-of-range mA should indicate broken transducer

    sensorHealth.reportFeedbackOK(0.0);   // Below 4mA — should be suspect
    const sensor_health_t* h = sensorHealth.getFeedbackHealth();
    TEST_ASSERT(h->consecutive_failures == 0,
                "0 mA accepted without range check (KNOWN GAP)");

    sensorHealth.reportFeedbackOK(-5.0);  // Negative — clearly invalid
    h = sensorHealth.getFeedbackHealth();
    TEST_ASSERT(h->consecutive_failures == 0,
                "Negative mA accepted without range check (KNOWN GAP)");

    sensorHealth.reportFeedbackOK(100.0); // Way above 20mA
    h = sensorHealth.getFeedbackHealth();
    TEST_ASSERT(h->consecutive_failures == 0,
                "100 mA accepted without range check (KNOWN GAP)");

    // All are treated as OK since reportFeedbackOK has no range check
    TEST_ASSERT(!h->value_suspect,
                "value_suspect not set for any mA value (KNOWN GAP)");
}

// ============================================================================
// E7: SAFE MODE RE-ENTRY AFTER EXIT
// ============================================================================

void test_safe_mode_reentry() {
    TEST_BEGIN("E7: Safe Mode Re-entry After Exit");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);

    // First safe mode cycle: fault -> safe mode -> recover -> exit
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.update();
    TEST_ASSERT(sensorHealth.isInSafeMode(), "First safe mode entry");

    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportMeasurementCycle();

    Serial.println("  (Waiting for hold time...)");
    delay(HEALTH_SAFE_MODE_HOLD_MS + 500);
    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isInSafeMode(), "Exited first safe mode");

    // Second safe mode cycle: should be able to re-enter
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.update();

    TEST_ASSERT(sensorHealth.isInSafeMode(),
                "Safe mode re-entered after previous exit");
    TEST_ASSERT(sensorHealth.getSafeMode() == SAFE_MODE_SENSOR_FAIL,
                "Re-entry reason is SENSOR_FAIL");
}

// ============================================================================
// E8: DISABLED SENSOR SUPPRESSES SAFE MODE
// ============================================================================

void test_disabled_sensor_no_safe_mode() {
    TEST_BEGIN("E8: Disabled Sensor Suppresses Safe Mode");
    resetTestState();

    // Enable but then disable the conductivity probe
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    // Conductivity is required — can't disable it via setEnabled
    // But we can test with a non-required sensor fault

    // Install all, then disable conductivity for this test
    // Since conductivity is required, we test with a different approach:
    // If conductivity is NOT enabled, evaluateSafeMode should not trigger
    // even if it's faulted. The code checks:
    //   if (_cond_health.faulted && deviceManager.isEnabled(DEV_CONDUCTIVITY_PROBE))
    // So a disabled-but-faulted conductivity should NOT trigger safe mode.

    // We can't disable conductivity (it's required), so let's test with
    // a scenario where no required sensors fault
    deviceManager.setInstalled(DEV_TEMP_RTD, true);

    // Fault temperature (not required device)
    sensorHealth.reportTemperatureFail();
    sensorHealth.reportTemperatureFail();
    sensorHealth.reportTemperatureFail();
    sensorHealth.reportMeasurementCycle();
    sensorHealth.update();

    TEST_ASSERT(!sensorHealth.isTemperatureValid(),
                "Temperature is faulted");
    TEST_ASSERT(!sensorHealth.isInSafeMode(),
                "Safe mode NOT entered for non-required sensor fault");

    // Also verify: disabled optional device doesn't count as faulted
    deviceManager.setEnabled(DEV_WATER_METER_2, false);
    TEST_ASSERT(!deviceManager.isFaulted(DEV_WATER_METER_2),
                "Disabled device not reported as faulted");
}

// ============================================================================
// E9: ENABLED AND FAULTED MASK CONSISTENCY
// ============================================================================

void test_mask_consistency() {
    TEST_BEGIN("E9: Enabled/Faulted Mask Consistency");
    resetTestState();

    // Install several devices
    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);
    deviceManager.setInstalled(DEV_TEMP_RTD, true);
    deviceManager.setInstalled(DEV_BLOWDOWN_VALVE, true);
    deviceManager.setInstalled(DEV_PUMP_H2SO3, true);
    deviceManager.setInstalled(DEV_LCD_DISPLAY, true);

    uint16_t enabled_mask = deviceManager.getEnabledMask();
    uint16_t faulted_mask = deviceManager.getFaultedMask();

    // All devices start as enabled (default), none faulted
    TEST_ASSERT(enabled_mask == HW_CONFIG_DEFAULT_ENABLED,
                "Enabled mask matches default (all enabled)");
    TEST_ASSERT(faulted_mask == 0,
                "Faulted mask is 0 initially");

    // Disable WM2
    deviceManager.setEnabled(DEV_WATER_METER_2, false);
    enabled_mask = deviceManager.getEnabledMask();
    TEST_ASSERT(!(enabled_mask & (1 << DEV_WATER_METER_2)),
                "WM2 bit cleared in enabled mask");

    // Fault the RTD
    for (int i = 0; i < FAULT_CONSECUTIVE_THRESHOLD; i++) {
        deviceManager.reportFault(DEV_TEMP_RTD);
    }
    faulted_mask = deviceManager.getFaultedMask();
    TEST_ASSERT(faulted_mask & (1 << DEV_TEMP_RTD),
                "RTD bit set in faulted mask");

    // Fault pump
    for (int i = 0; i < FAULT_CONSECUTIVE_THRESHOLD; i++) {
        deviceManager.reportFault(DEV_PUMP_H2SO3);
    }
    faulted_mask = deviceManager.getFaultedMask();
    TEST_ASSERT(faulted_mask & (1 << DEV_PUMP_H2SO3),
                "Pump bit set in faulted mask");

    // Verify counts match mask popcount
    uint8_t faulted_count = deviceManager.countFaulted();
    uint8_t mask_popcount = 0;
    for (int i = 0; i < 16; i++) {
        if (faulted_mask & (1 << i)) mask_popcount++;
    }
    TEST_ASSERT(faulted_count == mask_popcount,
                "countFaulted() matches faulted mask popcount");

    Serial.printf("  Enabled mask: 0x%04X, Faulted mask: 0x%04X, "
                  "Count: %d operational, %d faulted\n",
                  enabled_mask, faulted_mask,
                  deviceManager.countOperational(), faulted_count);
}

// ============================================================================
// E10: HEALTH STRUCT INITIAL STATE
// ============================================================================

void test_health_initial_state() {
    TEST_BEGIN("E10: Health Struct Initial State");
    resetTestState();

    const sensor_health_t* cond = sensorHealth.getConductivityHealth();
    const sensor_health_t* temp = sensorHealth.getTemperatureHealth();
    const sensor_health_t* fb   = sensorHealth.getFeedbackHealth();

    // After begin(), all health trackers should be in initial state
    TEST_ASSERT(cond->last_good_time == 0, "Cond last_good_time is 0");
    TEST_ASSERT(cond->last_read_time == 0, "Cond last_read_time is 0");
    TEST_ASSERT(cond->consecutive_failures == 0, "Cond consecutive_failures is 0");
    TEST_ASSERT(cond->consecutive_ok == 0, "Cond consecutive_ok is 0");
    TEST_ASSERT(cond->total_failures == 0, "Cond total_failures is 0");
    TEST_ASSERT(cond->stale == true, "Cond stale is true initially");
    TEST_ASSERT(cond->faulted == false, "Cond faulted is false initially");
    TEST_ASSERT(cond->value_suspect == false, "Cond value_suspect is false");

    TEST_ASSERT(temp->stale == true, "Temp stale is true initially");
    TEST_ASSERT(fb->stale == true, "Feedback stale is true initially");

    // isConductivityValid should be false (stale)
    TEST_ASSERT(!sensorHealth.isConductivityValid(),
                "Conductivity invalid initially (stale)");
    TEST_ASSERT(!sensorHealth.isTemperatureValid(),
                "Temperature invalid initially (stale)");
    TEST_ASSERT(!sensorHealth.isFeedbackValid(),
                "Feedback invalid initially (stale)");

    // Safe mode should be NONE
    TEST_ASSERT(!sensorHealth.isInSafeMode(),
                "Not in safe mode initially");
    TEST_ASSERT(sensorHealth.getSafeMode() == SAFE_MODE_NONE,
                "Safe mode reason is NONE");

    // Safe mode string
    TEST_ASSERT(strcmp(sensorHealth.getSafeModeString(), "NORMAL") == 0,
                "Safe mode string is 'NORMAL'");
}

// ============================================================================
// E11: MULTIPLE SAFE MODE REASONS (PRIORITY)
// ============================================================================

void test_safe_mode_priority() {
    TEST_BEGIN("E11: Safe Mode Reason Priority");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);

    // Scenario: Both sensor fault AND stale data conditions are true
    // The sensor fault check comes first in evaluateSafeMode()

    // Fault the sensor
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();
    sensorHealth.reportConductivityFail();

    // Also make measurement stale — but DON'T call reportMeasurementCycle()
    // The _last_measurement_time was set by begin(), so wait for it to go stale
    Serial.println("  (Waiting for stale condition...)");
    delay(HEALTH_STALE_READING_MS + 500);

    sensorHealth.update();

    // Both conditions true, but SENSOR_FAIL should take priority (checked first)
    TEST_ASSERT(sensorHealth.isInSafeMode(), "Safe mode entered");
    TEST_ASSERT(sensorHealth.getSafeMode() == SAFE_MODE_SENSOR_FAIL,
                "Reason is SENSOR_FAIL (has priority over STALE_DATA)");

    // Once in safe mode, the reason doesn't change even if the initial
    // cause clears (as long as we stay in safe mode)
}

// ============================================================================
// E12: CONDUCTIVITY SUSPECT FLAG LIFECYCLE
// ============================================================================

void test_conductivity_suspect_lifecycle() {
    TEST_BEGIN("E12: Conductivity Suspect Flag Lifecycle");
    resetTestState();

    deviceManager.setInstalled(DEV_CONDUCTIVITY_PROBE, true);

    // Start with valid reading
    sensorHealth.reportConductivityOK(2500.0);
    const sensor_health_t* h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(!h->value_suspect, "Not suspect after valid reading");

    // Report out-of-range value — sets value_suspect
    sensorHealth.reportConductivityOK(20000.0);
    h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(h->value_suspect, "Suspect after out-of-range value");
    TEST_ASSERT(h->consecutive_failures > 0,
                "Out-of-range counts as failure");

    // Report valid value — clears value_suspect
    sensorHealth.reportConductivityOK(2500.0);
    h = sensorHealth.getConductivityHealth();
    TEST_ASSERT(!h->value_suspect, "Suspect cleared after valid reading");

    // Verify isConductivityValid checks value_suspect
    // Force suspect without fault
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.reportConductivityOK(2500.0);
    sensorHealth.update();
    TEST_ASSERT(sensorHealth.isConductivityValid(), "Valid before suspect test");

    sensorHealth.reportConductivityOK(20000.0);  // Out-of-range
    sensorHealth.update();
    TEST_ASSERT(!sensorHealth.isConductivityValid(),
                "Invalid when value_suspect is true (even before full fault)");
}

// ============================================================================
// MAIN
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println();
    Serial.println("=============================================");
    Serial.println("  CT-6 Sensor Health Edge Case Tests");
    Serial.println("  Testing: SensorHealthMonitor edge cases,");
    Serial.println("           safe mode transitions,");
    Serial.println("           DeviceManager integration");
    Serial.println("=============================================");

    // Run all tests
    test_staleness_all_sensors();
    test_safe_mode_exit_temp_gap();
    test_measurement_freshness_lifecycle();
    test_i2c_recovery_rate_limit();
    test_rapid_oscillation();
    test_feedback_no_range_validation();
    test_safe_mode_reentry();
    test_disabled_sensor_no_safe_mode();
    test_mask_consistency();
    test_health_initial_state();
    test_safe_mode_priority();
    test_conductivity_suspect_lifecycle();

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

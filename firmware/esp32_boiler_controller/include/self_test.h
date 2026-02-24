/**
 * @file self_test.h
 * @brief Power-On Self-Test (POST) Module
 *
 * Runs at boot to verify all hardware peripherals are present and responding.
 * Results are reported to DeviceManager and displayed on Serial / LCD.
 *
 * Tests performed:
 *   - I2C bus scan (LCD, ADS1115)
 *   - SPI device verification (MAX31865, SD card)
 *   - UART probe (EZO-EC)
 *   - GPIO input level checks (water meters, aux inputs)
 *   - ESP32 reset reason logging
 */

#ifndef SELF_TEST_H
#define SELF_TEST_H

#include <Arduino.h>
#include "device_manager.h"

// ============================================================================
// SELF-TEST RESULT STRUCTURE
// ============================================================================

typedef struct {
    uint16_t tested_mask;      // Bitmask of devices tested
    uint16_t passed_mask;      // Bitmask of devices that passed
    uint16_t failed_mask;      // Bitmask of devices that failed
    uint16_t skipped_mask;     // Bitmask of devices skipped (disabled)
    uint8_t  total_tested;
    uint8_t  total_passed;
    uint8_t  total_failed;
    uint8_t  total_skipped;
    bool     critical_failure; // A required device failed
    const char* reset_reason;  // Human-readable boot reason
} self_test_result_t;

// ============================================================================
// SELF-TEST CLASS
// ============================================================================

class SelfTest {
public:
    SelfTest();

    /**
     * Run complete POST sequence.
     * Updates DeviceManager with installed/not-found status.
     * Returns summary of results.
     */
    self_test_result_t runAll();

    /** Run individual tests (called by runAll, but available separately) */
    bool testI2CBus();
    bool testLCD();
    bool testADS1115();
    bool testEZO_EC();
    bool testMAX31865();
    bool testSDCard(SemaphoreHandle_t spiMutex);
    bool testWaterMeterPins();
    bool testAuxInputPins();
    bool testStepperPins();

    /** Get the ESP32 reset reason as a string */
    const char* getResetReasonString();

    /** Log the boot reason to Serial (and SD if available) */
    void logBootReason();

    /** Get the last test result */
    self_test_result_t getLastResult();

    /** Print detailed results to Serial */
    void printResults();

private:
    self_test_result_t _result;

    void recordResult(device_id_t id, bool passed);
    bool probeI2CAddress(uint8_t addr);
    bool readBackADS1115Config();
};

// Global instance
extern SelfTest selfTest;

#endif // SELF_TEST_H

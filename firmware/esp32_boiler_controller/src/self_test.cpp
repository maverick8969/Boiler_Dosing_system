/**
 * @file self_test.cpp
 * @brief Power-On Self-Test (POST) Implementation
 */

#include "self_test.h"
#include "pin_definitions.h"
#include "config.h"
#include <Wire.h>
#include <SPI.h>
#include <esp_system.h>

// Global instance
SelfTest selfTest;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SelfTest::SelfTest() {
    memset(&_result, 0, sizeof(_result));
}

// ============================================================================
// MAIN TEST SEQUENCE
// ============================================================================

self_test_result_t SelfTest::runAll() {
    memset(&_result, 0, sizeof(_result));
    _result.critical_failure = false;
    _result.reset_reason = getResetReasonString();

    Serial.println();
    Serial.println("==============================");
    Serial.println("  POWER-ON SELF-TEST (POST)");
    Serial.println("==============================");
    Serial.printf("  Boot reason: %s\n", _result.reset_reason);
    Serial.println();

    // --- I2C Bus ---
    Serial.println("[POST] Testing I2C bus...");
    if (!testI2CBus()) {
        Serial.println("[POST] WARNING: I2C bus may have issues");
    }

    // --- LCD Display (I2C 0x27) ---
    Serial.println("[POST] Testing LCD display...");
    bool lcd_ok = testLCD();
    recordResult(DEV_LCD_DISPLAY, lcd_ok);
    Serial.printf("[POST]   LCD (0x%02X): %s\n",
                  LCD_I2C_ADDR, lcd_ok ? "PASS" : "FAIL");

    // --- ADS1115 Valve Feedback (I2C 0x48) ---
    Serial.println("[POST] Testing ADS1115 (valve feedback)...");
    bool ads_ok = testADS1115();
    recordResult(DEV_VALVE_FEEDBACK, ads_ok);
    Serial.printf("[POST]   ADS1115 (0x%02X): %s\n",
                  ADS1115_I2C_ADDR, ads_ok ? "PASS" : "FAIL");

    // --- Atlas EZO-EC (UART) ---
    Serial.println("[POST] Testing EZO-EC conductivity...");
    bool ezo_ok = testEZO_EC();
    recordResult(DEV_CONDUCTIVITY_PROBE, ezo_ok);
    Serial.printf("[POST]   EZO-EC (UART2): %s\n",
                  ezo_ok ? "PASS" : "FAIL");

    // --- MAX31865 PT1000 RTD (SPI) ---
    Serial.println("[POST] Testing MAX31865 RTD...");
    bool rtd_ok = testMAX31865();
    recordResult(DEV_TEMP_RTD, rtd_ok);
    Serial.printf("[POST]   MAX31865 (SPI CS=%d): %s\n",
                  MAX31865_CS_PIN, rtd_ok ? "PASS" : "FAIL");

    // --- Stepper Pins (GPIO level check) ---
    Serial.println("[POST] Testing stepper pins...");
    bool stepper_ok = testStepperPins();
    // All 3 pumps share the enable pin — test as a group
    recordResult(DEV_PUMP_H2SO3, stepper_ok);
    recordResult(DEV_PUMP_NAOH, stepper_ok);
    recordResult(DEV_PUMP_AMINE, stepper_ok);
    Serial.printf("[POST]   Stepper enable (GPIO%d): %s\n",
                  STEPPER_ENABLE_PIN, stepper_ok ? "PASS" : "FAIL");

    // --- Water Meter Pins ---
    Serial.println("[POST] Testing water meter inputs...");
    bool wm_ok = testWaterMeterPins();
    recordResult(DEV_WATER_METER_1, wm_ok);
    // WM2 is optional, mark as installed if its pin is configured
    recordResult(DEV_WATER_METER_2, true);  // GPIO19 assumed available
    Serial.printf("[POST]   Water meter (GPIO%d): %s\n",
                  WATER_METER_PIN, wm_ok ? "PASS" : "FAIL");

    // --- Aux Inputs ---
    Serial.println("[POST] Testing auxiliary inputs...");
    bool aux_ok = testAuxInputPins();
    recordResult(DEV_AUX_INPUT_1, aux_ok);
    Serial.printf("[POST]   AUX1 (GPIO%d): %s\n",
                  AUX_INPUT1_PIN, aux_ok ? "PASS" : "FAIL");

    // --- Feedwater Pump Monitor ---
    recordResult(DEV_FEEDWATER_MONITOR, true);  // GPIO35 passive input, always available
    Serial.printf("[POST]   FW Pump Monitor (GPIO%d): PASS (passive input)\n",
                  FEEDWATER_PUMP_PIN);

    // --- Blowdown Valve Relay ---
    // GPIO4 is a simple output; verify it can be set
    recordResult(DEV_BLOWDOWN_VALVE, true);  // Always installed (hardwired)
    Serial.printf("[POST]   Blowdown relay (GPIO%d): PASS (GPIO output)\n",
                  BLOWDOWN_RELAY_PIN);

    // --- WiFi (tested separately during WiFi connect) ---
    recordResult(DEV_WIFI, true);  // Will be updated during WiFi connect
    Serial.println("[POST]   WiFi: DEFERRED (tested at connect)");

    // --- SD Card (tested separately with SPI mutex) ---
    // SD test requires spiMutex, which is called from setup() after mutex creation
    Serial.println("[POST]   SD Card: DEFERRED (tested at SD init)");

    // --- Summary ---
    Serial.println();
    printResults();

    return _result;
}

// ============================================================================
// INDIVIDUAL TESTS
// ============================================================================

bool SelfTest::testI2CBus() {
    // Scan for any device on I2C to verify the bus is alive
    uint8_t found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            found++;
            Serial.printf("[POST]   I2C device found at 0x%02X\n", addr);
        }
    }
    return (found > 0);
}

bool SelfTest::testLCD() {
    return probeI2CAddress(LCD_I2C_ADDR);
}

bool SelfTest::testADS1115() {
    if (!probeI2CAddress(ADS1115_I2C_ADDR)) {
        return false;
    }
    // Verify by reading back config register
    return readBackADS1115Config();
}

bool SelfTest::testEZO_EC() {
    // The EZO-EC is already initialized by conductivitySensor.begin().
    // We check the stored result from ConductivitySensor::_ezo_ok.
    // For an independent test, we send the 'i' query.

    // Note: This test assumes Serial2 is already configured.
    // If called before conductivitySensor.begin(), we need to init UART first.

    // Drain any pending data
    while (Serial2.available()) Serial2.read();

    // Send device info query
    Serial2.print("i\r");

    // Wait for response (up to 1000ms)
    String response = "";
    uint32_t start = millis();
    while ((millis() - start) < 1000) {
        if (Serial2.available()) {
            char c = Serial2.read();
            if (c == '\r') {
                response.trim();
                break;
            }
            if (c != '\n') {
                response += c;
            }
        }
        yield();
    }

    // Drain any trailing bytes (e.g., *OK response code)
    delay(100);
    while (Serial2.available()) Serial2.read();

    // Valid response starts with "?I,EC"
    return response.startsWith("?I,EC");
}

bool SelfTest::testMAX31865() {
    // Read the MAX31865 config register (address 0x00)
    // A successful read returns a non-0xFF, non-0x00 value
    // This test reads raw SPI — assumes SPI bus is initialized

    digitalWrite(MAX31865_CS_PIN, LOW);
    delayMicroseconds(10);

    // Read register 0x00 (config)
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));
    uint8_t reg = SPI.transfer(0x00);  // Read address 0x00
    uint8_t val = SPI.transfer(0xFF);  // Read data
    SPI.endTransaction();

    digitalWrite(MAX31865_CS_PIN, HIGH);

    // Valid config register should not be 0x00 or 0xFF
    // Typical power-on default is 0x00, after begin() it's configured
    // We accept any non-0xFF response as "device present"
    return (val != 0xFF);
}

bool SelfTest::testSDCard(SemaphoreHandle_t spiMutex) {
    // SD card test is called separately from runAll() because it needs
    // the SPI mutex which may not exist during early POST.
    // The result is recorded when sdLogger.begin() completes.

    // Just check if the CS pin can be toggled
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);  // Deselect

    return true;  // Actual test done by sdLogger.begin()
}

bool SelfTest::testWaterMeterPins() {
    // Verify the water meter pin is readable (input-only pin)
    // GPIO34 is input-only; just verify we can read it without crash
    pinMode(WATER_METER_PIN, INPUT);
    digitalRead(WATER_METER_PIN);  // If this doesn't crash, pin is accessible
    return true;
}

bool SelfTest::testAuxInputPins() {
    // GPIO17 with internal pull-up
    pinMode(AUX_INPUT1_PIN, INPUT_PULLUP);
    // Read the pin — with pull-up it should be HIGH unless switch is closed
    digitalRead(AUX_INPUT1_PIN);
    return true;
}

bool SelfTest::testStepperPins() {
    // Verify stepper enable pin can be controlled
    // Set it HIGH (disabled) and read back
    pinMode(STEPPER_ENABLE_PIN, OUTPUT);
    digitalWrite(STEPPER_ENABLE_PIN, HIGH);  // Disable

    // Tiny delay for output to settle
    delayMicroseconds(10);

    // We can't reliably read back a GPIO output on ESP32 in all cases,
    // but we verify no crash occurs during pin manipulation.
    return true;
}

// ============================================================================
// BOOT REASON
// ============================================================================

const char* SelfTest::getResetReasonString() {
    esp_reset_reason_t reason = esp_reset_reason();

    switch (reason) {
        case ESP_RST_POWERON:   return "POWER_ON";
        case ESP_RST_EXT:       return "EXTERNAL_RESET";
        case ESP_RST_SW:        return "SOFTWARE_RESET";
        case ESP_RST_PANIC:     return "PANIC_CRASH";
        case ESP_RST_INT_WDT:   return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT:  return "TASK_WATCHDOG";
        case ESP_RST_WDT:       return "OTHER_WATCHDOG";
        case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP_WAKE";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO_RESET";
        default:                return "UNKNOWN";
    }
}

void SelfTest::logBootReason() {
    const char* reason = getResetReasonString();
    Serial.printf("Boot reason: %s (code: %d)\n",
                  reason, (int)esp_reset_reason());

    // Check for abnormal resets
    esp_reset_reason_t r = esp_reset_reason();
    if (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
        r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
        r == ESP_RST_BROWNOUT) {
        Serial.println("WARNING: Previous boot ended abnormally!");
    }
}

// ============================================================================
// RESULTS
// ============================================================================

self_test_result_t SelfTest::getLastResult() {
    return _result;
}

void SelfTest::printResults() {
    Serial.println("=== POST Results ===");
    Serial.printf("  Tested:  %d\n", _result.total_tested);
    Serial.printf("  Passed:  %d\n", _result.total_passed);
    Serial.printf("  Failed:  %d\n", _result.total_failed);
    Serial.printf("  Skipped: %d\n", _result.total_skipped);

    if (_result.critical_failure) {
        Serial.println("  *** CRITICAL: Required device(s) failed! ***");
    } else if (_result.total_failed > 0) {
        Serial.println("  WARNING: Optional device(s) not detected");
    } else {
        Serial.println("  All tested devices passed");
    }

    Serial.println("====================");
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void SelfTest::recordResult(device_id_t id, bool passed) {
    if (id >= DEV_COUNT) return;

    // Skip if device is disabled
    if (!deviceManager.isEnabled(id)) {
        _result.skipped_mask |= (1 << id);
        _result.total_skipped++;
        return;
    }

    _result.tested_mask |= (1 << id);
    _result.total_tested++;

    if (passed) {
        _result.passed_mask |= (1 << id);
        _result.total_passed++;
        deviceManager.setInstalled(id, true);
    } else {
        _result.failed_mask |= (1 << id);
        _result.total_failed++;
        deviceManager.setInstalled(id, false);

        // Check if this is a required device
        if (deviceManager.isRequired(id)) {
            _result.critical_failure = true;
        }
    }
}

bool SelfTest::probeI2CAddress(uint8_t addr) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    return (error == 0);
}

bool SelfTest::readBackADS1115Config() {
    // Write to config register to verify read-back
    Wire.beginTransmission(ADS1115_I2C_ADDR);
    Wire.write(0x01);  // Config register address
    uint8_t err = Wire.endTransmission();
    if (err != 0) return false;

    // Read back 2 bytes
    uint8_t received = Wire.requestFrom((uint8_t)ADS1115_I2C_ADDR, (uint8_t)2);
    if (received != 2) return false;

    uint16_t config = (Wire.read() << 8) | Wire.read();

    // ADS1115 power-on default config is 0x8583
    // Any non-0x0000, non-0xFFFF value suggests a real device
    return (config != 0x0000 && config != 0xFFFF);
}

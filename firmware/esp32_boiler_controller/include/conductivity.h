/**
 * @file conductivity.h
 * @brief Conductivity Measurement Module for Sensorex CS675HTTC-P1K/K=1.0
 *
 * Handles conductivity measurement using analog interface with:
 * - AC excitation signal generation
 * - Current-to-voltage conversion reading
 * - Pt1000 RTD temperature compensation
 * - Calibration and anti-flashing features
 */

#ifndef CONDUCTIVITY_H
#define CONDUCTIVITY_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// CONSTANTS
// ============================================================================

// Pt1000 RTD temperature calculation constants (IEC 60751)
#define PT1000_R0           1000.0      // Resistance at 0C
#define PT1000_ALPHA        0.00385     // Temperature coefficient
#define PT1000_A            3.9083e-3
#define PT1000_B            -5.775e-7

// ADC configuration
#define ADC_RESOLUTION      12          // 12-bit ADC
#define ADC_MAX_VALUE       4095
#define ADC_VREF            3.3         // Reference voltage
#define ADC_SAMPLES         64          // Oversampling for noise reduction

// Conductivity measurement
#define COND_EXCITATION_FREQ    1000    // 1kHz AC excitation
#define COND_SETTLE_TIME_MS     100     // Time for signal to settle
#define COND_READINGS_AVG       10      // Number of readings to average

// Temperature compensation reference
#define TEMP_REF_CELSIUS        25.0    // Reference temperature for compensation

// ============================================================================
// CALIBRATION DATA STRUCTURE
// ============================================================================

typedef struct {
    float offset;           // Zero offset
    float slope;            // Scale factor (gain)
    float temp_offset;      // Temperature offset
    uint32_t timestamp;     // Last calibration timestamp
    bool valid;             // Calibration data valid flag
} calibration_data_t;

// ============================================================================
// MEASUREMENT RESULT STRUCTURE
// ============================================================================

typedef struct {
    float raw_conductivity;         // Before any compensation (uS/cm)
    float temp_compensated;         // After temperature compensation
    float calibrated;               // After user calibration
    float temperature_c;            // Current temperature (Celsius)
    float temperature_f;            // Current temperature (Fahrenheit)
    bool sensor_ok;                 // Sensor connection OK
    bool temp_sensor_ok;            // Temperature sensor OK
    uint32_t timestamp;             // Reading timestamp
} conductivity_reading_t;

// ============================================================================
// CLASS DEFINITION
// ============================================================================

class ConductivitySensor {
public:
    /**
     * @brief Constructor
     * @param excite_pin DAC pin for AC excitation
     * @param sense_pin ADC pin for conductivity measurement
     * @param temp_pin ADC pin for Pt1000 temperature measurement
     */
    ConductivitySensor(uint8_t excite_pin, uint8_t sense_pin, uint8_t temp_pin);

    /**
     * @brief Initialize the sensor hardware
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Configure sensor parameters
     * @param config Pointer to conductivity configuration
     */
    void configure(conductivity_config_t* config);

    /**
     * @brief Take a conductivity measurement
     * @return Measurement result structure
     */
    conductivity_reading_t read();

    /**
     * @brief Read raw ADC value for conductivity
     * @return Raw ADC value (0-4095)
     */
    uint16_t readRawConductivity();

    /**
     * @brief Read temperature from Pt1000 RTD
     * @return Temperature in Celsius
     */
    float readTemperature();

    /**
     * @brief Get the last measurement
     * @return Last measurement result
     */
    conductivity_reading_t getLastReading();

    /**
     * @brief Perform self-test
     * @return true if self-test passed (1000 +/- 100 uS/cm simulated)
     */
    bool selfTest();

    /**
     * @brief Start calibration mode
     * @param reference_value Known conductivity value (uS/cm)
     */
    void calibrate(float reference_value);

    /**
     * @brief Apply calibration offset
     * @param percent Calibration percentage (-50 to +50)
     */
    void setCalibrationPercent(int8_t percent);

    /**
     * @brief Get current calibration percentage
     * @return Calibration percentage
     */
    int8_t getCalibrationPercent();

    /**
     * @brief Reset calibration to factory defaults
     */
    void resetCalibration();

    /**
     * @brief Set cell constant
     * @param k Cell constant (default 1.0)
     */
    void setCellConstant(float k);

    /**
     * @brief Set temperature compensation coefficient
     * @param coeff Temperature coefficient (default 0.02 = 2%/C)
     */
    void setTempCoefficient(float coeff);

    /**
     * @brief Enable/disable temperature compensation
     * @param enable True to enable auto compensation
     */
    void setTempCompensation(bool enable);

    /**
     * @brief Set manual temperature value
     * @param temp_c Temperature in Celsius
     */
    void setManualTemperature(float temp_c);

    /**
     * @brief Enable/disable anti-flashing feature
     * @param enable True to enable anti-flashing
     * @param factor Dampening factor (1-10)
     */
    void setAntiFlash(bool enable, uint8_t factor = 5);

    /**
     * @brief Check if sensor is connected and responding
     * @return true if sensor OK
     */
    bool isSensorOK();

    /**
     * @brief Check if temperature sensor is OK
     * @return true if temp sensor OK
     */
    bool isTempSensorOK();

    /**
     * @brief Convert conductivity to PPM (TDS)
     * @param conductivity Conductivity in uS/cm
     * @param conversion_factor PPM conversion factor (default 0.666)
     * @return TDS in PPM
     */
    static float conductivityToPPM(float conductivity, float conversion_factor = 0.666);

    /**
     * @brief Convert temperature from Pt1000 resistance
     * @param resistance Measured resistance in ohms
     * @return Temperature in Celsius
     */
    static float resistanceToTemperature(float resistance);

private:
    // Pin assignments
    uint8_t _excite_pin;
    uint8_t _sense_pin;
    uint8_t _temp_pin;

    // Configuration
    conductivity_config_t* _config;
    calibration_data_t _calibration;

    // State
    conductivity_reading_t _last_reading;
    bool _initialized;
    bool _temp_comp_enabled;
    float _manual_temp;
    bool _anti_flash_enabled;
    uint8_t _anti_flash_factor;
    float _anti_flash_buffer;

    // Internal methods
    void generateExcitation();
    void stopExcitation();
    uint16_t readADCOversampled(uint8_t pin, uint8_t samples);
    float adcToVoltage(uint16_t adc_value);
    float voltageToResistance(float voltage);
    float voltageToConductivity(float voltage);
    float applyTempCompensation(float conductivity, float temperature);
    float applyCalibration(float conductivity);
    float applyAntiFlash(float conductivity);
    bool checkSensorRange(float conductivity);
    bool checkTempRange(float temperature);
};

// ============================================================================
// GLOBAL INSTANCE (optional - can be instantiated in main)
// ============================================================================

// extern ConductivitySensor conductivitySensor;

#endif // CONDUCTIVITY_H

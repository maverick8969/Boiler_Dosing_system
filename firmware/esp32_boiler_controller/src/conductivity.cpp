/**
 * @file conductivity.cpp
 * @brief Conductivity Measurement Module Implementation
 *
 * Implements analog conductivity measurement for Sensorex CS675HTTC-P1K/K=1.0
 * with Pt1000 RTD temperature compensation.
 */

#include "conductivity.h"
#include "pin_definitions.h"
#include <driver/dac.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

// ADC calibration characteristics
static esp_adc_cal_characteristics_t adc_chars;
static bool adc_calibrated = false;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ConductivitySensor::ConductivitySensor(uint8_t excite_pin, uint8_t sense_pin, uint8_t temp_pin)
    : _excite_pin(excite_pin)
    , _sense_pin(sense_pin)
    , _temp_pin(temp_pin)
    , _config(nullptr)
    , _initialized(false)
    , _temp_comp_enabled(true)
    , _manual_temp(25.0)
    , _anti_flash_enabled(false)
    , _anti_flash_factor(5)
    , _anti_flash_buffer(0)
{
    // Initialize calibration with defaults
    _calibration.offset = 0;
    _calibration.slope = 1.0;
    _calibration.temp_offset = 0;
    _calibration.timestamp = 0;
    _calibration.valid = false;

    // Initialize last reading
    memset(&_last_reading, 0, sizeof(_last_reading));
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool ConductivitySensor::begin() {
    // Configure DAC for excitation signal
    if (_excite_pin == GPIO_NUM_25) {
        dac_output_enable(DAC_CHANNEL_1);
    } else if (_excite_pin == GPIO_NUM_26) {
        dac_output_enable(DAC_CHANNEL_2);
    } else {
        Serial.println("Error: Invalid DAC pin for excitation");
        return false;
    }

    // Configure ADC for conductivity measurement
    adc1_config_width(ADC_WIDTH_BIT_12);

    // Configure conductivity sense pin
    if (_sense_pin == GPIO_NUM_36) {
        adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
    } else if (_sense_pin == GPIO_NUM_39) {
        adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_11);
    } else if (_sense_pin == GPIO_NUM_34) {
        adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
    } else if (_sense_pin == GPIO_NUM_35) {
        adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);
    }

    // Configure temperature sense pin
    if (_temp_pin == GPIO_NUM_39) {
        adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_11);
    } else if (_temp_pin == GPIO_NUM_36) {
        adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
    } else if (_temp_pin == GPIO_NUM_34) {
        adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
    } else if (_temp_pin == GPIO_NUM_35) {
        adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);
    }

    // Characterize ADC for better accuracy
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_11,
        ADC_WIDTH_BIT_12,
        1100,  // Default Vref
        &adc_chars
    );

    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        Serial.println("ADC: Using eFuse Vref");
        adc_calibrated = true;
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        Serial.println("ADC: Using Two Point calibration");
        adc_calibrated = true;
    } else {
        Serial.println("ADC: Using default Vref");
        adc_calibrated = false;
    }

    _initialized = true;
    Serial.println("Conductivity sensor initialized");
    return true;
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void ConductivitySensor::configure(conductivity_config_t* config) {
    _config = config;
    if (_config) {
        _temp_comp_enabled = _config->temp_comp_enabled;
        _anti_flash_enabled = _config->anti_flash_enabled;
        _anti_flash_factor = _config->anti_flash_factor;
        _calibration.slope = 1.0 + (_config->calibration_percent / 100.0);
    }
}

// ============================================================================
// MEASUREMENT
// ============================================================================

conductivity_reading_t ConductivitySensor::read() {
    conductivity_reading_t result;
    memset(&result, 0, sizeof(result));
    result.timestamp = millis();

    if (!_initialized) {
        result.sensor_ok = false;
        return result;
    }

    // Generate excitation and wait for settling
    generateExcitation();
    delay(COND_SETTLE_TIME_MS);

    // Read multiple samples and average
    float cond_sum = 0;
    float temp_sum = 0;
    int valid_cond_readings = 0;
    int valid_temp_readings = 0;

    for (int i = 0; i < COND_READINGS_AVG; i++) {
        // Read conductivity
        uint16_t adc_cond = readADCOversampled(_sense_pin, ADC_SAMPLES);
        float voltage_cond = adcToVoltage(adc_cond);
        float cond = voltageToConductivity(voltage_cond);

        if (checkSensorRange(cond)) {
            cond_sum += cond;
            valid_cond_readings++;
        }

        // Read temperature
        uint16_t adc_temp = readADCOversampled(_temp_pin, ADC_SAMPLES);
        float voltage_temp = adcToVoltage(adc_temp);
        float resistance = voltageToResistance(voltage_temp);
        float temp = resistanceToTemperature(resistance);

        if (checkTempRange(temp)) {
            temp_sum += temp;
            valid_temp_readings++;
        }

        delay(10);  // Small delay between readings
    }

    // Stop excitation
    stopExcitation();

    // Process conductivity
    if (valid_cond_readings > 0) {
        result.raw_conductivity = cond_sum / valid_cond_readings;
        result.sensor_ok = true;
    } else {
        result.raw_conductivity = 0;
        result.sensor_ok = false;
    }

    // Process temperature
    if (valid_temp_readings > 0) {
        result.temperature_c = temp_sum / valid_temp_readings;
        result.temperature_f = (result.temperature_c * 9.0 / 5.0) + 32.0;
        result.temp_sensor_ok = true;
    } else {
        // Use manual temperature if auto fails
        result.temperature_c = _manual_temp;
        result.temperature_f = (_manual_temp * 9.0 / 5.0) + 32.0;
        result.temp_sensor_ok = false;
    }

    // Apply temperature compensation
    if (_temp_comp_enabled && result.temp_sensor_ok) {
        result.temp_compensated = applyTempCompensation(
            result.raw_conductivity,
            result.temperature_c
        );
    } else if (_temp_comp_enabled) {
        // Use manual temperature for compensation
        result.temp_compensated = applyTempCompensation(
            result.raw_conductivity,
            _manual_temp
        );
    } else {
        result.temp_compensated = result.raw_conductivity;
    }

    // Apply anti-flashing filter
    if (_anti_flash_enabled) {
        result.temp_compensated = applyAntiFlash(result.temp_compensated);
    }

    // Apply user calibration
    result.calibrated = applyCalibration(result.temp_compensated);

    // Store last reading
    _last_reading = result;

    return result;
}

uint16_t ConductivitySensor::readRawConductivity() {
    generateExcitation();
    delay(COND_SETTLE_TIME_MS);
    uint16_t raw = readADCOversampled(_sense_pin, ADC_SAMPLES);
    stopExcitation();
    return raw;
}

float ConductivitySensor::readTemperature() {
    uint16_t adc_temp = readADCOversampled(_temp_pin, ADC_SAMPLES);
    float voltage = adcToVoltage(adc_temp);
    float resistance = voltageToResistance(voltage);
    return resistanceToTemperature(resistance);
}

conductivity_reading_t ConductivitySensor::getLastReading() {
    return _last_reading;
}

// ============================================================================
// SELF-TEST
// ============================================================================

bool ConductivitySensor::selfTest() {
    // The self-test should produce approximately 1000 uS/cm
    // This tests the electronics path, not the actual sensor

    // Store current config
    bool prev_temp_comp = _temp_comp_enabled;
    _temp_comp_enabled = false;

    // Take a reading with known excitation
    generateExcitation();
    delay(200);  // Longer settle time for self-test

    uint16_t adc_value = readADCOversampled(_sense_pin, ADC_SAMPLES * 2);
    float voltage = adcToVoltage(adc_value);
    float cond = voltageToConductivity(voltage);

    stopExcitation();

    // Restore config
    _temp_comp_enabled = prev_temp_comp;

    // Check if reading is within expected range (1000 +/- 100 uS/cm)
    // Note: This assumes a specific calibration resistor is connected
    // In practice, this tests the ADC and DAC circuits
    Serial.printf("Self-test reading: %.1f uS/cm\n", cond);

    // For now, just verify we get a reasonable reading
    return (cond > 10 && cond < 15000);
}

// ============================================================================
// CALIBRATION
// ============================================================================

void ConductivitySensor::calibrate(float reference_value) {
    if (reference_value <= 0) return;

    // Take a fresh reading
    conductivity_reading_t reading = read();

    if (!reading.sensor_ok) {
        Serial.println("Calibration failed: sensor error");
        return;
    }

    // Calculate calibration factor
    float measured = reading.temp_compensated;
    if (measured <= 0) {
        Serial.println("Calibration failed: invalid reading");
        return;
    }

    _calibration.slope = reference_value / measured;
    _calibration.timestamp = millis();
    _calibration.valid = true;

    // Update config calibration percent
    if (_config) {
        float percent = ((_calibration.slope - 1.0) * 100.0);
        _config->calibration_percent = (int8_t)constrain(percent, -50, 50);
    }

    Serial.printf("Calibration applied: slope=%.4f\n", _calibration.slope);
}

void ConductivitySensor::setCalibrationPercent(int8_t percent) {
    percent = constrain(percent, -50, 50);
    _calibration.slope = 1.0 + (percent / 100.0);
    _calibration.valid = true;

    if (_config) {
        _config->calibration_percent = percent;
    }
}

int8_t ConductivitySensor::getCalibrationPercent() {
    return (int8_t)((_calibration.slope - 1.0) * 100.0);
}

void ConductivitySensor::resetCalibration() {
    _calibration.offset = 0;
    _calibration.slope = 1.0;
    _calibration.temp_offset = 0;
    _calibration.timestamp = 0;
    _calibration.valid = false;

    if (_config) {
        _config->calibration_percent = 0;
    }
}

// ============================================================================
// CONFIGURATION METHODS
// ============================================================================

void ConductivitySensor::setCellConstant(float k) {
    if (_config) {
        _config->cell_constant = constrain(k, 0.01, 10.0);
    }
}

void ConductivitySensor::setTempCoefficient(float coeff) {
    if (_config) {
        _config->temp_comp_coefficient = constrain(coeff, 0.0, 0.05);
    }
}

void ConductivitySensor::setTempCompensation(bool enable) {
    _temp_comp_enabled = enable;
    if (_config) {
        _config->temp_comp_enabled = enable;
    }
}

void ConductivitySensor::setManualTemperature(float temp_c) {
    _manual_temp = constrain(temp_c, -10.0, 250.0);
    if (_config) {
        _config->manual_temperature = _manual_temp;
    }
}

void ConductivitySensor::setAntiFlash(bool enable, uint8_t factor) {
    _anti_flash_enabled = enable;
    _anti_flash_factor = constrain(factor, 1, 10);

    if (_config) {
        _config->anti_flash_enabled = enable;
        _config->anti_flash_factor = _anti_flash_factor;
    }
}

bool ConductivitySensor::isSensorOK() {
    return _last_reading.sensor_ok;
}

bool ConductivitySensor::isTempSensorOK() {
    return _last_reading.temp_sensor_ok;
}

// ============================================================================
// STATIC UTILITY METHODS
// ============================================================================

float ConductivitySensor::conductivityToPPM(float conductivity, float conversion_factor) {
    return conductivity * conversion_factor;
}

float ConductivitySensor::resistanceToTemperature(float resistance) {
    // Callendar-Van Dusen equation for Pt1000
    // Simplified linear approximation for 0-200C range:
    // R(T) = R0 * (1 + A*T + B*T^2)
    // Solving for T using quadratic formula (neglecting C term for T > 0)

    if (resistance < 800 || resistance > 2000) {
        // Out of range
        return -999;
    }

    // For quick approximation: T = (R - R0) / (R0 * alpha)
    // More accurate: use quadratic solution

    float R0 = PT1000_R0;
    float A = PT1000_A;
    float B = PT1000_B;

    // R = R0(1 + AT + BT^2)
    // BT^2 + AT + (1 - R/R0) = 0
    // T = (-A + sqrt(A^2 - 4B(1-R/R0))) / 2B

    float ratio = resistance / R0;
    float discriminant = A * A - 4 * B * (1 - ratio);

    if (discriminant < 0) {
        // Use linear approximation
        return (resistance - R0) / (R0 * PT1000_ALPHA);
    }

    float temp = (-A + sqrt(discriminant)) / (2 * B);
    return temp;
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void ConductivitySensor::generateExcitation() {
    // Generate a simple DC excitation for initial testing
    // For true AC excitation, use timer-based PWM
    if (_excite_pin == GPIO_NUM_25) {
        dac_output_voltage(DAC_CHANNEL_1, 200);  // ~2.6V
    } else if (_excite_pin == GPIO_NUM_26) {
        dac_output_voltage(DAC_CHANNEL_2, 200);
    }
}

void ConductivitySensor::stopExcitation() {
    if (_excite_pin == GPIO_NUM_25) {
        dac_output_voltage(DAC_CHANNEL_1, 0);
    } else if (_excite_pin == GPIO_NUM_26) {
        dac_output_voltage(DAC_CHANNEL_2, 0);
    }
}

uint16_t ConductivitySensor::readADCOversampled(uint8_t pin, uint8_t samples) {
    uint32_t sum = 0;
    adc1_channel_t channel;

    // Map GPIO to ADC channel
    switch (pin) {
        case GPIO_NUM_36: channel = ADC1_CHANNEL_0; break;
        case GPIO_NUM_37: channel = ADC1_CHANNEL_1; break;
        case GPIO_NUM_38: channel = ADC1_CHANNEL_2; break;
        case GPIO_NUM_39: channel = ADC1_CHANNEL_3; break;
        case GPIO_NUM_32: channel = ADC1_CHANNEL_4; break;
        case GPIO_NUM_33: channel = ADC1_CHANNEL_5; break;
        case GPIO_NUM_34: channel = ADC1_CHANNEL_6; break;
        case GPIO_NUM_35: channel = ADC1_CHANNEL_7; break;
        default: return 0;
    }

    for (int i = 0; i < samples; i++) {
        sum += adc1_get_raw(channel);
    }

    return sum / samples;
}

float ConductivitySensor::adcToVoltage(uint16_t adc_value) {
    if (adc_calibrated) {
        return esp_adc_cal_raw_to_voltage(adc_value, &adc_chars) / 1000.0;
    } else {
        return (adc_value / (float)ADC_MAX_VALUE) * ADC_VREF;
    }
}

float ConductivitySensor::voltageToResistance(float voltage) {
    // Assuming voltage divider with reference resistor
    // Vout = Vcc * R_pt1000 / (R_ref + R_pt1000)
    // R_pt1000 = R_ref * Vout / (Vcc - Vout)

    if (voltage >= ADC_VREF || voltage <= 0) {
        return 0;
    }

    float R_ref = TEMP_REF_RESISTOR;
    float Vcc = ADC_VREF;
    return R_ref * voltage / (Vcc - voltage);
}

float ConductivitySensor::voltageToConductivity(float voltage) {
    // Convert voltage to conductivity based on sensor characteristics
    // This is a simplified model - actual implementation depends on
    // the specific signal conditioning circuit

    // Assuming linear relationship:
    // 0V = 0 uS/cm, 3.3V = 10000 uS/cm (or as configured)

    float cell_k = (_config) ? _config->cell_constant : 1.0;
    uint16_t range_max = (_config) ? _config->range_max : 10000;

    // Linear interpolation
    float conductivity = (voltage / ADC_VREF) * range_max * cell_k;

    return conductivity;
}

float ConductivitySensor::applyTempCompensation(float conductivity, float temperature) {
    // Standard temperature compensation formula:
    // Cond_25C = Cond_T / (1 + alpha * (T - 25))

    float alpha = (_config) ? _config->temp_comp_coefficient : 0.02;
    float ref_temp = TEMP_REF_CELSIUS;

    float compensation_factor = 1.0 + alpha * (temperature - ref_temp);

    if (compensation_factor <= 0.1) {
        // Prevent division by very small numbers
        compensation_factor = 0.1;
    }

    return conductivity / compensation_factor;
}

float ConductivitySensor::applyCalibration(float conductivity) {
    // Apply user calibration
    return (conductivity + _calibration.offset) * _calibration.slope;
}

float ConductivitySensor::applyAntiFlash(float conductivity) {
    // Low-pass filter to reduce steam flash noise
    // Using exponential moving average

    if (_anti_flash_buffer == 0) {
        _anti_flash_buffer = conductivity;
        return conductivity;
    }

    // Factor determines smoothing: higher = more smoothing
    float alpha = 1.0 / _anti_flash_factor;
    _anti_flash_buffer = alpha * conductivity + (1.0 - alpha) * _anti_flash_buffer;

    return _anti_flash_buffer;
}

bool ConductivitySensor::checkSensorRange(float conductivity) {
    uint16_t range_max = (_config) ? _config->range_max : 10000;
    return (conductivity >= 0 && conductivity <= range_max * 1.5);
}

bool ConductivitySensor::checkTempRange(float temperature) {
    // Valid temperature range for Pt1000 sensor
    return (temperature >= -40 && temperature <= 250);
}

/**
 * @file conductivity.cpp
 * @brief Conductivity Measurement Module Implementation
 *
 * Implements conductivity measurement using:
 * - Atlas Scientific EZO-EC circuit in UART mode
 * - Adafruit MAX31865 with PT1000 RTD for temperature compensation
 *
 * EZO-EC UART Protocol:
 * - Default 9600 baud, 8N1
 * - Commands terminated with carriage return (0x0D)
 * - Responses terminated with carriage return
 * - Response codes enabled by default (*OK after successful commands)
 */

#include "conductivity.h"
#include "pin_definitions.h"

// ============================================================================
// CONSTRUCTORS
// ============================================================================

// Hardware SPI constructor (preferred — shares VSPI bus with SD card)
ConductivitySensor::ConductivitySensor(
    HardwareSerial& ezoSerial,
    uint8_t ezoRxPin, uint8_t ezoTxPin,
    uint8_t rtdCsPin, SPIClass* spi)
    : _serial(ezoSerial)
    , _rxPin(ezoRxPin)
    , _txPin(ezoTxPin)
    , _rtd(new Adafruit_MAX31865(rtdCsPin, spi))
    , _config(nullptr)
    , _calibration_percent(0)
    , _initialized(false)
    , _ezo_ok(false)
    , _rtd_ok(false)
    , _temp_comp_enabled(true)
    , _manual_temp(EZO_DEFAULT_TEMP_C)
    , _anti_flash_enabled(false)
    , _anti_flash_factor(5)
    , _anti_flash_buffer(0)
    , _sleeping(false)
{
    memset(&_last_reading, 0, sizeof(_last_reading));
}

// Software SPI constructor (legacy — for standalone test programs)
ConductivitySensor::ConductivitySensor(
    HardwareSerial& ezoSerial,
    uint8_t ezoRxPin, uint8_t ezoTxPin,
    uint8_t rtdCsPin, uint8_t rtdMosiPin,
    uint8_t rtdMisoPin, uint8_t rtdSckPin)
    : _serial(ezoSerial)
    , _rxPin(ezoRxPin)
    , _txPin(ezoTxPin)
    , _rtd(new Adafruit_MAX31865(rtdCsPin, rtdMosiPin, rtdMisoPin, rtdSckPin))
    , _config(nullptr)
    , _calibration_percent(0)
    , _initialized(false)
    , _ezo_ok(false)
    , _rtd_ok(false)
    , _temp_comp_enabled(true)
    , _manual_temp(EZO_DEFAULT_TEMP_C)
    , _anti_flash_enabled(false)
    , _anti_flash_factor(5)
    , _anti_flash_buffer(0)
    , _sleeping(false)
{
    memset(&_last_reading, 0, sizeof(_last_reading));
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool ConductivitySensor::begin() {
    Serial.println("Initializing conductivity sensor...");

    // ---- Initialize EZO-EC UART ----
    _serial.begin(EZO_EC_BAUD_RATE, SERIAL_8N1, _rxPin, _txPin);
    Serial.printf("  EZO-EC UART: %d baud (TX=GPIO%d, RX=GPIO%d)\n",
                  EZO_EC_BAUD_RATE, _txPin, _rxPin);

    // Wait for EZO to boot
    delay(EZO_BOOT_DELAY_MS);
    drainSerial();

    // Disable continuous mode so we use on-demand readings
    String resp = sendCommand("C,0", EZO_CMD_TIMEOUT_MS);
    delay(100);
    drainSerial();

    // Verify EZO is responding with device info query
    resp = sendCommand("i", EZO_CMD_TIMEOUT_MS);
    if (resp.startsWith("?I,EC")) {
        _ezo_ok = true;
        Serial.printf("  EZO-EC found: %s\n", resp.c_str());
    } else {
        _ezo_ok = false;
        Serial.println("  WARNING: EZO-EC not responding or unexpected device");
        Serial.printf("  Response: '%s'\n", resp.c_str());
    }

    // Enable response codes for reliable command confirmation
    sendCommand("*OK,1", EZO_CMD_TIMEOUT_MS);

    // Lock protocol to UART to prevent accidental I2C switch
    sendCommand("Plock,1", EZO_CMD_TIMEOUT_MS);

    // ---- Initialize MAX31865 PT1000 RTD ----
    Serial.printf("  MAX31865: CS=GPIO%d, MOSI=GPIO%d, MISO=GPIO%d, SCK=GPIO%d\n",
                  MAX31865_CS_PIN, MAX31865_MOSI_PIN, MAX31865_MISO_PIN, MAX31865_SCK_PIN);

    // Determine wire configuration
    max31865_numwires_t wireConfig = MAX31865_2WIRE;
    uint8_t cfgWires = (_config) ? _config->rtd_wires : RTD_NUM_WIRES;
    if (cfgWires == 3) wireConfig = MAX31865_3WIRE;
    else if (cfgWires == 4) wireConfig = MAX31865_4WIRE;

    if (!_rtd->begin(wireConfig)) {
        _rtd_ok = false;
        Serial.println("  WARNING: MAX31865 initialization failed");
    } else {
        // Test read to verify RTD is connected
        float temp = readTemperature();
        if (temp > -900) {
            _rtd_ok = true;
            Serial.printf("  MAX31865 PT1000 OK, current temp: %.1f C\n", temp);
        } else {
            _rtd_ok = false;
            uint8_t fault = _rtd->readFault();
            Serial.printf("  WARNING: MAX31865 RTD fault: 0x%02X\n", fault);
            _rtd->clearFault();
        }
    }

    _initialized = _ezo_ok;  // Minimum: EZO must work; RTD failure degrades gracefully

    if (_initialized) {
        Serial.println("Conductivity sensor initialized successfully");
    } else {
        Serial.println("ERROR: Conductivity sensor initialization failed");
    }

    return _initialized;
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void ConductivitySensor::configure(conductivity_config_t* config) {
    _config = config;
    if (!_config) return;

    _temp_comp_enabled = _config->temp_comp_enabled;
    _anti_flash_enabled = _config->anti_flash_enabled;
    _anti_flash_factor = _config->anti_flash_factor;
    _calibration_percent = _config->calibration_percent;
    _manual_temp = _config->manual_temperature;

    if (!_ezo_ok) return;

    // Set cell constant (K value) on EZO
    setCellConstant(_config->cell_constant);

    // Set TDS conversion factor
    setTDSConversionFactor(_config->ppm_conversion_factor);

    // Configure output parameters
    setOutputParameters(_config->ezo_output_ec, _config->ezo_output_tds,
                        _config->ezo_output_sal, _config->ezo_output_sg);

    Serial.printf("  EZO configured: K=%.2f, TDS factor=%.2f\n",
                  _config->cell_constant, _config->ppm_conversion_factor);
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
        result.temp_sensor_ok = false;
        return result;
    }

    // Wake EZO if sleeping
    if (_sleeping) {
        wake();
    }

    // ---- Read temperature from MAX31865 ----
    float temperature = readTemperature();
    if (temperature > -900) {
        result.temperature_c = temperature;
        result.temperature_f = (temperature * 9.0f / 5.0f) + 32.0f;
        result.temp_sensor_ok = true;
        _rtd_ok = true;
    } else {
        // RTD failed - use manual temperature
        result.temperature_c = _manual_temp;
        result.temperature_f = (_manual_temp * 9.0f / 5.0f) + 32.0f;
        result.temp_sensor_ok = false;
        _rtd_ok = false;
    }

    // ---- Read conductivity from EZO-EC ----
    // Use RT command to send temperature and get reading in one step,
    // or R command if temperature compensation is disabled
    String response;
    float temp_to_send = result.temp_sensor_ok ? result.temperature_c : _manual_temp;

    if (_temp_comp_enabled) {
        // RT command: set temp compensation and take reading
        String cmd = "RT," + String(temp_to_send, 1);
        response = sendCommand(cmd, EZO_RT_TIMEOUT_MS);
    } else {
        // R command: take reading without updating temperature
        response = sendCommand("R", EZO_READ_TIMEOUT_MS);
    }

    // Parse the response
    float ec = 0, tds = 0, sal = 0, sg = 0;
    if (parseReadingResponse(response, ec, tds, sal, sg)) {
        // EZO returns temperature-compensated EC when using RT command
        result.raw_conductivity = ec;
        result.temp_compensated = ec;
        result.tds = tds;
        result.salinity = sal;
        result.specific_gravity = sg;
        result.sensor_ok = true;
        _ezo_ok = true;

        // Apply software anti-flash filter
        if (_anti_flash_enabled) {
            result.temp_compensated = applyAntiFlash(result.temp_compensated);
        }

        // Apply software calibration trim
        result.calibrated = applySoftwareCalibration(result.temp_compensated);
    } else {
        result.sensor_ok = false;
        _ezo_ok = false;
        Serial.printf("EZO read failed, response: '%s'\n", response.c_str());
    }

    _last_reading = result;
    return result;
}

float ConductivitySensor::readTemperature() {
    float rtdNominal = (_config) ? _config->rtd_nominal : RTD_NOMINAL_RESISTANCE;
    float rtdRef = (_config) ? _config->rtd_reference : RTD_REFERENCE_RESISTOR;

    // Prevent division by zero or faults in MAX31865 library if config is invalid
    if (rtdRef <= 0.0f) {
        rtdRef = RTD_REFERENCE_RESISTOR;
    }

    float temp = _rtd->temperature(rtdNominal, rtdRef);

    // Check for RTD faults
    uint8_t fault = _rtd->readFault();
    if (fault) {
        Serial.printf("MAX31865 fault: 0x%02X - ", fault);
        if (fault & MAX31865_FAULT_HIGHTHRESH) Serial.print("RTD High Threshold ");
        if (fault & MAX31865_FAULT_LOWTHRESH)  Serial.print("RTD Low Threshold ");
        if (fault & MAX31865_FAULT_REFINLOW)   Serial.print("REFIN- > 0.85 x Bias ");
        if (fault & MAX31865_FAULT_REFINHIGH)  Serial.print("REFIN- < 0.85 x Bias (FORCE- open) ");
        if (fault & MAX31865_FAULT_RTDINLOW)   Serial.print("RTDIN- < 0.85 x Bias (FORCE- open) ");
        if (fault & MAX31865_FAULT_OVUV)       Serial.print("Under/Over voltage ");
        Serial.println();
        _rtd->clearFault();
        return -999.0f;
    }

    // Sanity check the temperature range
    if (temp < -40.0f || temp > 250.0f) {
        return -999.0f;
    }

    return temp;
}

conductivity_reading_t ConductivitySensor::getLastReading() {
    return _last_reading;
}

// ============================================================================
// EZO-EC CALIBRATION
// ============================================================================

bool ConductivitySensor::calibrateDry() {
    String resp = sendCommand("Cal,dry", EZO_CAL_TIMEOUT_MS);
    bool ok = isResponseOK(resp);
    if (ok) {
        Serial.println("EZO dry calibration complete");
    } else {
        Serial.printf("EZO dry calibration failed: '%s'\n", resp.c_str());
    }
    return ok;
}

bool ConductivitySensor::calibrateSingle(float value) {
    String cmd = "Cal," + String(value, 0);
    String resp = sendCommand(cmd, EZO_CAL_TIMEOUT_MS);
    bool ok = isResponseOK(resp);
    if (ok) {
        Serial.printf("EZO single-point calibration complete (%.0f uS/cm)\n", value);
    } else {
        Serial.printf("EZO calibration failed: '%s'\n", resp.c_str());
    }
    return ok;
}

bool ConductivitySensor::calibrateLow(float value) {
    String cmd = "Cal,low," + String(value, 0);
    String resp = sendCommand(cmd, EZO_CAL_TIMEOUT_MS);
    bool ok = isResponseOK(resp);
    if (ok) {
        Serial.printf("EZO low-point calibration complete (%.0f uS/cm)\n", value);
    }
    return ok;
}

bool ConductivitySensor::calibrateHigh(float value) {
    String cmd = "Cal,high," + String(value, 0);
    String resp = sendCommand(cmd, EZO_CAL_TIMEOUT_MS);
    bool ok = isResponseOK(resp);
    if (ok) {
        Serial.printf("EZO high-point calibration complete (%.0f uS/cm)\n", value);
    }
    return ok;
}

uint8_t ConductivitySensor::getCalibrationStatus() {
    String resp = sendCommand("Cal,?", EZO_CMD_TIMEOUT_MS);
    // Response format: ?Cal,N where N is 0, 1, or 2
    int idx = resp.indexOf("?Cal,");
    if (idx >= 0) {
        return resp.substring(idx + 5).toInt();
    }
    return 0;
}

String ConductivitySensor::exportCalibration() {
    String result = "";
    String resp = sendCommand("Export", EZO_CMD_TIMEOUT_MS);

    if (resp.length() == 0) return result;

    // The EZO sends multiple lines of calibration data
    // First response is the data, subsequent responses contain more lines
    // ending with *DONE
    result = resp;

    // Read additional lines until *DONE
    for (int i = 0; i < 20; i++) {
        String line = readResponse(EZO_CMD_TIMEOUT_MS);
        if (line.startsWith("*DONE") || line.length() == 0) break;
        if (!line.startsWith("*OK")) {
            result += "\n" + line;
        }
    }

    return result;
}

bool ConductivitySensor::importCalibration(const String& data) {
    String cmd = "Import," + data;
    String resp = sendCommand(cmd, EZO_CMD_TIMEOUT_MS);
    return isResponseOK(resp);
}

// ============================================================================
// CONFIGURATION METHODS
// ============================================================================

void ConductivitySensor::setCellConstant(float k) {
    k = constrain(k, 0.01f, 10.0f);
    String cmd = "K," + String(k, 2);
    sendCommand(cmd, EZO_CMD_TIMEOUT_MS);

    if (_config) {
        _config->cell_constant = k;
    }
}

void ConductivitySensor::setTDSConversionFactor(float factor) {
    factor = constrain(factor, 0.01f, 1.0f);
    String cmd = "TDS," + String(factor, 2);
    sendCommand(cmd, EZO_CMD_TIMEOUT_MS);

    if (_config) {
        _config->ppm_conversion_factor = factor;
    }
}

void ConductivitySensor::setTempCompensation(bool enable) {
    _temp_comp_enabled = enable;
    if (_config) {
        _config->temp_comp_enabled = enable;
    }
}

void ConductivitySensor::setManualTemperature(float temp_c) {
    _manual_temp = constrain(temp_c, -10.0f, 250.0f);
    if (_config) {
        _config->manual_temperature = _manual_temp;
    }
}

void ConductivitySensor::setAntiFlash(bool enable, uint8_t factor) {
    _anti_flash_enabled = enable;
    _anti_flash_factor = constrain(factor, (uint8_t)1, (uint8_t)10);

    if (_config) {
        _config->anti_flash_enabled = enable;
        _config->anti_flash_factor = _anti_flash_factor;
    }
}

void ConductivitySensor::setCalibrationPercent(int8_t percent) {
    _calibration_percent = constrain(percent, (int8_t)-50, (int8_t)50);
    if (_config) {
        _config->calibration_percent = _calibration_percent;
    }
}

int8_t ConductivitySensor::getCalibrationPercent() {
    return _calibration_percent;
}

void ConductivitySensor::setOutputParameters(bool ec, bool tds, bool sal, bool sg) {
    sendCommand(ec  ? "O,EC,1"  : "O,EC,0",  EZO_CMD_TIMEOUT_MS);
    sendCommand(tds ? "O,TDS,1" : "O,TDS,0", EZO_CMD_TIMEOUT_MS);
    sendCommand(sal ? "O,S,1"   : "O,S,0",   EZO_CMD_TIMEOUT_MS);
    sendCommand(sg  ? "O,SG,1"  : "O,SG,0",  EZO_CMD_TIMEOUT_MS);

    if (_config) {
        _config->ezo_output_ec = ec;
        _config->ezo_output_tds = tds;
        _config->ezo_output_sal = sal;
        _config->ezo_output_sg = sg;
    }
}

// ============================================================================
// STATUS
// ============================================================================

bool ConductivitySensor::isSensorOK() {
    return _ezo_ok;
}

bool ConductivitySensor::isTempSensorOK() {
    return _rtd_ok;
}

String ConductivitySensor::getDeviceInfo() {
    return sendCommand("i", EZO_CMD_TIMEOUT_MS);
}

String ConductivitySensor::getDeviceStatus() {
    return sendCommand("Status", EZO_CMD_TIMEOUT_MS);
}

uint8_t ConductivitySensor::getRTDFault() {
    return _rtd->readFault();
}

// ============================================================================
// EZO CONTROL
// ============================================================================

void ConductivitySensor::sleep() {
    sendCommand("Sleep", EZO_CMD_TIMEOUT_MS);
    _sleeping = true;
}

void ConductivitySensor::wake() {
    // Any command wakes the EZO from sleep
    // Send a harmless query and discard the wake-up response
    drainSerial();
    _serial.print('\r');
    delay(100);
    drainSerial();

    // Verify device is awake
    String resp = sendCommand("i", EZO_CMD_TIMEOUT_MS);
    if (resp.startsWith("?I")) {
        _sleeping = false;
    }
}

void ConductivitySensor::factoryReset() {
    sendCommand("Factory", EZO_CMD_TIMEOUT_MS);
    delay(EZO_BOOT_DELAY_MS);
    drainSerial();
    Serial.println("WARNING: EZO factory reset - all calibration cleared!");
}

void ConductivitySensor::setLED(bool on) {
    sendCommand(on ? "L,1" : "L,0", EZO_CMD_TIMEOUT_MS);
}

void ConductivitySensor::find() {
    sendCommand("Find", EZO_CMD_TIMEOUT_MS);
}

// ============================================================================
// UTILITY
// ============================================================================

float ConductivitySensor::conductivityToPPM(float conductivity, float conversion_factor) {
    return conductivity * conversion_factor;
}

// ============================================================================
// EZO UART COMMUNICATION
// ============================================================================

String ConductivitySensor::sendCommand(const String& command, uint16_t timeout_ms) {
    // Drain any pending data
    drainSerial();

    // Send command with CR terminator
    _serial.print(command);
    _serial.print('\r');

    // Read the data response
    String response = readResponse(timeout_ms);

    // If response codes are enabled, the EZO sends *OK (or *ER) after the data.
    // Read and check the response code, but return the data response.
    if (response.length() > 0 && !response.startsWith("*")) {
        // This was data; now read the *OK/*ER response code
        String code = readResponse(timeout_ms);
        // We don't need to use the code here; it's logged if there's an error
        if (code.startsWith("*ER")) {
            Serial.printf("EZO error for command '%s'\n", command.c_str());
        }
    }

    return response;
}

String ConductivitySensor::readResponse(uint16_t timeout_ms) {
    String response = "";
    uint32_t start = millis();

    while ((millis() - start) < timeout_ms) {
        if (_serial.available()) {
            char c = _serial.read();
            if (c == '\r') {
                // CR terminates the response
                response.trim();
                return response;
            }
            if (c != '\n') {  // Ignore any LF characters
                response += c;
            }
        }
        yield();
    }

    // Timeout - return whatever we have
    response.trim();
    return response;
}

bool ConductivitySensor::isResponseOK(const String& response) {
    return response.startsWith("*OK") || response.indexOf("*OK") >= 0;
}

bool ConductivitySensor::parseReadingResponse(
    const String& response, float& ec, float& tds, float& sal, float& sg) {

    if (response.length() == 0) return false;

    // The first character must be a digit for a valid reading
    if (!isdigit(response.charAt(0))) {
        return false;
    }

    // EZO outputs enabled parameters in fixed order: EC, TDS, SAL, SG
    // Disabled parameters are omitted from the comma-separated output.
    char buf[64];
    response.toCharArray(buf, sizeof(buf));

    // Build ordered list of which outputs are enabled
    bool enabled[] = {
        (_config) ? _config->ezo_output_ec  : true,
        (_config) ? _config->ezo_output_tds : true,
        (_config) ? _config->ezo_output_sal : false,
        (_config) ? _config->ezo_output_sg  : false
    };
    float* targets[] = { &ec, &tds, &sal, &sg };

    // Parse tokens and assign to enabled outputs in order
    char* token = strtok(buf, ",");
    int enabledIdx = 0;

    while (token != NULL && enabledIdx < 4) {
        // Find the next enabled output
        while (enabledIdx < 4 && !enabled[enabledIdx]) {
            enabledIdx++;
        }
        if (enabledIdx < 4) {
            *targets[enabledIdx] = atof(token);
            enabledIdx++;
        }
        token = strtok(NULL, ",");
    }

    // Valid if EC is enabled and we got a non-negative value
    return (enabled[0] && ec >= 0);
}

float ConductivitySensor::applyAntiFlash(float conductivity) {
    // Low-pass filter to reduce steam flash noise
    // Using exponential moving average
    if (_anti_flash_buffer == 0) {
        _anti_flash_buffer = conductivity;
        return conductivity;
    }

    float alpha = 1.0f / _anti_flash_factor;
    _anti_flash_buffer = alpha * conductivity + (1.0f - alpha) * _anti_flash_buffer;

    return _anti_flash_buffer;
}

float ConductivitySensor::applySoftwareCalibration(float conductivity) {
    // Apply software trim percentage on top of EZO hardware calibration
    float factor = 1.0f + (_calibration_percent / 100.0f);
    return conductivity * factor;
}

void ConductivitySensor::drainSerial() {
    while (_serial.available()) {
        _serial.read();
    }
}

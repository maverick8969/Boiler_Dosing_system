/**
 * @file conductivity.h
 * @brief Conductivity Measurement Module for Sensorex CS675HTTC/P1K
 *
 * Hardware:
 * - Atlas Scientific EZO-EC circuit in UART mode for conductivity measurement
 * - Adafruit MAX31865 with PT1000 RTD for temperature compensation
 *
 * The EZO-EC handles conductivity measurement and internal temperature
 * compensation. Temperature from the MAX31865 PT1000 RTD is sent to the
 * EZO via the RT command for accurate compensation. The EZO returns
 * EC, TDS, Salinity, and Specific Gravity values.
 */

#ifndef CONDUCTIVITY_H
#define CONDUCTIVITY_H

#include <Arduino.h>
#include <Adafruit_MAX31865.h>
#include "config.h"

// ============================================================================
// EZO-EC TIMING CONSTANTS
// ============================================================================

#define EZO_READ_TIMEOUT_MS         1000    // Timeout for single reading (R command)
#define EZO_RT_TIMEOUT_MS           1500    // Timeout for RT command (temp comp + read)
#define EZO_CMD_TIMEOUT_MS          600     // Timeout for general commands
#define EZO_CAL_TIMEOUT_MS          1600    // Timeout for calibration commands
#define EZO_BOOT_DELAY_MS           2000    // Wait for EZO to boot after power-on

// EZO-EC default temperature (used when no RTD available)
#define EZO_DEFAULT_TEMP_C          25.0

// ============================================================================
// MEASUREMENT RESULT STRUCTURE
// ============================================================================

typedef struct {
    float raw_conductivity;         // EC from EZO before software trim (uS/cm)
    float temp_compensated;         // EC after EZO temperature compensation (uS/cm)
    float calibrated;               // After software calibration trim (uS/cm)
    float tds;                      // Total Dissolved Solids from EZO (ppm)
    float salinity;                 // Salinity from EZO (PSU)
    float specific_gravity;         // Specific gravity from EZO
    float temperature_c;            // Current temperature from MAX31865 (Celsius)
    float temperature_f;            // Current temperature (Fahrenheit)
    bool sensor_ok;                 // EZO-EC connection and reading OK
    bool temp_sensor_ok;            // MAX31865 PT1000 OK
    uint32_t timestamp;             // Reading timestamp (millis)
} conductivity_reading_t;

// ============================================================================
// CLASS DEFINITION
// ============================================================================

class ConductivitySensor {
public:
    /**
     * @brief Constructor
     * @param ezoSerial HardwareSerial reference for EZO-EC UART (e.g., Serial2)
     * @param ezoRxPin ESP32 RX pin connected to EZO TX
     * @param ezoTxPin ESP32 TX pin connected to EZO RX
     * @param rtdCsPin MAX31865 chip select pin
     * @param rtdMosiPin MAX31865 MOSI pin (software SPI)
     * @param rtdMisoPin MAX31865 MISO pin (software SPI)
     * @param rtdSckPin MAX31865 SCK pin (software SPI)
     */
    ConductivitySensor(HardwareSerial& ezoSerial,
                        uint8_t ezoRxPin, uint8_t ezoTxPin,
                        uint8_t rtdCsPin, uint8_t rtdMosiPin,
                        uint8_t rtdMisoPin, uint8_t rtdSckPin);

    /**
     * @brief Initialize EZO-EC UART and MAX31865 SPI
     * @return true if both devices initialized successfully
     */
    bool begin();

    /**
     * @brief Configure sensor parameters from system config
     * @param config Pointer to conductivity configuration
     */
    void configure(conductivity_config_t* config);

    /**
     * @brief Take a conductivity measurement with temperature compensation
     *
     * Reads temperature from MAX31865, sends it to EZO via RT command,
     * and parses the resulting EC/TDS/SAL/SG values.
     *
     * @return Measurement result structure
     */
    conductivity_reading_t read();

    /**
     * @brief Read temperature from MAX31865 PT1000 RTD
     * @return Temperature in Celsius, or -999 on error
     */
    float readTemperature();

    /**
     * @brief Get the last measurement result
     * @return Last measurement result
     */
    conductivity_reading_t getLastReading();

    // ------------------------------------------------------------------
    // EZO-EC Calibration (managed by EZO hardware)
    // ------------------------------------------------------------------

    /**
     * @brief Perform dry calibration (must be done first)
     * @return true if EZO acknowledged
     */
    bool calibrateDry();

    /**
     * @brief Single-point calibration with known solution
     * @param value Known conductivity in uS/cm
     * @return true if EZO acknowledged
     */
    bool calibrateSingle(float value);

    /**
     * @brief Two-point low calibration
     * @param value Known low conductivity in uS/cm
     * @return true if EZO acknowledged
     */
    bool calibrateLow(float value);

    /**
     * @brief Two-point high calibration
     * @param value Known high conductivity in uS/cm
     * @return true if EZO acknowledged
     */
    bool calibrateHigh(float value);

    /**
     * @brief Query EZO calibration status
     * @return 0=not calibrated, 1=single point, 2=two point
     */
    uint8_t getCalibrationStatus();

    /**
     * @brief Export calibration data from EZO
     * @return Calibration data string, or empty on error
     */
    String exportCalibration();

    /**
     * @brief Import calibration data to EZO
     * @param data Calibration data string from exportCalibration()
     * @return true if EZO acknowledged
     */
    bool importCalibration(const String& data);

    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------

    /**
     * @brief Set probe cell constant (K value) on EZO
     * @param k Cell constant (0.01 - 10.0)
     */
    void setCellConstant(float k);

    /**
     * @brief Set TDS conversion factor on EZO
     * @param factor Conversion factor (0.01 - 1.00, default 0.54)
     */
    void setTDSConversionFactor(float factor);

    /**
     * @brief Enable/disable temperature compensation
     * @param enable True to send RTD temperature to EZO for compensation
     */
    void setTempCompensation(bool enable);

    /**
     * @brief Set manual temperature (used when RTD fails)
     * @param temp_c Temperature in Celsius
     */
    void setManualTemperature(float temp_c);

    /**
     * @brief Enable/disable anti-flashing filter (software low-pass)
     * @param enable True to enable anti-flashing
     * @param factor Dampening factor (1-10, higher = more smoothing)
     */
    void setAntiFlash(bool enable, uint8_t factor = 5);

    /**
     * @brief Apply software calibration trim percentage
     * @param percent Calibration percentage (-50 to +50)
     */
    void setCalibrationPercent(int8_t percent);

    /**
     * @brief Get current software calibration trim percentage
     * @return Calibration percentage
     */
    int8_t getCalibrationPercent();

    /**
     * @brief Configure which parameters EZO includes in output
     * @param ec Enable conductivity output
     * @param tds Enable TDS output
     * @param sal Enable salinity output
     * @param sg Enable specific gravity output
     */
    void setOutputParameters(bool ec, bool tds, bool sal, bool sg);

    // ------------------------------------------------------------------
    // Status
    // ------------------------------------------------------------------

    /**
     * @brief Check if EZO-EC is connected and responding
     * @return true if EZO sensor OK
     */
    bool isSensorOK();

    /**
     * @brief Check if MAX31865 PT1000 is OK
     * @return true if temp sensor OK
     */
    bool isTempSensorOK();

    /**
     * @brief Get EZO device information (type, firmware version)
     * @return Info string, e.g. "?I,EC,2.15"
     */
    String getDeviceInfo();

    /**
     * @brief Get EZO status (voltage and restart reason)
     * @return Status string, e.g. "?Status,P,5.02"
     */
    String getDeviceStatus();

    /**
     * @brief Check MAX31865 for RTD faults
     * @return Fault register value (0 = no faults)
     */
    uint8_t getRTDFault();

    // ------------------------------------------------------------------
    // EZO Control
    // ------------------------------------------------------------------

    /**
     * @brief Put EZO into low-power sleep mode
     */
    void sleep();

    /**
     * @brief Wake EZO from sleep (any command wakes it)
     */
    void wake();

    /**
     * @brief Factory reset EZO (clears all calibration!)
     */
    void factoryReset();

    /**
     * @brief Set EZO LED on/off
     * @param on True to turn LED on
     */
    void setLED(bool on);

    /**
     * @brief Blink EZO LED white to locate device
     */
    void find();

    // ------------------------------------------------------------------
    // Utility
    // ------------------------------------------------------------------

    /**
     * @brief Convert conductivity to PPM using configured factor
     * @param conductivity Conductivity in uS/cm
     * @param conversion_factor PPM conversion factor
     * @return TDS in PPM
     */
    static float conductivityToPPM(float conductivity, float conversion_factor = 0.54);

    /**
     * @brief Send a raw command to EZO and get response
     * @param command Command string (without CR terminator)
     * @param timeout_ms Response timeout in milliseconds
     * @return Response string, or empty on timeout
     */
    String sendCommand(const String& command, uint16_t timeout_ms = EZO_CMD_TIMEOUT_MS);

private:
    // Hardware interfaces
    HardwareSerial& _serial;
    uint8_t _rxPin;
    uint8_t _txPin;
    Adafruit_MAX31865 _rtd;

    // Configuration
    conductivity_config_t* _config;
    int8_t _calibration_percent;

    // State
    conductivity_reading_t _last_reading;
    bool _initialized;
    bool _ezo_ok;
    bool _rtd_ok;
    bool _temp_comp_enabled;
    float _manual_temp;
    bool _anti_flash_enabled;
    uint8_t _anti_flash_factor;
    float _anti_flash_buffer;
    bool _sleeping;

    // Internal methods
    String readResponse(uint16_t timeout_ms);
    bool isResponseOK(const String& response);
    bool parseReadingResponse(const String& response,
                              float& ec, float& tds, float& sal, float& sg);
    float applyAntiFlash(float conductivity);
    float applySoftwareCalibration(float conductivity);
    void drainSerial();
};

#endif // CONDUCTIVITY_H

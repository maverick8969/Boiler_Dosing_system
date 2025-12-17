/**
 * @file water_meter.h
 * @brief Water Meter Input Module
 *
 * Handles water meter pulse counting with:
 * - Contact closure (contactor) support
 * - Paddlewheel (Hall effect) support
 * - Flow rate calculation
 * - Volume totalizer with NVS persistence
 */

#ifndef WATER_METER_H
#define WATER_METER_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// WATER METER CLASS
// ============================================================================

class WaterMeter {
public:
    /**
     * @brief Constructor
     * @param pin GPIO pin for meter input
     * @param meter_id Meter identifier (0 or 1)
     */
    WaterMeter(uint8_t pin, uint8_t meter_id);

    /**
     * @brief Initialize meter hardware
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Configure meter parameters
     * @param config Pointer to meter configuration
     */
    void configure(water_meter_config_t* config);

    /**
     * @brief Update meter - call frequently for flow rate calculation
     */
    void update();

    /**
     * @brief Get total volume
     * @return Volume in configured units (gallons or liters)
     */
    uint32_t getTotalVolume();

    /**
     * @brief Get flow rate
     * @return Flow rate in units per minute (GPM or LPM)
     */
    float getFlowRate();

    /**
     * @brief Get pulse count since last reset
     * @return Raw pulse count
     */
    uint32_t getPulseCount();

    /**
     * @brief Get contact count since last check (for feed mode D)
     * @return Number of contacts since last call
     */
    uint32_t getContactsSinceLast();

    /**
     * @brief Get volume since last check (for feed mode E)
     * @return Volume since last call
     */
    float getVolumeSinceLast();

    /**
     * @brief Reset total volume to zero
     */
    void resetTotal();

    /**
     * @brief Save total to NVS
     */
    void saveToNVS();

    /**
     * @brief Load total from NVS
     */
    void loadFromNVS();

    /**
     * @brief Get meter type string
     * @return Type name
     */
    const char* getTypeName();

    /**
     * @brief Check if meter is enabled
     * @return true if enabled
     */
    bool isEnabled();

    /**
     * @brief Interrupt handler (called from ISR)
     */
    static void IRAM_ATTR handleInterrupt(void* arg);

private:
    uint8_t _pin;
    uint8_t _meter_id;
    water_meter_config_t* _config;

    // Pulse counting (volatile for ISR access)
    volatile uint32_t _pulse_count;
    volatile uint32_t _last_pulse_time;

    // Flow rate calculation
    uint32_t _last_flow_calc_time;
    uint32_t _last_flow_pulse_count;
    float _flow_rate;

    // For delta queries
    uint32_t _last_query_pulse_count;
    float _last_query_volume;

    // Debouncing
    uint32_t _debounce_time;

    // Internal methods
    float pulsesToVolume(uint32_t pulses);
};

// ============================================================================
// WATER METER MANAGER
// ============================================================================

class WaterMeterManager {
public:
    WaterMeterManager();

    bool begin();
    void configure(water_meter_config_t configs[2]);
    void update();

    WaterMeter* getMeter(uint8_t id);

    // Combined volume from both meters
    uint32_t getTotalVolume();
    float getCombinedFlowRate();

    // For feed modes
    uint32_t getContactsSinceLast(uint8_t meter_select);  // 0=WM1, 1=WM2, 2=Both
    float getVolumeSinceLast(uint8_t meter_select);

    void saveAllToNVS();
    void loadAllFromNVS();

private:
    WaterMeter* _meters[2];
};

extern WaterMeterManager waterMeterManager;

#endif // WATER_METER_H

/**
 * @file blowdown.h
 * @brief Blowdown Valve Control Module
 *
 * Implements blowdown control with:
 * - Continuous mode (setpoint-based)
 * - Intermittent sampling modes (I/T/P)
 * - Ball valve delay for motorized actuators
 * - Timeout protection
 * - HOA (Hand-Off-Auto) control
 */

#ifndef BLOWDOWN_H
#define BLOWDOWN_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// BLOWDOWN STATE MACHINE
// ============================================================================

typedef enum {
    BD_STATE_IDLE = 0,          // Waiting, valve closed
    BD_STATE_VALVE_OPENING,     // Ball valve opening
    BD_STATE_BLOWING_DOWN,      // Valve open, blowing down
    BD_STATE_VALVE_CLOSING,     // Ball valve closing
    BD_STATE_SAMPLING,          // Intermittent: taking sample
    BD_STATE_HOLDING,           // Intermittent: sample trapped
    BD_STATE_WAITING,           // Intermittent: waiting for next cycle
    BD_STATE_TIMEOUT,           // Timeout occurred
    BD_STATE_ERROR              // Error state
} blowdown_state_t;

// ============================================================================
// BLOWDOWN STATUS STRUCTURE
// ============================================================================

typedef struct {
    blowdown_state_t state;
    bool valve_open;
    bool relay_energized;
    hoa_mode_t hoa_mode;
    uint32_t state_start_time;
    uint32_t blowdown_start_time;
    uint32_t current_blowdown_time;
    uint32_t total_blowdown_time;       // Today's total
    uint32_t accumulated_blowdown_time; // For feed mode B calculation
    bool timeout_flag;
    bool waiting_for_reset;
    float last_conductivity;
    float trapped_sample_conductivity;
} blowdown_status_t;

// ============================================================================
// BLOWDOWN CONTROLLER CLASS
// ============================================================================

class BlowdownController {
public:
    /**
     * @brief Constructor
     * @param relay_pin GPIO for relay output
     * @param nc_pin GPIO for NC contact (optional, for dual relay)
     */
    BlowdownController(uint8_t relay_pin, uint8_t nc_pin = 255);

    /**
     * @brief Initialize controller
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Configure blowdown parameters
     * @param config Pointer to blowdown configuration
     */
    void configure(blowdown_config_t* config);

    /**
     * @brief Configure conductivity parameters (for sampling modes)
     * @param cond_config Pointer to conductivity configuration
     */
    void setConductivityConfig(conductivity_config_t* cond_config);

    /**
     * @brief Main control update - call frequently
     * @param conductivity Current conductivity reading
     * @param flow_ok True if flow switch indicates flow present
     */
    void update(float conductivity, bool flow_ok);

    /**
     * @brief Set HOA mode
     * @param mode HOA mode
     */
    void setHOA(hoa_mode_t mode);

    /**
     * @brief Get HOA mode
     * @return Current HOA mode
     */
    hoa_mode_t getHOA();

    /**
     * @brief Manually open valve
     */
    void openValve();

    /**
     * @brief Manually close valve
     */
    void closeValve();

    /**
     * @brief Reset timeout condition
     */
    void resetTimeout();

    /**
     * @brief Get current status
     * @return Status structure
     */
    blowdown_status_t getStatus();

    /**
     * @brief Check if blowdown is active
     * @return true if blowing down
     */
    bool isActive();

    /**
     * @brief Check if timeout occurred
     * @return true if timed out
     */
    bool isTimeout();

    /**
     * @brief Get accumulated blowdown time (for feed mode B)
     * @return Time in milliseconds
     */
    uint32_t getAccumulatedTime();

    /**
     * @brief Clear accumulated blowdown time
     */
    void clearAccumulatedTime();

    /**
     * @brief Get total blowdown time today
     * @return Time in seconds
     */
    uint32_t getTotalBlowdownTime();

    /**
     * @brief Reset daily total
     */
    void resetDailyTotal();

private:
    // Hardware
    uint8_t _relay_pin;
    uint8_t _nc_pin;
    bool _dual_relay;

    // Configuration
    blowdown_config_t* _config;
    conductivity_config_t* _cond_config;

    // Status
    blowdown_status_t _status;

    // Ball valve timing
    uint32_t _valve_action_start;
    bool _valve_target_state;

    // Intermittent mode timing
    uint32_t _interval_timer;
    uint32_t _duration_timer;
    uint32_t _hold_timer;
    uint32_t _prop_blowdown_time;

    // Internal methods
    void processHOA();
    void processContinuousMode(float conductivity);
    void processIntermittentMode(float conductivity);
    void processTimedBlowdown(float conductivity);
    void processTimeProportional(float conductivity);
    void setRelayState(bool energize);
    void startBallValve(bool opening);
    void checkBallValveComplete();
    void checkTimeout();
    void transitionState(blowdown_state_t new_state);
    uint32_t calculateProportionalTime(float conductivity);
};

extern BlowdownController blowdownController;

#endif // BLOWDOWN_H

/**
 * @file chemical_pump.h
 * @brief Chemical Dosing Pump Control Module
 *
 * Controls Nema17 stepper motors via A4988 drivers for chemical injection:
 * - Hydrogen Sulfite (H2SO3) - Oxygen scavenger
 * - Sodium Hydroxide (NaOH) - pH adjustment
 * - Amine - Condensate treatment
 *
 * Supports multiple feed modes from Lakewood 1575e and Walchem WBL400 controllers.
 */

#ifndef CHEMICAL_PUMP_H
#define CHEMICAL_PUMP_H

#include <Arduino.h>
#include <AccelStepper.h>
#include "config.h"

// ============================================================================
// PUMP IDENTIFICATION
// ============================================================================

typedef enum {
    PUMP_H2SO3 = 0,     // Hydrogen Sulfite - Oxygen Scavenger
    PUMP_NAOH = 1,      // Sodium Hydroxide - pH Control
    PUMP_AMINE = 2,     // Amine - Condensate Treatment
    PUMP_COUNT = 3
} pump_id_t;

// ============================================================================
// PUMP STATE
// ============================================================================

typedef enum {
    PUMP_STATE_IDLE = 0,
    PUMP_STATE_RUNNING,
    PUMP_STATE_PRIMING,
    PUMP_STATE_CALIBRATING,
    PUMP_STATE_LOCKED_OUT,
    PUMP_STATE_ERROR
} pump_state_t;

// ============================================================================
// PUMP STATUS STRUCTURE
// ============================================================================

typedef struct {
    pump_state_t state;
    bool enabled;
    bool running;
    hoa_mode_t hoa_mode;
    uint32_t start_time;            // When current run started
    uint32_t runtime_ms;            // Current run duration
    uint32_t total_runtime_ms;      // Total runtime this session
    uint32_t total_steps;           // Total steps this session
    float volume_dispensed_ml;      // Estimated volume dispensed
    uint32_t lockout_end_time;      // When lockout expires
    uint32_t accumulated_feed_time; // For modes B, D, E
    uint32_t contact_count;         // For mode D
    float accumulated_volume;       // For mode E
} pump_status_t;

// ============================================================================
// PUMP CLASS
// ============================================================================

class ChemicalPump {
public:
    /**
     * @brief Constructor
     * @param id Pump identifier
     * @param step_pin GPIO for step signal
     * @param dir_pin GPIO for direction signal
     * @param enable_pin GPIO for enable signal (active LOW)
     */
    ChemicalPump(pump_id_t id, uint8_t step_pin, uint8_t dir_pin, uint8_t enable_pin);

    /**
     * @brief Initialize pump hardware
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Configure pump parameters
     * @param config Pointer to pump configuration
     */
    void configure(pump_config_t* config);

    /**
     * @brief Main update function - call in loop
     * Must be called frequently to step the motor
     */
    void update();

    /**
     * @brief Start pumping
     * @param duration_ms Duration in milliseconds (0 = until stopped)
     * @param volume_ml Target volume in ml (0 = use duration)
     */
    void start(uint32_t duration_ms = 0, float volume_ml = 0);

    /**
     * @brief Stop pumping immediately
     */
    void stop();

    /**
     * @brief Enable or disable the pump
     * @param enable true to enable
     */
    void setEnabled(bool enable);

    /**
     * @brief Set HOA mode
     * @param mode HOA mode
     */
    void setHOA(hoa_mode_t mode);

    /**
     * @brief Get current HOA mode
     * @return HOA mode
     */
    hoa_mode_t getHOA();

    /**
     * @brief Process feed mode logic
     * @param blowdown_active True if blowdown is currently active
     * @param blowdown_time_ms Accumulated blowdown time
     * @param water_contacts Number of water meter contacts since last check
     * @param water_volume Volume from paddlewheel since last check
     */
    void processFeedMode(bool blowdown_active, uint32_t blowdown_time_ms,
                        uint32_t water_contacts, float water_volume);

    /**
     * @brief Process scheduled feed
     * @param current_time Current epoch time
     */
    void processSchedule(uint32_t current_time);

    /**
     * @brief Prime the pump (run for fixed duration)
     * @param duration_ms Prime duration
     */
    void prime(uint32_t duration_ms = 5000);

    /**
     * @brief Start calibration mode
     * @param steps Number of steps to run
     */
    void startCalibration(uint32_t steps);

    /**
     * @brief Set calibration (steps per ml)
     * @param steps_per_ml Steps per milliliter
     */
    void setCalibration(uint32_t steps_per_ml);

    /**
     * @brief Get current status
     * @return Status structure
     */
    pump_status_t getStatus();

    /**
     * @brief Check if pump is running
     * @return true if running
     */
    bool isRunning();

    /**
     * @brief Check if pump is in error state
     * @return true if error
     */
    bool hasError();

    /**
     * @brief Clear error state
     */
    void clearError();

    /**
     * @brief Get pump ID
     * @return Pump ID
     */
    pump_id_t getID();

    /**
     * @brief Get pump name
     * @return Pump name string
     */
    const char* getName();

    /**
     * @brief Reset statistics
     */
    void resetStats();

    /**
     * @brief Get total runtime in seconds
     * @return Total runtime
     */
    uint32_t getTotalRuntimeSec();

    /**
     * @brief Get estimated volume dispensed
     * @return Volume in ml
     */
    float getTotalVolumeMl();

private:
    // Identification
    pump_id_t _id;
    char _name[16];

    // Hardware
    AccelStepper _stepper;
    uint8_t _step_pin;
    uint8_t _dir_pin;
    uint8_t _enable_pin;

    // Configuration
    pump_config_t* _config;

    // State
    pump_status_t _status;
    uint32_t _target_steps;
    uint32_t _target_time_ms;
    bool _time_limited;
    bool _steps_limited;

    // Mode-specific state
    uint32_t _mode_b_accumulated_blowdown;
    bool _mode_a_was_blowing;
    uint32_t _mode_c_cycle_start;

    // Internal methods
    void enableDriver(bool enable);
    void processHOA();
    void processModeA(bool blowdown_active);
    void processModeB(bool blowdown_active, uint32_t blowdown_time_ms);
    void processModeC();
    void processModeD(uint32_t water_contacts);
    void processModeE(float water_volume);
    void checkTimeout();
    void updateStats();
};

// ============================================================================
// PUMP MANAGER CLASS
// ============================================================================

class PumpManager {
public:
    /**
     * @brief Constructor - initializes all pumps
     */
    PumpManager();

    /**
     * @brief Initialize all pump hardware
     * @return true if all pumps initialized successfully
     */
    bool begin();

    /**
     * @brief Configure all pumps
     * @param configs Array of pump configurations
     */
    void configure(pump_config_t configs[PUMP_COUNT]);

    /**
     * @brief Update all pumps - call in loop
     */
    void update();

    /**
     * @brief Process feed modes for all pumps
     */
    void processFeedModes(bool blowdown_active, uint32_t blowdown_time_ms,
                         uint32_t water_contacts, float water_volume);

    /**
     * @brief Enable/disable all pumps
     */
    void setAllEnabled(bool enable);

    /**
     * @brief Stop all pumps
     */
    void stopAll();

    /**
     * @brief Emergency stop all pumps
     */
    void emergencyStop();

    /**
     * @brief Get pump by ID
     * @param id Pump ID
     * @return Pointer to pump (nullptr if invalid ID)
     */
    ChemicalPump* getPump(pump_id_t id);

    /**
     * @brief Check if any pump is running
     * @return true if any pump running
     */
    bool anyPumpRunning();

    /**
     * @brief Check if any pump has error
     * @return true if any pump has error
     */
    bool anyPumpError();

private:
    ChemicalPump* _pumps[PUMP_COUNT];
    bool _initialized;
    bool _emergency_stop;
};

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

extern PumpManager pumpManager;

#endif // CHEMICAL_PUMP_H

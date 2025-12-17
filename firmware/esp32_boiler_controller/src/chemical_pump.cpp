/**
 * @file chemical_pump.cpp
 * @brief Chemical Dosing Pump Control Implementation
 *
 * Implements stepper motor control for Nema17 pumps via A4988 drivers.
 */

#include "chemical_pump.h"
#include "pin_definitions.h"

// Global pump manager instance
PumpManager pumpManager;

// ============================================================================
// CHEMICAL PUMP IMPLEMENTATION
// ============================================================================

ChemicalPump::ChemicalPump(pump_id_t id, uint8_t step_pin, uint8_t dir_pin, uint8_t enable_pin)
    : _id(id)
    , _stepper(AccelStepper::DRIVER, step_pin, dir_pin)
    , _step_pin(step_pin)
    , _dir_pin(dir_pin)
    , _enable_pin(enable_pin)
    , _config(nullptr)
    , _target_steps(0)
    , _target_time_ms(0)
    , _time_limited(false)
    , _steps_limited(false)
    , _mode_b_accumulated_blowdown(0)
    , _mode_a_was_blowing(false)
    , _mode_c_cycle_start(0)
{
    // Set pump name based on ID
    switch (_id) {
        case PUMP_H2SO3:
            strncpy(_name, "H2SO3", sizeof(_name));
            break;
        case PUMP_NAOH:
            strncpy(_name, "NaOH", sizeof(_name));
            break;
        case PUMP_AMINE:
            strncpy(_name, "Amine", sizeof(_name));
            break;
        default:
            strncpy(_name, "Unknown", sizeof(_name));
    }

    // Initialize status
    memset(&_status, 0, sizeof(_status));
    _status.state = PUMP_STATE_IDLE;
    _status.hoa_mode = HOA_AUTO;
}

bool ChemicalPump::begin() {
    // Configure pins
    pinMode(_enable_pin, OUTPUT);
    digitalWrite(_enable_pin, HIGH);  // Disable driver initially (active LOW)

    // Configure stepper
    _stepper.setMaxSpeed(PUMP_DEFAULT_MAX_SPEED);
    _stepper.setAcceleration(PUMP_DEFAULT_ACCELERATION);

    Serial.printf("Pump %s initialized (step=%d, dir=%d, en=%d)\n",
                  _name, _step_pin, _dir_pin, _enable_pin);

    return true;
}

void ChemicalPump::configure(pump_config_t* config) {
    _config = config;

    if (_config) {
        _stepper.setMaxSpeed(_config->max_speed);
        _stepper.setAcceleration(_config->acceleration);
        _status.enabled = _config->enabled;
        _status.hoa_mode = _config->hoa_mode;

        strncpy(_name, _config->name, sizeof(_name));
        _name[sizeof(_name) - 1] = '\0';
    }
}

void ChemicalPump::update() {
    // Process HOA mode
    processHOA();

    // Check for timeout
    checkTimeout();

    // Run stepper
    if (_status.running) {
        _stepper.run();

        // Check if target reached
        if (_steps_limited && _stepper.distanceToGo() == 0) {
            stop();
        }

        // Check time limit
        if (_time_limited) {
            uint32_t elapsed = millis() - _status.start_time;
            if (elapsed >= _target_time_ms) {
                stop();
            }
        }

        // Update stats
        updateStats();
    }
}

void ChemicalPump::start(uint32_t duration_ms, float volume_ml) {
    if (!_status.enabled) {
        Serial.printf("Pump %s: Cannot start - disabled\n", _name);
        return;
    }

    if (_status.state == PUMP_STATE_LOCKED_OUT) {
        if (millis() < _status.lockout_end_time) {
            Serial.printf("Pump %s: Cannot start - locked out\n", _name);
            return;
        }
        _status.state = PUMP_STATE_IDLE;
    }

    // Enable driver
    enableDriver(true);

    _status.state = PUMP_STATE_RUNNING;
    _status.running = true;
    _status.start_time = millis();
    _status.runtime_ms = 0;

    // Set target based on volume or duration
    if (volume_ml > 0 && _config && _config->steps_per_ml > 0) {
        _target_steps = (uint32_t)(volume_ml * _config->steps_per_ml);
        _steps_limited = true;
        _stepper.move(_target_steps);
    } else {
        _steps_limited = false;
        // Set to run indefinitely in one direction
        _stepper.move(1000000000);  // Very large number
    }

    if (duration_ms > 0) {
        _target_time_ms = duration_ms;
        _time_limited = true;
    } else {
        _time_limited = false;
    }

    Serial.printf("Pump %s: Started (duration=%lu ms, volume=%.2f ml)\n",
                  _name, duration_ms, volume_ml);
}

void ChemicalPump::stop() {
    _stepper.stop();
    enableDriver(false);

    _status.running = false;

    if (_status.state == PUMP_STATE_RUNNING) {
        _status.state = PUMP_STATE_IDLE;
    }

    Serial.printf("Pump %s: Stopped (ran for %lu ms)\n",
                  _name, _status.runtime_ms);
}

void ChemicalPump::setEnabled(bool enable) {
    _status.enabled = enable;
    if (_config) {
        _config->enabled = enable;
    }

    if (!enable && _status.running) {
        stop();
    }
}

void ChemicalPump::setHOA(hoa_mode_t mode) {
    _status.hoa_mode = mode;
    if (_config) {
        _config->hoa_mode = mode;
    }
}

hoa_mode_t ChemicalPump::getHOA() {
    return _status.hoa_mode;
}

void ChemicalPump::processFeedMode(bool blowdown_active, uint32_t blowdown_time_ms,
                                   uint32_t water_contacts, float water_volume) {
    if (!_status.enabled || !_config) return;
    if (_status.hoa_mode != HOA_AUTO) return;

    switch (_config->feed_mode) {
        case FEED_MODE_A_BLOWDOWN_FEED:
            processModeA(blowdown_active);
            break;
        case FEED_MODE_B_PERCENT_BLOWDOWN:
            processModeB(blowdown_active, blowdown_time_ms);
            break;
        case FEED_MODE_C_PERCENT_TIME:
            processModeC();
            break;
        case FEED_MODE_D_WATER_CONTACT:
            processModeD(water_contacts);
            break;
        case FEED_MODE_E_PADDLEWHEEL:
            processModeE(water_volume);
            break;
        case FEED_MODE_DISABLED:
        default:
            break;
    }
}

void ChemicalPump::processSchedule(uint32_t current_time) {
    // Schedule processing handled by main controller
    // This method is for future expansion
}

void ChemicalPump::prime(uint32_t duration_ms) {
    if (!_status.enabled) return;

    _status.state = PUMP_STATE_PRIMING;
    start(duration_ms, 0);

    Serial.printf("Pump %s: Priming for %lu ms\n", _name, duration_ms);
}

void ChemicalPump::startCalibration(uint32_t steps) {
    if (!_status.enabled) return;

    _status.state = PUMP_STATE_CALIBRATING;
    enableDriver(true);

    _stepper.move(steps);
    _status.running = true;
    _status.start_time = millis();
    _target_steps = steps;
    _steps_limited = true;
    _time_limited = false;

    Serial.printf("Pump %s: Calibrating (%lu steps)\n", _name, steps);
}

void ChemicalPump::setCalibration(uint32_t steps_per_ml) {
    if (_config) {
        _config->steps_per_ml = steps_per_ml;
    }
}

pump_status_t ChemicalPump::getStatus() {
    return _status;
}

bool ChemicalPump::isRunning() {
    return _status.running;
}

bool ChemicalPump::hasError() {
    return _status.state == PUMP_STATE_ERROR;
}

void ChemicalPump::clearError() {
    if (_status.state == PUMP_STATE_ERROR) {
        _status.state = PUMP_STATE_IDLE;
    }
}

pump_id_t ChemicalPump::getID() {
    return _id;
}

const char* ChemicalPump::getName() {
    return _name;
}

void ChemicalPump::resetStats() {
    _status.total_runtime_ms = 0;
    _status.total_steps = 0;
    _status.volume_dispensed_ml = 0;
}

uint32_t ChemicalPump::getTotalRuntimeSec() {
    return _status.total_runtime_ms / 1000;
}

float ChemicalPump::getTotalVolumeMl() {
    return _status.volume_dispensed_ml;
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void ChemicalPump::enableDriver(bool enable) {
    digitalWrite(_enable_pin, enable ? LOW : HIGH);  // Active LOW
}

void ChemicalPump::processHOA() {
    static uint32_t hand_start_time = 0;

    switch (_status.hoa_mode) {
        case HOA_HAND:
            // Force ON with timeout
            if (!_status.running) {
                hand_start_time = millis();
                start(HOA_HAND_TIMEOUT_SEC * 1000, 0);
            } else if (millis() - hand_start_time >= HOA_HAND_TIMEOUT_SEC * 1000) {
                // Timeout - return to AUTO
                stop();
                _status.hoa_mode = HOA_AUTO;
            }
            break;

        case HOA_OFF:
            // Force OFF
            if (_status.running) {
                stop();
            }
            break;

        case HOA_AUTO:
            // Normal operation - handled by processFeedMode
            break;
    }
}

void ChemicalPump::processModeA(bool blowdown_active) {
    // Mode A: Feed follows blowdown with optional lockout

    if (blowdown_active) {
        if (!_status.running) {
            // Check lockout time limit
            if (_config->lockout_seconds > 0) {
                start(_config->lockout_seconds * 1000, 0);
            } else {
                start(0, 0);  // No time limit
            }
        }
        _mode_a_was_blowing = true;
    } else {
        if (_mode_a_was_blowing && _status.running) {
            stop();
        }
        _mode_a_was_blowing = false;
    }
}

void ChemicalPump::processModeB(bool blowdown_active, uint32_t blowdown_time_ms) {
    // Mode B: Feed is percentage of accumulated blowdown time

    if (blowdown_active) {
        // Accumulate blowdown time
        _mode_b_accumulated_blowdown = blowdown_time_ms;
        // Don't feed during blowdown
        if (_status.running) {
            stop();
        }
    } else {
        // Blowdown stopped - calculate and run feed time
        if (_mode_b_accumulated_blowdown > 0 && !_status.running) {
            uint32_t feed_time = (_mode_b_accumulated_blowdown * _config->percent_of_blowdown) / 100;

            // Cap at max time
            if (_config->max_time_seconds > 0) {
                uint32_t max_ms = _config->max_time_seconds * 1000;
                if (feed_time > max_ms) {
                    feed_time = max_ms;
                }
            }

            if (feed_time > 0) {
                start(feed_time, 0);
            }

            _mode_b_accumulated_blowdown = 0;
        }
    }
}

void ChemicalPump::processModeC() {
    // Mode C: Continuous duty cycle (% of time)

    if (_mode_c_cycle_start == 0) {
        _mode_c_cycle_start = millis();
    }

    uint32_t cycle_time_ms = _config->cycle_time_seconds * 1000;
    uint32_t on_time_ms = (cycle_time_ms * _config->percent_of_time) / 1000;  // /1000 because percent is in 0.1% units
    uint32_t elapsed = millis() - _mode_c_cycle_start;

    if (elapsed < on_time_ms) {
        // Should be ON
        if (!_status.running) {
            start(on_time_ms - elapsed, 0);
        }
    } else if (elapsed >= cycle_time_ms) {
        // Cycle complete, reset
        _mode_c_cycle_start = millis();
    } else {
        // Should be OFF
        if (_status.running) {
            stop();
        }
    }
}

void ChemicalPump::processModeD(uint32_t water_contacts) {
    // Mode D: Feed triggered by water meter contacts

    _status.contact_count += water_contacts;

    if (_status.contact_count >= _config->contact_divider) {
        // Trigger feed
        _status.accumulated_feed_time += _config->time_per_contact_ms;

        // Cap at time limit
        if (_config->time_limit_seconds > 0) {
            uint32_t limit_ms = _config->time_limit_seconds * 1000;
            if (_status.accumulated_feed_time > limit_ms) {
                _status.accumulated_feed_time = limit_ms;
            }
        }

        _status.contact_count -= _config->contact_divider;
    }

    // Run pump if accumulated time available
    if (_status.accumulated_feed_time > 0 && !_status.running) {
        uint32_t run_time = _status.accumulated_feed_time;
        _status.accumulated_feed_time = 0;
        start(run_time, 0);
    }
}

void ChemicalPump::processModeE(float water_volume) {
    // Mode E: Feed triggered by paddlewheel volume

    _status.accumulated_volume += water_volume;

    if (_status.accumulated_volume >= _config->volume_to_initiate) {
        // Trigger feed
        _status.accumulated_feed_time += _config->time_per_volume_ms;

        // Cap at time limit
        if (_config->time_limit_seconds > 0) {
            uint32_t limit_ms = _config->time_limit_seconds * 1000;
            if (_status.accumulated_feed_time > limit_ms) {
                _status.accumulated_feed_time = limit_ms;
            }
        }

        _status.accumulated_volume -= _config->volume_to_initiate;
    }

    // Run pump if accumulated time available
    if (_status.accumulated_feed_time > 0 && !_status.running) {
        uint32_t run_time = _status.accumulated_feed_time;
        _status.accumulated_feed_time = 0;
        start(run_time, 0);
    }
}

void ChemicalPump::checkTimeout() {
    if (!_config || !_status.running) return;

    // Check feed time limit
    if (_config->time_limit_seconds > 0) {
        if (_status.runtime_ms >= _config->time_limit_seconds * 1000) {
            stop();
            _status.state = PUMP_STATE_LOCKED_OUT;
            Serial.printf("Pump %s: Timeout - locked out\n", _name);
        }
    }
}

void ChemicalPump::updateStats() {
    if (_status.running) {
        uint32_t now = millis();
        uint32_t delta = now - _status.start_time;
        _status.runtime_ms = delta;
        _status.total_runtime_ms += delta;

        // Count steps
        long steps = _stepper.currentPosition();
        _status.total_steps += abs(steps);
        _stepper.setCurrentPosition(0);

        // Estimate volume
        if (_config && _config->steps_per_ml > 0) {
            _status.volume_dispensed_ml = (float)_status.total_steps / _config->steps_per_ml;
        }
    }
}

// ============================================================================
// PUMP MANAGER IMPLEMENTATION
// ============================================================================

PumpManager::PumpManager()
    : _initialized(false)
    , _emergency_stop(false)
{
    // Create pump instances
    _pumps[PUMP_H2SO3] = new ChemicalPump(PUMP_H2SO3,
        STEPPER1_STEP_PIN, STEPPER1_DIR_PIN, STEPPER_ENABLE_PIN);
    _pumps[PUMP_NAOH] = new ChemicalPump(PUMP_NAOH,
        STEPPER2_STEP_PIN, STEPPER2_DIR_PIN, STEPPER_ENABLE_PIN);
    _pumps[PUMP_AMINE] = new ChemicalPump(PUMP_AMINE,
        STEPPER3_STEP_PIN, STEPPER3_DIR_PIN, STEPPER_ENABLE_PIN);
}

bool PumpManager::begin() {
    bool success = true;

    for (int i = 0; i < PUMP_COUNT; i++) {
        if (!_pumps[i]->begin()) {
            success = false;
        }
    }

    _initialized = success;
    return success;
}

void PumpManager::configure(pump_config_t configs[PUMP_COUNT]) {
    for (int i = 0; i < PUMP_COUNT; i++) {
        _pumps[i]->configure(&configs[i]);
    }
}

void PumpManager::update() {
    if (_emergency_stop) return;

    for (int i = 0; i < PUMP_COUNT; i++) {
        _pumps[i]->update();
    }
}

void PumpManager::processFeedModes(bool blowdown_active, uint32_t blowdown_time_ms,
                                   uint32_t water_contacts, float water_volume) {
    if (_emergency_stop) return;

    for (int i = 0; i < PUMP_COUNT; i++) {
        _pumps[i]->processFeedMode(blowdown_active, blowdown_time_ms,
                                   water_contacts, water_volume);
    }
}

void PumpManager::setAllEnabled(bool enable) {
    for (int i = 0; i < PUMP_COUNT; i++) {
        _pumps[i]->setEnabled(enable);
    }
}

void PumpManager::stopAll() {
    for (int i = 0; i < PUMP_COUNT; i++) {
        _pumps[i]->stop();
    }
}

void PumpManager::emergencyStop() {
    _emergency_stop = true;
    stopAll();

    // Disable all drivers
    digitalWrite(STEPPER_ENABLE_PIN, HIGH);

    Serial.println("PUMP MANAGER: EMERGENCY STOP ACTIVATED");
}

ChemicalPump* PumpManager::getPump(pump_id_t id) {
    if (id >= PUMP_COUNT) return nullptr;
    return _pumps[id];
}

bool PumpManager::anyPumpRunning() {
    for (int i = 0; i < PUMP_COUNT; i++) {
        if (_pumps[i]->isRunning()) return true;
    }
    return false;
}

bool PumpManager::anyPumpError() {
    for (int i = 0; i < PUMP_COUNT; i++) {
        if (_pumps[i]->hasError()) return true;
    }
    return false;
}

/**
 * @file blowdown.cpp
 * @brief Blowdown Valve Control Implementation
 */

#include "blowdown.h"
#include "pin_definitions.h"

// Global instance
BlowdownController blowdownController(BLOWDOWN_RELAY_PIN, BLOWDOWN_NO_PIN);

// ============================================================================
// BLOWDOWN CONTROLLER IMPLEMENTATION
// ============================================================================

BlowdownController::BlowdownController(uint8_t relay_pin, uint8_t nc_pin)
    : _relay_pin(relay_pin)
    , _nc_pin(nc_pin)
    , _dual_relay(nc_pin != 255)
    , _config(nullptr)
    , _cond_config(nullptr)
    , _valve_action_start(0)
    , _valve_target_state(false)
    , _interval_timer(0)
    , _duration_timer(0)
    , _hold_timer(0)
    , _prop_blowdown_time(0)
{
    memset(&_status, 0, sizeof(_status));
    _status.state = BD_STATE_IDLE;
    _status.hoa_mode = HOA_AUTO;
}

bool BlowdownController::begin() {
    pinMode(_relay_pin, OUTPUT);
    digitalWrite(_relay_pin, LOW);  // Start with valve closed

    if (_dual_relay) {
        pinMode(_nc_pin, OUTPUT);
        digitalWrite(_nc_pin, HIGH);  // NC contact
    }

    Serial.printf("Blowdown controller initialized on pin %d\n", _relay_pin);
    return true;
}

void BlowdownController::configure(blowdown_config_t* config) {
    _config = config;
    if (_config) {
        _status.hoa_mode = _config->hoa_mode;
    }
}

void BlowdownController::setConductivityConfig(conductivity_config_t* cond_config) {
    _cond_config = cond_config;
}

void BlowdownController::update(float conductivity, bool flow_ok) {
    _status.last_conductivity = conductivity;

    // No flow protection
    if (!flow_ok) {
        if (_status.valve_open) {
            closeValve();
        }
        return;
    }

    // Process HOA mode
    processHOA();

    // If not in AUTO, don't run automatic control
    if (_status.hoa_mode != HOA_AUTO) {
        return;
    }

    // Check ball valve transitions
    if (_status.state == BD_STATE_VALVE_OPENING ||
        _status.state == BD_STATE_VALVE_CLOSING) {
        checkBallValveComplete();
        return;
    }

    // Process based on sampling mode
    if (!_cond_config) {
        processContinuousMode(conductivity);
    } else {
        switch (_cond_config->sample_mode) {
            case SAMPLE_MODE_CONTINUOUS:
                processContinuousMode(conductivity);
                break;
            case SAMPLE_MODE_INTERMITTENT:
                processIntermittentMode(conductivity);
                break;
            case SAMPLE_MODE_TIMED_BLOWDOWN:
                processTimedBlowdown(conductivity);
                break;
            case SAMPLE_MODE_TIME_PROPORTIONAL:
                processTimeProportional(conductivity);
                break;
        }
    }

    // Check timeout
    checkTimeout();

    // Update blowdown timer
    if (_status.state == BD_STATE_BLOWING_DOWN) {
        _status.current_blowdown_time = millis() - _status.blowdown_start_time;
    }
}

void BlowdownController::setHOA(hoa_mode_t mode) {
    _status.hoa_mode = mode;
    if (_config) {
        _config->hoa_mode = mode;
    }
}

hoa_mode_t BlowdownController::getHOA() {
    return _status.hoa_mode;
}

void BlowdownController::openValve() {
    if (_config && _config->ball_valve_delay > 0) {
        startBallValve(true);
    } else {
        setRelayState(true);
        _status.valve_open = true;
        _status.blowdown_start_time = millis();
        transitionState(BD_STATE_BLOWING_DOWN);
    }
}

void BlowdownController::closeValve() {
    if (_config && _config->ball_valve_delay > 0) {
        startBallValve(false);
    } else {
        setRelayState(false);
        _status.valve_open = false;

        // Update accumulated time for feed modes
        if (_status.state == BD_STATE_BLOWING_DOWN) {
            _status.accumulated_blowdown_time += _status.current_blowdown_time;
            _status.total_blowdown_time += _status.current_blowdown_time / 1000;
        }

        transitionState(BD_STATE_IDLE);
    }
}

void BlowdownController::resetTimeout() {
    _status.timeout_flag = false;
    _status.waiting_for_reset = false;
    if (_config) {
        _config->timeout_flag = false;
    }
    transitionState(BD_STATE_IDLE);
}

blowdown_status_t BlowdownController::getStatus() {
    return _status;
}

bool BlowdownController::isActive() {
    return _status.valve_open ||
           _status.state == BD_STATE_BLOWING_DOWN ||
           _status.state == BD_STATE_VALVE_OPENING;
}

bool BlowdownController::isTimeout() {
    return _status.timeout_flag;
}

uint32_t BlowdownController::getAccumulatedTime() {
    return _status.accumulated_blowdown_time;
}

void BlowdownController::clearAccumulatedTime() {
    _status.accumulated_blowdown_time = 0;
}

uint32_t BlowdownController::getTotalBlowdownTime() {
    return _status.total_blowdown_time;
}

void BlowdownController::resetDailyTotal() {
    _status.total_blowdown_time = 0;
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void BlowdownController::processHOA() {
    static uint32_t hand_start_time = 0;

    switch (_status.hoa_mode) {
        case HOA_HAND:
            if (!_status.valve_open) {
                hand_start_time = millis();
                openValve();
            } else if (millis() - hand_start_time >= HOA_HAND_TIMEOUT_SEC * 1000) {
                closeValve();
                _status.hoa_mode = HOA_AUTO;
            }
            break;

        case HOA_OFF:
            if (_status.valve_open) {
                closeValve();
            }
            break;

        case HOA_AUTO:
            // Handled by mode processors
            break;
    }
}

void BlowdownController::processContinuousMode(float conductivity) {
    if (!_config || _status.waiting_for_reset) return;

    uint16_t setpoint = _config->setpoint;
    uint16_t deadband = _config->deadband;
    bool high_control = (_config->control_direction == 0);

    if (high_control) {
        // Blowdown when ABOVE setpoint
        if (conductivity > setpoint && !_status.valve_open) {
            openValve();
        } else if (conductivity < (setpoint - deadband) && _status.valve_open) {
            closeValve();
        }
    } else {
        // Blowdown when BELOW setpoint
        if (conductivity < setpoint && !_status.valve_open) {
            openValve();
        } else if (conductivity > (setpoint + deadband) && _status.valve_open) {
            closeValve();
        }
    }
}

void BlowdownController::processIntermittentMode(float conductivity) {
    if (!_config || !_cond_config || _status.waiting_for_reset) return;

    uint32_t now = millis();
    uint16_t setpoint = _config->setpoint;

    switch (_status.state) {
        case BD_STATE_IDLE:
        case BD_STATE_WAITING:
            // Check if interval has elapsed
            if (now - _interval_timer >= _cond_config->interval_seconds * 1000) {
                _interval_timer = now;
                _duration_timer = now;
                transitionState(BD_STATE_SAMPLING);
                openValve();
            }
            break;

        case BD_STATE_SAMPLING:
            // Sample for duration
            if (now - _duration_timer >= _cond_config->duration_seconds * 1000) {
                // Duration complete - check if above setpoint
                if (conductivity > setpoint) {
                    // Continue blowing until below setpoint
                    transitionState(BD_STATE_BLOWING_DOWN);
                } else {
                    // Below setpoint - trap sample
                    closeValve();
                    _hold_timer = now;
                    _status.trapped_sample_conductivity = conductivity;
                    transitionState(BD_STATE_HOLDING);
                }
            }
            break;

        case BD_STATE_BLOWING_DOWN:
            // Wait for conductivity to drop below setpoint
            if (conductivity < setpoint) {
                closeValve();
                _hold_timer = now;
                _status.trapped_sample_conductivity = conductivity;
                transitionState(BD_STATE_HOLDING);
            }
            break;

        case BD_STATE_HOLDING:
            // Hold sample for measurement
            if (now - _hold_timer >= _cond_config->hold_time_seconds * 1000) {
                // Re-check conductivity
                if (conductivity > setpoint) {
                    // Still high - sample again
                    _duration_timer = now;
                    transitionState(BD_STATE_SAMPLING);
                    openValve();
                } else {
                    // Good - wait for next interval
                    transitionState(BD_STATE_WAITING);
                }
            }
            break;

        default:
            break;
    }
}

void BlowdownController::processTimedBlowdown(float conductivity) {
    if (!_config || !_cond_config || _status.waiting_for_reset) return;

    uint32_t now = millis();
    uint16_t setpoint = _config->setpoint;

    switch (_status.state) {
        case BD_STATE_IDLE:
        case BD_STATE_WAITING:
            if (now - _interval_timer >= _cond_config->interval_seconds * 1000) {
                _interval_timer = now;
                _duration_timer = now;
                transitionState(BD_STATE_SAMPLING);
                openValve();
            }
            break;

        case BD_STATE_SAMPLING:
            if (now - _duration_timer >= _cond_config->duration_seconds * 1000) {
                closeValve();
                _hold_timer = now;
                _status.trapped_sample_conductivity = conductivity;
                transitionState(BD_STATE_HOLDING);
            }
            break;

        case BD_STATE_HOLDING:
            if (now - _hold_timer >= _cond_config->hold_time_seconds * 1000) {
                if (conductivity > setpoint) {
                    // Blow down for fixed time
                    _prop_blowdown_time = _cond_config->blow_time_seconds * 1000;
                    transitionState(BD_STATE_BLOWING_DOWN);
                    openValve();
                } else {
                    transitionState(BD_STATE_WAITING);
                }
            }
            break;

        case BD_STATE_BLOWING_DOWN:
            if (now - _status.blowdown_start_time >= _prop_blowdown_time) {
                closeValve();
                _hold_timer = now;
                transitionState(BD_STATE_HOLDING);
            }
            break;

        default:
            break;
    }
}

void BlowdownController::processTimeProportional(float conductivity) {
    if (!_config || !_cond_config || _status.waiting_for_reset) return;

    uint32_t now = millis();
    uint16_t setpoint = _config->setpoint;

    switch (_status.state) {
        case BD_STATE_IDLE:
        case BD_STATE_WAITING:
            if (now - _interval_timer >= _cond_config->interval_seconds * 1000) {
                _interval_timer = now;
                _duration_timer = now;
                transitionState(BD_STATE_SAMPLING);
                openValve();
            }
            break;

        case BD_STATE_SAMPLING:
            if (now - _duration_timer >= _cond_config->duration_seconds * 1000) {
                closeValve();
                _hold_timer = now;
                _status.trapped_sample_conductivity = conductivity;
                transitionState(BD_STATE_HOLDING);
            }
            break;

        case BD_STATE_HOLDING:
            if (now - _hold_timer >= _cond_config->hold_time_seconds * 1000) {
                if (conductivity > setpoint) {
                    // Calculate proportional blowdown time
                    _prop_blowdown_time = calculateProportionalTime(conductivity);
                    transitionState(BD_STATE_BLOWING_DOWN);
                    openValve();
                } else {
                    transitionState(BD_STATE_WAITING);
                }
            }
            break;

        case BD_STATE_BLOWING_DOWN:
            if (now - _status.blowdown_start_time >= _prop_blowdown_time) {
                closeValve();
                _hold_timer = now;
                transitionState(BD_STATE_HOLDING);
            }
            break;

        default:
            break;
    }
}

void BlowdownController::setRelayState(bool energize) {
    _status.relay_energized = energize;

    if (_dual_relay) {
        // Dual relay for motorized valve
        if (energize) {
            digitalWrite(_relay_pin, HIGH);   // NO - open
            digitalWrite(_nc_pin, LOW);       // NC - not close
        } else {
            digitalWrite(_relay_pin, LOW);    // NO - not open
            digitalWrite(_nc_pin, HIGH);      // NC - close
        }
    } else {
        digitalWrite(_relay_pin, energize ? HIGH : LOW);
    }
}

void BlowdownController::startBallValve(bool opening) {
    _valve_target_state = opening;
    _valve_action_start = millis();
    setRelayState(opening);
    transitionState(opening ? BD_STATE_VALVE_OPENING : BD_STATE_VALVE_CLOSING);
}

void BlowdownController::checkBallValveComplete() {
    if (!_config) return;

    uint32_t delay_ms = _config->ball_valve_delay * 1000;
    if (millis() - _valve_action_start >= delay_ms) {
        _status.valve_open = _valve_target_state;

        if (_valve_target_state) {
            // Valve now open
            _status.blowdown_start_time = millis();
            transitionState(BD_STATE_BLOWING_DOWN);
        } else {
            // Valve now closed
            transitionState(BD_STATE_IDLE);
        }
    }
}

void BlowdownController::checkTimeout() {
    if (!_config || _config->time_limit_seconds == 0) return;

    if (_status.state == BD_STATE_BLOWING_DOWN) {
        uint32_t limit_ms = _config->time_limit_seconds * 1000;
        if (_status.current_blowdown_time >= limit_ms) {
            closeValve();
            _status.timeout_flag = true;
            _status.waiting_for_reset = true;
            _config->timeout_flag = true;
            transitionState(BD_STATE_TIMEOUT);
            Serial.println("BLOWDOWN TIMEOUT!");
        }
    }
}

void BlowdownController::transitionState(blowdown_state_t new_state) {
    _status.state = new_state;
    _status.state_start_time = millis();
}

uint32_t BlowdownController::calculateProportionalTime(float conductivity) {
    if (!_config || !_cond_config) return 0;

    float deviation = conductivity - _config->setpoint;
    if (deviation <= 0) return 0;

    float percentage = deviation / _cond_config->prop_band;
    if (percentage > 1.0) percentage = 1.0;

    uint32_t max_time_ms = _cond_config->max_prop_time_seconds * 1000;
    return (uint32_t)(percentage * max_time_ms);
}

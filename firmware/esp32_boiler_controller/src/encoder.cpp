/**
 * @file encoder.cpp
 * @brief Rotary Encoder Input Module Implementation
 */

#include "encoder.h"

// Global instances
RotaryEncoder encoder;
MenuNavigator menuNav(&encoder);

// State table for quadrature decoding
// Indexed by (old_state << 2) | new_state
// Values: 0 = no change, 1 = CW, -1 = CCW
static const int8_t ENCODER_STATE_TABLE[16] = {
     0,  // 00 -> 00
    -1,  // 00 -> 01
     1,  // 00 -> 10
     0,  // 00 -> 11 (invalid)
     1,  // 01 -> 00
     0,  // 01 -> 01
     0,  // 01 -> 10 (invalid)
    -1,  // 01 -> 11
    -1,  // 10 -> 00
     0,  // 10 -> 01 (invalid)
     0,  // 10 -> 10
     1,  // 10 -> 11
     0,  // 11 -> 00 (invalid)
     1,  // 11 -> 01
    -1,  // 11 -> 10
     0   // 11 -> 11
};

// ============================================================================
// ROTARY ENCODER IMPLEMENTATION
// ============================================================================

RotaryEncoder::RotaryEncoder(uint8_t pin_a, uint8_t pin_b, uint8_t pin_btn)
    : _pin_a(pin_a)
    , _pin_b(pin_b)
    , _pin_btn(pin_btn)
    , _position(0)
    , _last_position(0)
    , _last_state(0)
    , _last_rotation_time(0)
    , _button_pressed(false)
    , _button_press_time(0)
    , _button_release_time(0)
    , _press_count(0)
    , _long_press_fired(false)
    , _waiting_for_double(false)
    , _queue_head(0)
    , _queue_tail(0)
    , _callback(nullptr)
    , _acceleration_enabled(true)
    , _accel_threshold(50)
    , _accel_multiplier(4)
    , _limits_enabled(false)
    , _limit_min(0)
    , _limit_max(0)
    , _limit_wrap(false)
{
}

bool RotaryEncoder::begin() {
    // Configure encoder pins with pull-ups
    pinMode(_pin_a, INPUT_PULLUP);
    pinMode(_pin_b, INPUT_PULLUP);
    pinMode(_pin_btn, INPUT_PULLUP);

    // Read initial state
    _last_state = (digitalRead(_pin_a) << 1) | digitalRead(_pin_b);

    // Attach interrupts
    attachInterruptArg(digitalPinToInterrupt(_pin_a), handleEncoderISR, this, CHANGE);
    attachInterruptArg(digitalPinToInterrupt(_pin_b), handleEncoderISR, this, CHANGE);
    attachInterruptArg(digitalPinToInterrupt(_pin_btn), handleButtonISR, this, CHANGE);

    Serial.printf("Encoder initialized (A=%d, B=%d, BTN=%d)\n", _pin_a, _pin_b, _pin_btn);
    return true;
}

void RotaryEncoder::update() {
    // Process button state
    processButton();

    // Process events and call callback
    if (_callback) {
        encoder_event_t event;
        while ((event = getEvent()) != ENC_EVENT_NONE) {
            _callback(event, _position);
        }
    }
}

encoder_event_t RotaryEncoder::getEvent() {
    if (_queue_head == _queue_tail) {
        return ENC_EVENT_NONE;
    }

    encoder_event_t event = _event_queue[_queue_tail];
    _queue_tail = (_queue_tail + 1) % QUEUE_SIZE;
    return event;
}

bool RotaryEncoder::hasEvent() {
    return _queue_head != _queue_tail;
}

int32_t RotaryEncoder::getPosition() {
    return _position;
}

void RotaryEncoder::setPosition(int32_t pos) {
    noInterrupts();
    _position = pos;
    _last_position = pos;
    interrupts();
    applyLimits();
}

void RotaryEncoder::resetPosition() {
    setPosition(0);
}

int32_t RotaryEncoder::getDelta() {
    int32_t delta = _position - _last_position;
    _last_position = _position;
    return delta;
}

bool RotaryEncoder::isButtonPressed() {
    return _button_pressed;
}

uint32_t RotaryEncoder::getButtonPressDuration() {
    if (!_button_pressed) return 0;
    return millis() - _button_press_time;
}

void RotaryEncoder::setCallback(encoder_callback_t callback) {
    _callback = callback;
}

void RotaryEncoder::setAcceleration(bool enable, uint16_t threshold, uint8_t multiplier) {
    _acceleration_enabled = enable;
    _accel_threshold = threshold;
    _accel_multiplier = multiplier;
}

void RotaryEncoder::setLimits(int32_t min, int32_t max, bool wrap) {
    _limits_enabled = true;
    _limit_min = min;
    _limit_max = max;
    _limit_wrap = wrap;
    applyLimits();
}

void RotaryEncoder::clearLimits() {
    _limits_enabled = false;
}

void IRAM_ATTR RotaryEncoder::handleEncoderISR(void* arg) {
    RotaryEncoder* enc = (RotaryEncoder*)arg;

    // Read current state
    uint8_t state = (digitalRead(enc->_pin_a) << 1) | digitalRead(enc->_pin_b);

    // Use state table to determine direction
    uint8_t index = (enc->_last_state << 2) | state;
    int8_t delta = ENCODER_STATE_TABLE[index];

    if (delta != 0) {
        uint32_t now = millis();
        uint32_t time_diff = now - enc->_last_rotation_time;

        // Apply acceleration
        int8_t step = 1;
        if (enc->_acceleration_enabled && time_diff < enc->_accel_threshold) {
            step = enc->_accel_multiplier;
        }

        enc->_position += delta * step;
        enc->_last_rotation_time = now;

        // Queue event (every STEPS_PER_NOTCH pulses)
        static int8_t pulse_count = 0;
        pulse_count += delta;

        if (abs(pulse_count) >= ENCODER_STEPS_PER_NOTCH / 4) {
            encoder_event_t event = (pulse_count > 0) ? ENC_EVENT_CW : ENC_EVENT_CCW;

            // Queue the event
            int next_head = (enc->_queue_head + 1) % QUEUE_SIZE;
            if (next_head != enc->_queue_tail) {
                enc->_event_queue[enc->_queue_head] = event;
                enc->_queue_head = next_head;
            }

            pulse_count = 0;
        }
    }

    enc->_last_state = state;
}

void IRAM_ATTR RotaryEncoder::handleButtonISR(void* arg) {
    RotaryEncoder* enc = (RotaryEncoder*)arg;

    // Debounce
    static uint32_t last_interrupt = 0;
    uint32_t now = millis();
    if (now - last_interrupt < ENCODER_BTN_DEBOUNCE_MS) return;
    last_interrupt = now;

    bool pressed = (digitalRead(enc->_pin_btn) == LOW);

    if (pressed && !enc->_button_pressed) {
        // Button just pressed
        enc->_button_pressed = true;
        enc->_button_press_time = now;
        enc->_long_press_fired = false;
    } else if (!pressed && enc->_button_pressed) {
        // Button just released
        enc->_button_pressed = false;
        enc->_button_release_time = now;

        // Queue release event
        int next_head = (enc->_queue_head + 1) % QUEUE_SIZE;
        if (next_head != enc->_queue_tail) {
            enc->_event_queue[enc->_queue_head] = ENC_EVENT_RELEASE;
            enc->_queue_head = next_head;
        }
    }
}

void RotaryEncoder::queueEvent(encoder_event_t event) {
    int next_head = (_queue_head + 1) % QUEUE_SIZE;
    if (next_head != _queue_tail) {
        _event_queue[_queue_head] = event;
        _queue_head = next_head;
    }
}

void RotaryEncoder::processButton() {
    uint32_t now = millis();

    // Check for long press
    if (_button_pressed && !_long_press_fired) {
        if (now - _button_press_time >= ENCODER_LONG_PRESS_MS) {
            queueEvent(ENC_EVENT_LONG_PRESS);
            _long_press_fired = true;
        }
    }

    // Check for short press / double press
    if (!_button_pressed && _button_release_time > 0) {
        uint32_t press_duration = _button_release_time - _button_press_time;

        if (press_duration < ENCODER_LONG_PRESS_MS) {
            // Short press detected
            if (_waiting_for_double) {
                // This is a double press
                if (now - _button_release_time < ENCODER_DOUBLE_PRESS_MS) {
                    queueEvent(ENC_EVENT_DOUBLE_PRESS);
                    _waiting_for_double = false;
                    _press_count = 0;
                }
            } else {
                _waiting_for_double = true;
                _press_count = 1;
            }
        }

        // Clear release time to prevent re-processing
        _button_release_time = 0;
    }

    // Timeout for double press detection
    if (_waiting_for_double) {
        if (now - _button_press_time > ENCODER_DOUBLE_PRESS_MS * 2) {
            // Single press confirmed
            queueEvent(ENC_EVENT_PRESS);
            _waiting_for_double = false;
            _press_count = 0;
        }
    }
}

void RotaryEncoder::applyLimits() {
    if (!_limits_enabled) return;

    if (_position < _limit_min) {
        _position = _limit_wrap ? _limit_max : _limit_min;
    } else if (_position > _limit_max) {
        _position = _limit_wrap ? _limit_min : _limit_max;
    }
}

// ============================================================================
// MENU NAVIGATOR IMPLEMENTATION
// ============================================================================

MenuNavigator::MenuNavigator(RotaryEncoder* encoder)
    : _encoder(encoder)
    , _item_count(1)
    , _selected(0)
    , _wrap(true)
    , _enter_pressed(false)
    , _back_pressed(false)
    , _home_pressed(false)
    , _editing(false)
{
}

void MenuNavigator::setMenu(int item_count, bool wrap) {
    _item_count = item_count;
    _wrap = wrap;
    _selected = 0;
    _encoder->setLimits(0, item_count - 1, wrap);
    _encoder->setPosition(0);
}

int MenuNavigator::getSelectedIndex() {
    return _selected;
}

void MenuNavigator::setSelectedIndex(int index) {
    if (index >= 0 && index < _item_count) {
        _selected = index;
        _encoder->setPosition(index);
    }
}

bool MenuNavigator::update() {
    _enter_pressed = false;
    _back_pressed = false;
    _home_pressed = false;

    bool changed = false;

    // Process encoder events
    encoder_event_t event;
    while ((event = _encoder->getEvent()) != ENC_EVENT_NONE) {
        switch (event) {
            case ENC_EVENT_CW:
                if (!_editing) {
                    _selected++;
                    if (_selected >= _item_count) {
                        _selected = _wrap ? 0 : _item_count - 1;
                    }
                    changed = true;
                }
                break;

            case ENC_EVENT_CCW:
                if (!_editing) {
                    _selected--;
                    if (_selected < 0) {
                        _selected = _wrap ? _item_count - 1 : 0;
                    }
                    changed = true;
                }
                break;

            case ENC_EVENT_PRESS:
                _enter_pressed = true;
                break;

            case ENC_EVENT_LONG_PRESS:
                _back_pressed = true;
                break;

            case ENC_EVENT_DOUBLE_PRESS:
                _home_pressed = true;
                break;

            default:
                break;
        }
    }

    return changed;
}

bool MenuNavigator::enterPressed() {
    return _enter_pressed;
}

bool MenuNavigator::backPressed() {
    return _back_pressed;
}

bool MenuNavigator::homePressed() {
    return _home_pressed;
}

bool MenuNavigator::editValue(int32_t* value, int32_t min, int32_t max, int32_t step) {
    if (!_editing) {
        // Start editing
        _editing = true;
        _encoder->setPosition(*value);
        _encoder->setLimits(min, max, false);
        return true;
    }

    // Process changes
    encoder_event_t event;
    while ((event = _encoder->getEvent()) != ENC_EVENT_NONE) {
        switch (event) {
            case ENC_EVENT_CW:
                *value += step;
                if (*value > max) *value = max;
                break;

            case ENC_EVENT_CCW:
                *value -= step;
                if (*value < min) *value = min;
                break;

            case ENC_EVENT_PRESS:
                // Confirm value
                _editing = false;
                _encoder->clearLimits();
                return false;

            case ENC_EVENT_LONG_PRESS:
                // Cancel editing (restore original)
                _editing = false;
                _encoder->clearLimits();
                return false;

            default:
                break;
        }
    }

    return true;  // Still editing
}

bool MenuNavigator::editValue(float* value, float min, float max, float step) {
    // Convert to integer for encoder
    int32_t int_value = (int32_t)(*value / step);
    int32_t int_min = (int32_t)(min / step);
    int32_t int_max = (int32_t)(max / step);

    bool result = editValue(&int_value, int_min, int_max, 1);

    *value = int_value * step;
    return result;
}

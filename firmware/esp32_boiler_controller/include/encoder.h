/**
 * @file encoder.h
 * @brief Rotary Encoder Input Module
 *
 * Handles KY-040 style rotary encoder with push button for menu navigation.
 * Features:
 * - Hardware interrupt-driven rotation detection
 * - Debounced button with short press, long press, and double press detection
 * - Acceleration for fast scrolling through long lists
 * - Thread-safe event queue
 */

#ifndef ENCODER_H
#define ENCODER_H

#include <Arduino.h>
#include "pin_definitions.h"

// ============================================================================
// ENCODER EVENTS
// ============================================================================

typedef enum {
    ENC_EVENT_NONE = 0,
    ENC_EVENT_CW,               // Clockwise rotation (down/increase)
    ENC_EVENT_CCW,              // Counter-clockwise rotation (up/decrease)
    ENC_EVENT_PRESS,            // Short button press
    ENC_EVENT_LONG_PRESS,       // Long button press (>1.5s)
    ENC_EVENT_DOUBLE_PRESS,     // Double press
    ENC_EVENT_RELEASE           // Button released
} encoder_event_t;

// ============================================================================
// ENCODER CALLBACK TYPE
// ============================================================================

typedef void (*encoder_callback_t)(encoder_event_t event, int32_t value);

// ============================================================================
// ENCODER CLASS
// ============================================================================

class RotaryEncoder {
public:
    /**
     * @brief Constructor
     * @param pin_a Encoder output A (CLK)
     * @param pin_b Encoder output B (DT)
     * @param pin_btn Push button pin
     */
    RotaryEncoder(uint8_t pin_a = ENCODER_PIN_A,
                  uint8_t pin_b = ENCODER_PIN_B,
                  uint8_t pin_btn = ENCODER_BUTTON_PIN);

    /**
     * @brief Initialize encoder hardware
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Process encoder events - call in main loop
     * Processes queued events and calls callbacks
     */
    void update();

    /**
     * @brief Get next event from queue
     * @return Event type (ENC_EVENT_NONE if no event)
     */
    encoder_event_t getEvent();

    /**
     * @brief Check if there are pending events
     * @return true if events available
     */
    bool hasEvent();

    /**
     * @brief Get current encoder position
     * @return Cumulative position value
     */
    int32_t getPosition();

    /**
     * @brief Set encoder position
     * @param pos New position value
     */
    void setPosition(int32_t pos);

    /**
     * @brief Reset position to zero
     */
    void resetPosition();

    /**
     * @brief Get delta since last read
     * @return Position change since last call
     */
    int32_t getDelta();

    /**
     * @brief Check if button is currently pressed
     * @return true if button pressed
     */
    bool isButtonPressed();

    /**
     * @brief Get button press duration
     * @return Duration in milliseconds (0 if not pressed)
     */
    uint32_t getButtonPressDuration();

    /**
     * @brief Set callback function for events
     * @param callback Function to call on events
     */
    void setCallback(encoder_callback_t callback);

    /**
     * @brief Enable/disable acceleration
     * @param enable True to enable acceleration
     * @param threshold Time between pulses for acceleration (ms)
     * @param multiplier Acceleration multiplier
     */
    void setAcceleration(bool enable, uint16_t threshold = 50, uint8_t multiplier = 4);

    /**
     * @brief Set value limits for bounded mode
     * @param min Minimum value
     * @param max Maximum value
     * @param wrap True to wrap around at limits
     */
    void setLimits(int32_t min, int32_t max, bool wrap = false);

    /**
     * @brief Clear value limits
     */
    void clearLimits();

    /**
     * @brief ISR handler for encoder rotation (called from ISR)
     */
    static void IRAM_ATTR handleEncoderISR(void* arg);

    /**
     * @brief ISR handler for button (called from ISR)
     */
    static void IRAM_ATTR handleButtonISR(void* arg);

private:
    // Pin configuration
    uint8_t _pin_a;
    uint8_t _pin_b;
    uint8_t _pin_btn;

    // Encoder state (volatile for ISR access)
    volatile int32_t _position;
    volatile int32_t _last_position;
    volatile uint8_t _last_state;
    volatile uint32_t _last_rotation_time;

    // Button state
    volatile bool _button_pressed;
    volatile uint32_t _button_press_time;
    volatile uint32_t _button_release_time;
    volatile uint8_t _press_count;
    bool _long_press_fired;
    bool _waiting_for_double;

    // Event queue
    static const int QUEUE_SIZE = 16;
    volatile encoder_event_t _event_queue[QUEUE_SIZE];
    volatile int _queue_head;
    volatile int _queue_tail;

    // Callback
    encoder_callback_t _callback;

    // Acceleration
    bool _acceleration_enabled;
    uint16_t _accel_threshold;
    uint8_t _accel_multiplier;

    // Limits
    bool _limits_enabled;
    int32_t _limit_min;
    int32_t _limit_max;
    bool _limit_wrap;

    // Internal methods
    void queueEvent(encoder_event_t event);
    void processButton();
    void applyLimits();
};

// ============================================================================
// MENU NAVIGATION HELPER CLASS
// ============================================================================

class MenuNavigator {
public:
    MenuNavigator(RotaryEncoder* encoder);

    /**
     * @brief Set menu bounds
     * @param item_count Number of items in current menu
     * @param wrap True to wrap at ends
     */
    void setMenu(int item_count, bool wrap = true);

    /**
     * @brief Get current selected index
     * @return Selected item index (0-based)
     */
    int getSelectedIndex();

    /**
     * @brief Set selected index
     * @param index Index to select
     */
    void setSelectedIndex(int index);

    /**
     * @brief Process navigation - call in loop
     * @return true if selection changed
     */
    bool update();

    /**
     * @brief Check if enter was pressed
     * @return true if enter pressed this update
     */
    bool enterPressed();

    /**
     * @brief Check if back was pressed (long press)
     * @return true if back pressed this update
     */
    bool backPressed();

    /**
     * @brief Check if double press occurred
     * @return true if double pressed this update
     */
    bool homePressed();

    /**
     * @brief Edit numeric value
     * @param value Pointer to value to edit
     * @param min Minimum value
     * @param max Maximum value
     * @param step Step size
     * @return true while editing, false when confirmed
     */
    bool editValue(int32_t* value, int32_t min, int32_t max, int32_t step = 1);

    /**
     * @brief Edit floating point value
     * @param value Pointer to value to edit
     * @param min Minimum value
     * @param max Maximum value
     * @param step Step size
     * @return true while editing, false when confirmed
     */
    bool editValue(float* value, float min, float max, float step = 0.1);

private:
    RotaryEncoder* _encoder;
    int _item_count;
    int _selected;
    bool _wrap;
    bool _enter_pressed;
    bool _back_pressed;
    bool _home_pressed;
    bool _editing;
};

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

extern RotaryEncoder encoder;
extern MenuNavigator menuNav;

#endif // ENCODER_H

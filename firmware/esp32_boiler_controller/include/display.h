/**
 * @file display.h
 * @brief Display Module for 20x4 LCD and WS2812 RGB LEDs
 *
 * Provides user interface display with:
 * - 20x4 I2C LCD for status and menu display
 * - WS2812 addressable RGB LEDs for visual indicators
 * - Bar graph display for conductivity deviation
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <FastLED.h>
#include "config.h"
#include "pin_definitions.h"

// ============================================================================
// DISPLAY SCREENS
// ============================================================================

typedef enum {
    SCREEN_MAIN = 0,        // Main status display
    SCREEN_CONDUCTIVITY,    // Conductivity details
    SCREEN_TEMPERATURE,     // Temperature display
    SCREEN_BLOWDOWN,        // Blowdown status
    SCREEN_PUMP1,           // H2SO3 pump status
    SCREEN_PUMP2,           // NaOH pump status
    SCREEN_PUMP3,           // Amine pump status
    SCREEN_WATER_METER1,    // Water meter 1
    SCREEN_WATER_METER2,    // Water meter 2
    SCREEN_ALARMS,          // Active alarms
    SCREEN_NETWORK,         // WiFi/Network status
    SCREEN_MENU,            // Menu display
    SCREEN_COUNT
} display_screen_t;

// ============================================================================
// LED COLORS
// ============================================================================

#define COLOR_OFF           CRGB::Black
#define COLOR_GREEN         CRGB::Green
#define COLOR_RED           CRGB::Red
#define COLOR_YELLOW        CRGB::Yellow
#define COLOR_BLUE          CRGB::Blue
#define COLOR_CYAN          CRGB::Cyan
#define COLOR_MAGENTA       CRGB::Magenta
#define COLOR_WHITE         CRGB::White
#define COLOR_ORANGE        CRGB::Orange

// ============================================================================
// CUSTOM LCD CHARACTERS
// ============================================================================

#define CHAR_BAR_FULL       0
#define CHAR_BAR_HALF       1
#define CHAR_BAR_EMPTY      2
#define CHAR_ARROW_UP       3
#define CHAR_ARROW_DOWN     4
#define CHAR_PUMP_ON        5
#define CHAR_PUMP_OFF       6
#define CHAR_ALARM          7

// ============================================================================
// DISPLAY CLASS
// ============================================================================

class Display {
public:
    Display();

    /**
     * @brief Initialize display hardware
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Update display - call in loop
     */
    void update();

    /**
     * @brief Set current display screen
     * @param screen Screen to display
     */
    void setScreen(display_screen_t screen);

    /**
     * @brief Go to next screen
     */
    void nextScreen();

    /**
     * @brief Go to previous screen
     */
    void prevScreen();

    /**
     * @brief Get current screen
     * @return Current screen
     */
    display_screen_t getScreen();

    /**
     * @brief Show temporary message (overlays current screen)
     * @param line1 First line text
     * @param line2 Second line text (optional)
     * @param duration_ms Display duration
     */
    void showMessage(const char* line1, const char* line2 = nullptr,
                    uint32_t duration_ms = 2000);

    /**
     * @brief Show alarm message (flashing)
     * @param message Alarm text
     */
    void showAlarm(const char* message);

    /**
     * @brief Clear alarm display
     */
    void clearAlarm();

    /**
     * @brief Update LED indicators
     */
    void updateLEDs();

    /**
     * @brief Set specific LED color
     * @param index LED index
     * @param color Color value
     */
    void setLED(uint8_t index, CRGB color);

    /**
     * @brief Set LED brightness
     * @param brightness Brightness (0-255)
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Set backlight state
     * @param on True to turn on
     */
    void setBacklight(bool on);

    /**
     * @brief Display conductivity bar graph
     * @param row LCD row (0-3)
     * @param value Current value
     * @param setpoint Setpoint value
     * @param range Display range (+/- percentage)
     */
    void drawBarGraph(uint8_t row, float value, float setpoint, uint8_t range = 20);

    /**
     * @brief Display progress bar
     * @param row LCD row
     * @param percent Percentage (0-100)
     */
    void drawProgressBar(uint8_t row, uint8_t percent);

    /**
     * @brief Print formatted value with units
     * @param col Column position
     * @param row Row position
     * @param value Numeric value
     * @param decimals Decimal places
     * @param unit Unit string
     */
    void printValue(uint8_t col, uint8_t row, float value,
                   uint8_t decimals, const char* unit);

    /**
     * @brief Clear LCD screen
     */
    void clear();

    /**
     * @brief Set cursor position
     * @param col Column
     * @param row Row
     */
    void setCursor(uint8_t col, uint8_t row);

    /**
     * @brief Print text at current cursor
     * @param text Text to print
     */
    void print(const char* text);

    /**
     * @brief Print formatted text
     * @param format Format string
     * @param ... Arguments
     */
    void printf(const char* format, ...);

private:
    LiquidCrystal_I2C _lcd;
    CRGB _leds[WS2812_NUM_LEDS];

    display_screen_t _current_screen;
    uint32_t _last_update;
    uint32_t _message_end_time;
    bool _showing_message;
    char _message_line1[21];
    char _message_line2[21];

    bool _alarm_active;
    bool _alarm_flash_state;
    uint32_t _alarm_flash_time;
    char _alarm_message[21];

    // Screen drawing methods
    void drawMainScreen();
    void drawConductivityScreen();
    void drawTemperatureScreen();
    void drawBlowdownScreen();
    void drawPumpScreen(uint8_t pump_index);
    void drawWaterMeterScreen(uint8_t meter_index);
    void drawAlarmsScreen();
    void drawNetworkScreen();

    // LED update methods
    void updatePowerLED();
    void updateWiFiLED();
    void updateConductivityLED();
    void updateBlowdownLED();
    void updatePumpLEDs();
    void updateAlarmLED();

    // Custom character creation
    void createCustomChars();

    // Helper methods
    void padRight(char* str, uint8_t length);
    CRGB conductivityToColor(float value, float setpoint, float deadband);
};

extern Display display;

#endif // DISPLAY_H

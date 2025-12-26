/**
 * @file display.cpp
 * @brief Display Module Implementation
 */

#include "display.h"
#include "conductivity.h"
#include "chemical_pump.h"
#include "water_meter.h"
#include "blowdown.h"
#include "data_logger.h"
#include <stdarg.h>

// Global instance
Display display;

// External references (will be defined in main)
extern ConductivitySensor conductivitySensor;
extern system_config_t systemConfig;
extern system_state_t_runtime systemState;

// Custom character bitmaps
static const uint8_t char_bar_full[8] = {
    0b11111, 0b11111, 0b11111, 0b11111,
    0b11111, 0b11111, 0b11111, 0b11111
};

static const uint8_t char_bar_half[8] = {
    0b11100, 0b11100, 0b11100, 0b11100,
    0b11100, 0b11100, 0b11100, 0b11100
};

static const uint8_t char_bar_empty[8] = {
    0b00000, 0b00000, 0b00000, 0b00000,
    0b00000, 0b00000, 0b00000, 0b00000
};

static const uint8_t char_arrow_up[8] = {
    0b00100, 0b01110, 0b11111, 0b00100,
    0b00100, 0b00100, 0b00100, 0b00000
};

static const uint8_t char_arrow_down[8] = {
    0b00100, 0b00100, 0b00100, 0b00100,
    0b11111, 0b01110, 0b00100, 0b00000
};

static const uint8_t char_pump_on[8] = {
    0b01110, 0b10001, 0b10001, 0b01110,
    0b00100, 0b00100, 0b01110, 0b00000
};

static const uint8_t char_pump_off[8] = {
    0b01110, 0b10001, 0b10001, 0b01110,
    0b00000, 0b00000, 0b00000, 0b00000
};

static const uint8_t char_alarm[8] = {
    0b00100, 0b01010, 0b01010, 0b10001,
    0b10001, 0b11111, 0b00100, 0b00000
};

// ============================================================================
// DISPLAY IMPLEMENTATION
// ============================================================================

Display::Display()
    : _lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS)
    , _current_screen(SCREEN_MAIN)
    , _last_update(0)
    , _message_end_time(0)
    , _showing_message(false)
    , _alarm_active(false)
    , _alarm_flash_state(false)
    , _alarm_flash_time(0)
{
    memset(_message_line1, 0, sizeof(_message_line1));
    memset(_message_line2, 0, sizeof(_message_line2));
    memset(_alarm_message, 0, sizeof(_alarm_message));
}

bool Display::begin() {
    // Initialize LCD
    _lcd.init();
    _lcd.backlight();
    _lcd.clear();

    // Create custom characters
    createCustomChars();

    // Initialize LEDs
    FastLED.addLeds<WS2812, WS2812_DATA_PIN, GRB>(_leds, WS2812_NUM_LEDS);
    FastLED.setBrightness(LED_BRIGHTNESS);
    FastLED.clear();
    FastLED.show();

    // Show startup message
    _lcd.setCursor(2, 1);
    _lcd.print("CT-6 Boiler");
    _lcd.setCursor(4, 2);
    _lcd.print("Controller");

    // Startup LED animation
    for (int i = 0; i < WS2812_NUM_LEDS; i++) {
        _leds[i] = CRGB::Blue;
        FastLED.show();
        delay(100);
    }
    delay(500);

    // Clear and set to main screen
    _lcd.clear();
    FastLED.clear();
    FastLED.show();

    Serial.println("Display initialized");
    return true;
}

void Display::update() {
    uint32_t now = millis();

    // Check message timeout
    if (_showing_message && now >= _message_end_time) {
        _showing_message = false;
        _lcd.clear();
    }

    // Handle alarm flashing
    if (_alarm_active && now - _alarm_flash_time >= 500) {
        _alarm_flash_state = !_alarm_flash_state;
        _alarm_flash_time = now;
    }

    // Update display at configured rate
    if (now - _last_update < 200) return;
    _last_update = now;

    // Update LEDs
    updateLEDs();

    // If showing message, don't update screen
    if (_showing_message) return;

    // Draw current screen
    switch (_current_screen) {
        case SCREEN_MAIN:
            drawMainScreen();
            break;
        case SCREEN_CONDUCTIVITY:
            drawConductivityScreen();
            break;
        case SCREEN_TEMPERATURE:
            drawTemperatureScreen();
            break;
        case SCREEN_BLOWDOWN:
            drawBlowdownScreen();
            break;
        case SCREEN_PUMP1:
            drawPumpScreen(0);
            break;
        case SCREEN_PUMP2:
            drawPumpScreen(1);
            break;
        case SCREEN_PUMP3:
            drawPumpScreen(2);
            break;
        case SCREEN_WATER_METER1:
            drawWaterMeterScreen(0);
            break;
        case SCREEN_WATER_METER2:
            drawWaterMeterScreen(1);
            break;
        case SCREEN_ALARMS:
            drawAlarmsScreen();
            break;
        case SCREEN_NETWORK:
            drawNetworkScreen();
            break;
        default:
            break;
    }
}

void Display::setScreen(display_screen_t screen) {
    if (screen < SCREEN_COUNT) {
        _current_screen = screen;
        _lcd.clear();
    }
}

void Display::nextScreen() {
    _current_screen = (display_screen_t)((_current_screen + 1) % SCREEN_COUNT);
    _lcd.clear();
}

void Display::prevScreen() {
    if (_current_screen == 0) {
        _current_screen = (display_screen_t)(SCREEN_COUNT - 1);
    } else {
        _current_screen = (display_screen_t)(_current_screen - 1);
    }
    _lcd.clear();
}

display_screen_t Display::getScreen() {
    return _current_screen;
}

void Display::showMessage(const char* line1, const char* line2, uint32_t duration_ms) {
    _showing_message = true;
    _message_end_time = millis() + duration_ms;

    strncpy(_message_line1, line1, 20);
    _message_line1[20] = '\0';

    if (line2) {
        strncpy(_message_line2, line2, 20);
        _message_line2[20] = '\0';
    } else {
        _message_line2[0] = '\0';
    }

    _lcd.clear();
    _lcd.setCursor((20 - strlen(_message_line1)) / 2, 1);
    _lcd.print(_message_line1);

    if (line2) {
        _lcd.setCursor((20 - strlen(_message_line2)) / 2, 2);
        _lcd.print(_message_line2);
    }
}

void Display::showAlarm(const char* message) {
    _alarm_active = true;
    strncpy(_alarm_message, message, 20);
    _alarm_message[20] = '\0';
    _alarm_flash_time = millis();
}

void Display::clearAlarm() {
    _alarm_active = false;
    _alarm_message[0] = '\0';
}

void Display::updateLEDs() {
    updatePowerLED();
    updateWiFiLED();
    updateConductivityLED();
    updateBlowdownLED();
    updatePumpLEDs();
    updateAlarmLED();
    FastLED.show();
}

void Display::setLED(uint8_t index, CRGB color) {
    if (index < WS2812_NUM_LEDS) {
        _leds[index] = color;
    }
}

void Display::setBrightness(uint8_t brightness) {
    FastLED.setBrightness(brightness);
}

void Display::setBacklight(bool on) {
    if (on) {
        _lcd.backlight();
    } else {
        _lcd.noBacklight();
    }
}

void Display::drawBarGraph(uint8_t row, float value, float setpoint, uint8_t range) {
    // Calculate deviation percentage
    float deviation = ((value - setpoint) / setpoint) * 100.0;

    // Clamp to range
    if (deviation < -range) deviation = -range;
    if (deviation > range) deviation = range;

    // Draw bar graph (20 characters, center is setpoint)
    _lcd.setCursor(0, row);

    for (int i = 0; i < 20; i++) {
        int pos_percent = ((i - 10) * range) / 10;

        if (i == 10) {
            _lcd.print('S');  // Setpoint marker
        } else if ((deviation >= 0 && pos_percent > 0 && pos_percent <= deviation) ||
                   (deviation < 0 && pos_percent < 0 && pos_percent >= deviation)) {
            _lcd.write(CHAR_BAR_FULL);
        } else {
            _lcd.print('-');
        }
    }
}

void Display::drawProgressBar(uint8_t row, uint8_t percent) {
    _lcd.setCursor(0, row);
    _lcd.print('[');

    int filled = (percent * 18) / 100;
    for (int i = 0; i < 18; i++) {
        if (i < filled) {
            _lcd.write(CHAR_BAR_FULL);
        } else {
            _lcd.print(' ');
        }
    }

    _lcd.print(']');
}

void Display::printValue(uint8_t col, uint8_t row, float value,
                        uint8_t decimals, const char* unit) {
    char buffer[21];
    _lcd.setCursor(col, row);
    snprintf(buffer, sizeof(buffer), "%.*f%s", decimals, value, unit);
    _lcd.print(buffer);
}

void Display::clear() {
    _lcd.clear();
}

void Display::setCursor(uint8_t col, uint8_t row) {
    _lcd.setCursor(col, row);
}

void Display::print(const char* text) {
    _lcd.print(text);
}

void Display::printf(const char* format, ...) {
    char buffer[41];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    _lcd.print(buffer);
}

// ============================================================================
// PRIVATE METHODS - SCREEN DRAWING
// ============================================================================

void Display::drawMainScreen() {
    char line[21];

    // Row 0: Conductivity with bar graph indicator
    _lcd.setCursor(0, 0);
    snprintf(line, sizeof(line), "Cond: %5.0f uS/cm",
             conductivitySensor.getLastReading().calibrated);
    _lcd.print(line);

    // Row 1: Temperature and blowdown status
    _lcd.setCursor(0, 1);
    snprintf(line, sizeof(line), "Temp: %5.1fC  BD:%s",
             conductivitySensor.getLastReading().temperature_c,
             blowdownController.isActive() ? "ON " : "OFF");
    _lcd.print(line);

    // Row 2: Pump status
    _lcd.setCursor(0, 2);
    _lcd.print("Pumps:");
    ChemicalPump* p1 = pumpManager.getPump(PUMP_H2SO3);
    ChemicalPump* p2 = pumpManager.getPump(PUMP_NAOH);
    ChemicalPump* p3 = pumpManager.getPump(PUMP_AMINE);
    snprintf(line, sizeof(line), " H%c N%c A%c",
             p1 && p1->isRunning() ? '+' : '-',
             p2 && p2->isRunning() ? '+' : '-',
             p3 && p3->isRunning() ? '+' : '-');
    _lcd.print(line);

    // Row 3: Alarm or status
    _lcd.setCursor(0, 3);
    if (_alarm_active && _alarm_flash_state) {
        snprintf(line, sizeof(line), "! %-18s", _alarm_message);
    } else {
        snprintf(line, sizeof(line), "WM1:%6lu gal",
                 waterMeterManager.getMeter(0)->getTotalVolume());
    }
    _lcd.print(line);
}

void Display::drawConductivityScreen() {
    char line[21];
    conductivity_reading_t reading = conductivitySensor.getLastReading();

    _lcd.setCursor(0, 0);
    _lcd.print("== Conductivity ==");

    _lcd.setCursor(0, 1);
    snprintf(line, sizeof(line), "Raw:   %7.1f uS", reading.raw_conductivity);
    _lcd.print(line);

    _lcd.setCursor(0, 2);
    snprintf(line, sizeof(line), "Comp:  %7.1f uS", reading.temp_compensated);
    _lcd.print(line);

    _lcd.setCursor(0, 3);
    snprintf(line, sizeof(line), "Final: %7.1f uS", reading.calibrated);
    _lcd.print(line);
}

void Display::drawTemperatureScreen() {
    char line[21];
    conductivity_reading_t reading = conductivitySensor.getLastReading();

    _lcd.setCursor(0, 0);
    _lcd.print("== Temperature ==");

    _lcd.setCursor(0, 1);
    snprintf(line, sizeof(line), "Celsius:    %6.1f C", reading.temperature_c);
    _lcd.print(line);

    _lcd.setCursor(0, 2);
    snprintf(line, sizeof(line), "Fahrenheit: %6.1f F", reading.temperature_f);
    _lcd.print(line);

    _lcd.setCursor(0, 3);
    snprintf(line, sizeof(line), "Sensor: %s",
             reading.temp_sensor_ok ? "OK" : "ERROR");
    _lcd.print(line);
}

void Display::drawBlowdownScreen() {
    char line[21];
    blowdown_status_t status = blowdownController.getStatus();

    _lcd.setCursor(0, 0);
    _lcd.print("=== Blowdown ===");

    _lcd.setCursor(0, 1);
    snprintf(line, sizeof(line), "State: %s",
             status.valve_open ? "OPEN" : "CLOSED");
    _lcd.print(line);

    _lcd.setCursor(0, 2);
    snprintf(line, sizeof(line), "Time: %lu sec",
             status.current_blowdown_time / 1000);
    _lcd.print(line);

    _lcd.setCursor(0, 3);
    snprintf(line, sizeof(line), "Total: %lu sec",
             status.total_blowdown_time);
    _lcd.print(line);
}

void Display::drawPumpScreen(uint8_t pump_index) {
    char line[21];
    ChemicalPump* pump = pumpManager.getPump((pump_id_t)pump_index);
    if (!pump) return;

    pump_status_t status = pump->getStatus();

    _lcd.setCursor(0, 0);
    snprintf(line, sizeof(line), "=== Pump %s ===", pump->getName());
    _lcd.print(line);

    _lcd.setCursor(0, 1);
    snprintf(line, sizeof(line), "State: %s",
             status.running ? "RUNNING" : "IDLE");
    _lcd.print(line);

    _lcd.setCursor(0, 2);
    snprintf(line, sizeof(line), "Runtime: %lu sec",
             status.total_runtime_ms / 1000);
    _lcd.print(line);

    _lcd.setCursor(0, 3);
    snprintf(line, sizeof(line), "Volume: %.1f ml",
             status.volume_dispensed_ml);
    _lcd.print(line);
}

void Display::drawWaterMeterScreen(uint8_t meter_index) {
    char line[21];
    WaterMeter* meter = waterMeterManager.getMeter(meter_index);
    if (!meter) return;

    _lcd.setCursor(0, 0);
    snprintf(line, sizeof(line), "== Water Meter %d ==", meter_index + 1);
    _lcd.print(line);

    _lcd.setCursor(0, 1);
    snprintf(line, sizeof(line), "Type: %s", meter->getTypeName());
    _lcd.print(line);

    _lcd.setCursor(0, 2);
    snprintf(line, sizeof(line), "Total: %lu gal", meter->getTotalVolume());
    _lcd.print(line);

    _lcd.setCursor(0, 3);
    snprintf(line, sizeof(line), "Flow: %.2f GPM", meter->getFlowRate());
    _lcd.print(line);
}

void Display::drawAlarmsScreen() {
    _lcd.setCursor(0, 0);
    _lcd.print("=== Alarms ===");

    // For now, just show alarm count
    _lcd.setCursor(0, 1);
    if (_alarm_active) {
        _lcd.print(_alarm_message);
    } else {
        _lcd.print("No active alarms");
    }
}

void Display::drawNetworkScreen() {
    char line[21];

    _lcd.setCursor(0, 0);
    _lcd.print("=== Network ===");

    _lcd.setCursor(0, 1);
    snprintf(line, sizeof(line), "WiFi: %s",
             dataLogger.isWiFiConnected() ? "Connected" : "Disconnected");
    _lcd.print(line);

    _lcd.setCursor(0, 2);
    if (dataLogger.isWiFiConnected()) {
        snprintf(line, sizeof(line), "RSSI: %d dBm", dataLogger.getWiFiRSSI());
    } else {
        snprintf(line, sizeof(line), "RSSI: N/A");
    }
    _lcd.print(line);

    _lcd.setCursor(0, 3);
    snprintf(line, sizeof(line), "Server: %s",
             dataLogger.isServerConnected() ? "OK" : "Error");
    _lcd.print(line);
}

// ============================================================================
// PRIVATE METHODS - LED UPDATES
// ============================================================================

void Display::updatePowerLED() {
    // Green when OK, red on any error
    _leds[LED_POWER] = COLOR_GREEN;
}

void Display::updateWiFiLED() {
    if (dataLogger.isWiFiConnected()) {
        _leds[LED_WIFI] = COLOR_BLUE;
    } else if (dataLogger.isAPMode()) {
        _leds[LED_WIFI] = COLOR_YELLOW;
    } else {
        _leds[LED_WIFI] = COLOR_OFF;
    }
}

void Display::updateConductivityLED() {
    conductivity_reading_t reading = conductivitySensor.getLastReading();
    if (reading.sensor_ok) {
        // Color based on relation to setpoint
        _leds[LED_CONDUCTIVITY] = conductivityToColor(
            reading.calibrated,
            systemConfig.blowdown.setpoint,
            systemConfig.blowdown.deadband
        );
    } else {
        // Flash red for sensor error
        _leds[LED_CONDUCTIVITY] = _alarm_flash_state ? COLOR_RED : COLOR_OFF;
    }
}

void Display::updateBlowdownLED() {
    if (blowdownController.isActive()) {
        _leds[LED_BLOWDOWN] = COLOR_YELLOW;
    } else if (blowdownController.isTimeout()) {
        _leds[LED_BLOWDOWN] = _alarm_flash_state ? COLOR_RED : COLOR_OFF;
    } else {
        _leds[LED_BLOWDOWN] = COLOR_OFF;
    }
}

void Display::updatePumpLEDs() {
    ChemicalPump* pumps[3] = {
        pumpManager.getPump(PUMP_H2SO3),
        pumpManager.getPump(PUMP_NAOH),
        pumpManager.getPump(PUMP_AMINE)
    };

    CRGB colors[3] = {COLOR_CYAN, COLOR_MAGENTA, COLOR_YELLOW};

    for (int i = 0; i < 3; i++) {
        if (pumps[i] && pumps[i]->isRunning()) {
            _leds[LED_PUMP1_H2SO3 + i] = colors[i];
        } else {
            _leds[LED_PUMP1_H2SO3 + i] = COLOR_OFF;
        }
    }
}

void Display::updateAlarmLED() {
    if (_alarm_active) {
        _leds[LED_ALARM] = _alarm_flash_state ? COLOR_RED : COLOR_OFF;
    } else {
        _leds[LED_ALARM] = COLOR_OFF;
    }
}

// ============================================================================
// PRIVATE METHODS - HELPERS
// ============================================================================

void Display::createCustomChars() {
    _lcd.createChar(CHAR_BAR_FULL, (uint8_t*)char_bar_full);
    _lcd.createChar(CHAR_BAR_HALF, (uint8_t*)char_bar_half);
    _lcd.createChar(CHAR_BAR_EMPTY, (uint8_t*)char_bar_empty);
    _lcd.createChar(CHAR_ARROW_UP, (uint8_t*)char_arrow_up);
    _lcd.createChar(CHAR_ARROW_DOWN, (uint8_t*)char_arrow_down);
    _lcd.createChar(CHAR_PUMP_ON, (uint8_t*)char_pump_on);
    _lcd.createChar(CHAR_PUMP_OFF, (uint8_t*)char_pump_off);
    _lcd.createChar(CHAR_ALARM, (uint8_t*)char_alarm);
}

void Display::padRight(char* str, uint8_t length) {
    size_t len = strlen(str);
    while (len < length) {
        str[len++] = ' ';
    }
    str[length] = '\0';
}

CRGB Display::conductivityToColor(float value, float setpoint, float deadband) {
    float deviation = value - setpoint;
    float percent = deviation / setpoint * 100.0;

    if (percent > 20) {
        return COLOR_RED;
    } else if (percent > 10) {
        return COLOR_ORANGE;
    } else if (percent > 5) {
        return COLOR_YELLOW;
    } else if (percent < -10) {
        return COLOR_BLUE;
    } else {
        return COLOR_GREEN;
    }
}

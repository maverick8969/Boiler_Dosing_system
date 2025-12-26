/**
 * @file water_meter.cpp
 * @brief Water Meter Input Implementation
 */

#include "water_meter.h"
#include "pin_definitions.h"
#include <Preferences.h>

// Global instance
WaterMeterManager waterMeterManager;

// Static ISR data
static volatile uint32_t meter_pulse_counts[2] = {0, 0};
static volatile uint32_t meter_last_pulse_times[2] = {0, 0};

// ============================================================================
// WATER METER IMPLEMENTATION
// ============================================================================

WaterMeter::WaterMeter(uint8_t pin, uint8_t meter_id)
    : _pin(pin)
    , _meter_id(meter_id)
    , _config(nullptr)
    , _pulse_count(0)
    , _last_pulse_time(0)
    , _last_flow_calc_time(0)
    , _last_flow_pulse_count(0)
    , _flow_rate(0)
    , _last_query_pulse_count(0)
    , _last_query_volume(0)
    , _debounce_time(WATER_METER_DEBOUNCE_MS)
{
}

bool WaterMeter::begin() {
    // Configure pin with internal pull-up
    pinMode(_pin, INPUT_PULLUP);

    // Attach interrupt
    attachInterruptArg(digitalPinToInterrupt(_pin), handleInterrupt,
                       (void*)(intptr_t)_meter_id, FALLING);

    Serial.printf("Water Meter %d initialized on pin %d\n", _meter_id, _pin);
    return true;
}

void WaterMeter::configure(water_meter_config_t* config) {
    _config = config;

    if (_config) {
        // Load totalizer from config
        _pulse_count = 0;  // Pulses since boot
        _last_query_pulse_count = 0;
        _last_query_volume = 0;
    }
}

void WaterMeter::update() {
    // Copy volatile pulse count
    noInterrupts();
    uint32_t current_pulses = meter_pulse_counts[_meter_id];
    uint32_t last_time = meter_last_pulse_times[_meter_id];
    interrupts();

    _pulse_count = current_pulses;
    _last_pulse_time = last_time;

    // Calculate flow rate (every second)
    uint32_t now = millis();
    if (now - _last_flow_calc_time >= 1000) {
        uint32_t delta_pulses = _pulse_count - _last_flow_pulse_count;
        float delta_time_min = (now - _last_flow_calc_time) / 60000.0;

        if (delta_time_min > 0 && _config) {
            float volume = pulsesToVolume(delta_pulses);
            _flow_rate = volume / delta_time_min;  // Units per minute
        }

        _last_flow_pulse_count = _pulse_count;
        _last_flow_calc_time = now;
    }

    // Update totalizer
    if (_config) {
        float current_volume = pulsesToVolume(_pulse_count);
        uint32_t volume_delta = (uint32_t)(current_volume - _last_query_volume);
        _config->totalizer += volume_delta;

        // Wrap at maximum
        if (_config->totalizer > METER_TOTALIZER_MAX) {
            _config->totalizer = 0;
        }
    }
}

uint32_t WaterMeter::getTotalVolume() {
    if (!_config) return 0;
    return _config->totalizer;
}

float WaterMeter::getFlowRate() {
    return _flow_rate;
}

uint32_t WaterMeter::getPulseCount() {
    return _pulse_count;
}

uint32_t WaterMeter::getContactsSinceLast() {
    uint32_t delta = _pulse_count - _last_query_pulse_count;
    _last_query_pulse_count = _pulse_count;
    return delta;
}

float WaterMeter::getVolumeSinceLast() {
    float current = pulsesToVolume(_pulse_count);
    float delta = current - _last_query_volume;
    _last_query_volume = current;
    return delta;
}

void WaterMeter::resetTotal() {
    if (_config) {
        _config->totalizer = 0;
        _config->last_reset_time = millis();
    }

    noInterrupts();
    meter_pulse_counts[_meter_id] = 0;
    interrupts();

    _pulse_count = 0;
    _last_query_pulse_count = 0;
    _last_query_volume = 0;
    _last_flow_pulse_count = 0;
}

void WaterMeter::saveToNVS() {
    Preferences prefs;
    char key[16];

    prefs.begin(NVS_NAMESPACE, false);
    snprintf(key, sizeof(key), "wm%d_total", _meter_id);
    prefs.putUInt(key, _config ? _config->totalizer : 0);
    prefs.end();
}

void WaterMeter::loadFromNVS() {
    Preferences prefs;
    char key[16];

    prefs.begin(NVS_NAMESPACE, true);
    snprintf(key, sizeof(key), "wm%d_total", _meter_id);
    uint32_t total = prefs.getUInt(key, 0);
    prefs.end();

    if (_config) {
        _config->totalizer = total;
    }
}

const char* WaterMeter::getTypeName() {
    if (!_config) return "Disabled";

    switch (_config->type) {
        case METER_TYPE_CONTACTOR: return "Contactor";
        case METER_TYPE_PADDLEWHEEL: return "Paddlewheel";
        default: return "Disabled";
    }
}

bool WaterMeter::isEnabled() {
    return _config && _config->type != METER_TYPE_DISABLED;
}

void IRAM_ATTR WaterMeter::handleInterrupt(void* arg) {
    uint8_t meter_id = (uint8_t)(intptr_t)arg;
    if (meter_id >= 2) return;

    uint32_t now = millis();

    // Simple debounce
    if (now - meter_last_pulse_times[meter_id] >= WATER_METER_DEBOUNCE_MS) {
        meter_pulse_counts[meter_id]++;
        meter_last_pulse_times[meter_id] = now;
    }
}

float WaterMeter::pulsesToVolume(uint32_t pulses) {
    if (!_config || pulses == 0) return 0;

    switch (_config->type) {
        case METER_TYPE_CONTACTOR:
            return pulses * _config->volume_per_contact;

        case METER_TYPE_PADDLEWHEEL:
            if (_config->k_factor > 0) {
                return pulses / _config->k_factor;
            }
            return 0;

        default:
            return 0;
    }
}

// ============================================================================
// WATER METER MANAGER IMPLEMENTATION
// ============================================================================

WaterMeterManager::WaterMeterManager() {
    _meters[0] = new WaterMeter(WATER_METER_PIN, 0);
    // Second meter on different pin if available
    _meters[1] = new WaterMeter(GPIO_NUM_35, 1);  // Using flow switch pin for now
}

bool WaterMeterManager::begin() {
    bool success = true;

    // Only initialize first meter by default
    if (!_meters[0]->begin()) {
        success = false;
    }

    // Second meter is optional
    // _meters[1]->begin();

    return success;
}

void WaterMeterManager::configure(water_meter_config_t configs[2]) {
    _meters[0]->configure(&configs[0]);
    _meters[1]->configure(&configs[1]);
}

void WaterMeterManager::update() {
    _meters[0]->update();
    _meters[1]->update();
}

WaterMeter* WaterMeterManager::getMeter(uint8_t id) {
    if (id >= 2) return nullptr;
    return _meters[id];
}

uint32_t WaterMeterManager::getTotalVolume() {
    return _meters[0]->getTotalVolume() + _meters[1]->getTotalVolume();
}

float WaterMeterManager::getCombinedFlowRate() {
    return _meters[0]->getFlowRate() + _meters[1]->getFlowRate();
}

uint32_t WaterMeterManager::getContactsSinceLast(uint8_t meter_select) {
    switch (meter_select) {
        case 0: return _meters[0]->getContactsSinceLast();
        case 1: return _meters[1]->getContactsSinceLast();
        case 2: return _meters[0]->getContactsSinceLast() +
                       _meters[1]->getContactsSinceLast();
        default: return 0;
    }
}

float WaterMeterManager::getVolumeSinceLast(uint8_t meter_select) {
    switch (meter_select) {
        case 0: return _meters[0]->getVolumeSinceLast();
        case 1: return _meters[1]->getVolumeSinceLast();
        case 2: return _meters[0]->getVolumeSinceLast() +
                       _meters[1]->getVolumeSinceLast();
        default: return 0;
    }
}

void WaterMeterManager::saveAllToNVS() {
    _meters[0]->saveToNVS();
    _meters[1]->saveToNVS();
}

void WaterMeterManager::loadAllFromNVS() {
    _meters[0]->loadFromNVS();
    _meters[1]->loadFromNVS();
}

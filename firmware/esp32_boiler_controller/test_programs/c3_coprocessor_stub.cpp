/**
 * @file c3_coprocessor_stub.cpp
 * @brief ESP32 DevKit coprocessor: RS-485 (auto-direction), EZO-EC, internal ADC (valve), telemetry
 *
 * Target: ESP32 DevKit (esp32dev). RS-485 on Serial2 (GPIO16/17), no DE pin.
 * EZO-EC on Serial1 (GPIO9/10). Valve 4–20 mA from internal ADC (GPIO36). No ADS1115.
 *
 * Build: pio run -e esp32dev_coprocessor
 */

#include <Arduino.h>
#include "../include/coprocessor_protocol.h"
#include "../include/c3_pin_definitions.h"

#define C3_TELEMETRY_HZ  5
#define C3_MAIN_HEARTBEAT_TIMEOUT_MS  3000
#define C3_EZO_POLL_INTERVAL_MS  1000
#define C3_EZO_RESPONSE_TIMEOUT_MS  800
#define C3_VALVE_FAULT_LOW_MA  3.0f
#define ADC_12BIT_MAX  4095.0f
#define ADC_VREF      3.3f

HardwareSerial C3Serial(2);  // Serial2 = RS-485 (GPIO16 RX, GPIO17 TX)

static uint8_t s_rx_buf[CP_MAX_FRAME];
static size_t s_rx_len = 0;
static uint16_t s_telemetry_sequence = 0;
static uint8_t s_blowdown_open = 0;
static uint8_t s_solenoid_on = 0;
static uint32_t s_last_main_frame_ms = 0;
static uint32_t s_last_telemetry_ms = 0;

static float s_cached_conductivity_uS_cm = 1500.0f;
static float s_cached_temperature_c = 25.0f;
static uint8_t s_ezo_ready = 0;
static uint8_t s_ezo_poll_state = 0;
static uint32_t s_ezo_poll_start_ms = 0;
static String s_ezo_response;

static float s_cached_valve_feedback_mA = 4.0f;

static void internal_adc_poll_valve() {
    int raw = analogRead(C3_ADC_VALVE_PIN);
    float v = (raw / ADC_12BIT_MAX) * ADC_VREF;
    float mA = (v / C3_4_20MA_R_SENSE) * 1000.0f;
    if (mA < 0.0f) mA = 0.0f;
    if (mA > 25.0f) mA = 25.0f;
    s_cached_valve_feedback_mA = mA;
}

static void set_de_re(bool drive) {
    if (C3_RS485_DE_RE_PIN >= 0)
        digitalWrite(C3_RS485_DE_RE_PIN, drive ? HIGH : LOW);
}

static void send_frame(uint8_t type, const uint8_t* payload, uint8_t plen) {
    if (plen > CP_MAX_PAYLOAD) return;
    uint8_t frame[CP_MAX_FRAME];
    frame[0] = CP_SYNC_0;
    frame[1] = CP_SYNC_1;
    frame[2] = type;
    frame[3] = plen;
    if (plen && payload) memcpy(frame + CP_HEADER_SIZE, payload, plen);
    uint16_t crc = cp_crc16(frame, CP_HEADER_SIZE + plen);
    frame[CP_HEADER_SIZE + plen]     = (uint8_t)(crc & 0xFF);
    frame[CP_HEADER_SIZE + plen + 1] = (uint8_t)(crc >> 8);
    set_de_re(true);
    delay(1);
    C3Serial.write(frame, CP_HEADER_SIZE + plen + CP_CRC_SIZE);
    C3Serial.flush();
    delay(2);
    set_de_re(false);
}

static void process_rx_frame(const uint8_t* frame, size_t len) {
    if (!cp_frame_valid(frame, len)) return;
    uint8_t type = cp_frame_type(frame);
    uint8_t plen = cp_frame_payload_len(frame);
    const uint8_t* pl = cp_frame_payload(frame);
    s_last_main_frame_ms = millis();

    switch (type) {
    case CP_TYPE_CMD_BLOWDOWN_OPEN: {
        s_blowdown_open = 1;
        cp_ack_nak_payload_t ack;
        ack.ack_sequence = plen >= 2 ? (uint16_t)pl[0] | ((uint16_t)pl[1] << 8) : 0;
        ack.result = 0;
        send_frame(CP_TYPE_ACK, (const uint8_t*)&ack, sizeof(ack));
        break;
    }
    case CP_TYPE_CMD_BLOWDOWN_CLOSE: {
        s_blowdown_open = 0;
        cp_ack_nak_payload_t ack;
        ack.ack_sequence = plen >= 2 ? (uint16_t)pl[0] | ((uint16_t)pl[1] << 8) : 0;
        ack.result = 0;
        send_frame(CP_TYPE_ACK, (const uint8_t*)&ack, sizeof(ack));
        break;
    }
    case CP_TYPE_CMD_SOLENOID:
        if (plen >= 3) s_solenoid_on = pl[2] ? 1 : 0;
        else s_solenoid_on = 0;
        {
            cp_ack_nak_payload_t ack;
            ack.ack_sequence = plen >= 2 ? (uint16_t)pl[0] | ((uint16_t)pl[1] << 8) : 0;
            ack.result = 0;
            send_frame(CP_TYPE_ACK, (const uint8_t*)&ack, sizeof(ack));
        }
        break;
    case CP_TYPE_CMD_SAMPLE_REQUEST:
    case CP_TYPE_CMD_CONFIG: {
        cp_ack_nak_payload_t ack;
        ack.ack_sequence = plen >= 2 ? (uint16_t)pl[0] | ((uint16_t)pl[1] << 8) : 0;
        ack.result = 0;
        send_frame(CP_TYPE_ACK, (const uint8_t*)&ack, sizeof(ack));
        break;
    }
    default:
        break;
    }
}

static void ezo_poll_tick() {
    if (!s_ezo_ready) return;

    uint32_t now = millis();
    if (s_ezo_poll_state == 0) {
        if (now - s_ezo_poll_start_ms >= C3_EZO_POLL_INTERVAL_MS) {
            Serial1.print("R\r");
            s_ezo_response = "";
            s_ezo_poll_state = 1;
            s_ezo_poll_start_ms = now;
        }
        return;
    }
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\r') {
            s_ezo_response.trim();
            if (s_ezo_response.length() > 0 && isdigit(s_ezo_response.charAt(0))) {
                float ec = s_ezo_response.toFloat();
                if (ec >= 0.0f && ec <= 50000.0f) s_cached_conductivity_uS_cm = ec;
            }
            s_ezo_poll_state = 0;
            s_ezo_poll_start_ms = now;
            return;
        }
        if (c != '\n') s_ezo_response += c;
        if (s_ezo_response.length() >= 64) s_ezo_response = "";
    }
    if (now - s_ezo_poll_start_ms >= C3_EZO_RESPONSE_TIMEOUT_MS) {
        s_ezo_poll_state = 0;
        s_ezo_poll_start_ms = now;
    }
}

static void send_telemetry() {
    internal_adc_poll_valve();
    cp_telemetry_payload_t t;
    t.conductivity_uS_cm = s_cached_conductivity_uS_cm;
    t.temperature_c = s_cached_temperature_c;
    t.blowdown_state = s_blowdown_open ? 2 : 0;
    t.valve_open = s_blowdown_open;
    t.valve_feedback_mA = s_cached_valve_feedback_mA;
    t.solenoid_on = s_solenoid_on;
    t.sensor_ok = s_ezo_ready ? 1 : 0;
    t.temp_ok = 1;
    t.valve_fault = (s_cached_valve_feedback_mA < C3_VALVE_FAULT_LOW_MA) ? 1 : 0;
    t.comms_lost = (millis() - s_last_main_frame_ms > C3_MAIN_HEARTBEAT_TIMEOUT_MS) ? 1 : 0;
    t.sequence = ++s_telemetry_sequence;
    t.timestamp_ms = millis();
    send_frame(CP_TYPE_TELEMETRY, (const uint8_t*)&t, sizeof(t));
}

void setup() {
    Serial.begin(115200);
    delay(500);
    if (C3_RS485_DE_RE_PIN >= 0) {
        pinMode(C3_RS485_DE_RE_PIN, OUTPUT);
        digitalWrite(C3_RS485_DE_RE_PIN, LOW);
    }
    C3Serial.begin(115200, SERIAL_8N1, C3_RS485_RX_PIN, C3_RS485_TX_PIN);
    s_rx_len = 0;
    s_last_telemetry_ms = millis();

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    // EZO-EC on Serial1 (GPIO9 RX, GPIO10 TX)
    Serial1.begin(C3_EZO_BAUD, SERIAL_8N1, C3_EZO_RX_PIN, C3_EZO_TX_PIN);
    delay(2000);
    while (Serial1.available()) Serial1.read();
    Serial1.print("i\r");
    delay(300);
    s_ezo_response = "";
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\r') break;
        if (c != '\n') s_ezo_response += c;
    }
    s_ezo_response.trim();
    if (s_ezo_response.startsWith("?I,EC")) {
        s_ezo_ready = 1;
        Serial.printf("EZO-EC: %s\n", s_ezo_response.c_str());
    } else {
        s_ezo_ready = 0;
        Serial.printf("EZO-EC not found (response: '%s')\n", s_ezo_response.c_str());
    }
    s_ezo_poll_start_ms = millis();
    s_ezo_poll_state = 0;

    Serial.println("ESP32 DevKit coprocessor stub ready");
}

void loop() {
    ezo_poll_tick();

    while (C3Serial.available()) {
        uint8_t b = (uint8_t)C3Serial.read();
        if (s_rx_len == 0 && b != CP_SYNC_0) continue;
        if (s_rx_len == 1 && b != CP_SYNC_1) { s_rx_len = 0; continue; }
        s_rx_buf[s_rx_len++] = b;
        if (s_rx_len >= CP_HEADER_SIZE) {
            uint8_t plen = s_rx_buf[3];
            if (plen > CP_MAX_PAYLOAD) { s_rx_len = 0; continue; }
            size_t need = CP_HEADER_SIZE + plen + CP_CRC_SIZE;
            if (s_rx_len >= need) {
                process_rx_frame(s_rx_buf, s_rx_len);
                s_rx_len = 0;
                continue;
            }
        }
        if (s_rx_len >= CP_MAX_FRAME) s_rx_len = 0;
    }

    uint32_t now = millis();
    if (now - s_last_telemetry_ms >= (1000 / C3_TELEMETRY_HZ)) {
        s_last_telemetry_ms = now;
        send_telemetry();
    }
    delay(1);
}

/**
 * @file c3_coprocessor_stub.cpp
 * @brief Minimal ESP32-C3 firmware: RS-485 link, frame parser, dummy telemetry, ACK commands
 *
 * Target: Seeed Studio XIAO ESP32-C3 with XIAO RS485 Expansion Board.
 * https://wiki.seeedstudio.com/XIAO-RS485-Expansion-Board/
 *
 * Build: pio run -e esp32c3_coprocessor
 * Receives blowdown open/close (and optional solenoid); sends ACK and periodic telemetry.
 */

#include <Arduino.h>
#include "../include/coprocessor_protocol.h"

// XIAO ESP32-C3 + Seeed XIAO RS485 Expansion Board (D4/D5 = UART, D2 = DE/RE)
// Wiki: RX=D4(GPIO6), TX=D5(GPIO7), enable=D2(GPIO2)
#define C3_RS485_RX_PIN  7   // D5 GPIO7 = Serial1 RX
#define C3_RS485_TX_PIN  6   // D4 GPIO6 = Serial1 TX
#define C3_DE_RE_PIN     2   // D2 GPIO2 = RS485 driver enable (DE/RE)
#define C3_TELEMETRY_HZ  5
#define C3_MAIN_HEARTBEAT_TIMEOUT_MS  3000

HardwareSerial C3Serial(1);  // UART1
static uint8_t s_rx_buf[CP_MAX_FRAME];
static size_t s_rx_len = 0;
static uint16_t s_telemetry_sequence = 0;
static uint8_t s_blowdown_open = 0;   // 0=closed, 1=open
static uint8_t s_solenoid_on = 0;
static uint32_t s_last_main_frame_ms = 0;
static uint32_t s_last_telemetry_ms = 0;

static void set_de_re(bool drive) {
    digitalWrite(C3_DE_RE_PIN, drive ? HIGH : LOW);
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

static void send_telemetry() {
    cp_telemetry_payload_t t;
    t.conductivity_uS_cm = 1500.0f;   // Dummy
    t.temperature_c = 25.0f;           // Dummy
    t.blowdown_state = s_blowdown_open ? 2 : 0;  // 0=idle, 2=blowing down
    t.valve_open = s_blowdown_open;
    t.valve_feedback_mA = s_blowdown_open ? 19.5f : 4.2f;  // Dummy
    t.solenoid_on = s_solenoid_on;
    t.sensor_ok = 1;
    t.temp_ok = 1;
    t.valve_fault = 0;
    t.comms_lost = (millis() - s_last_main_frame_ms > C3_MAIN_HEARTBEAT_TIMEOUT_MS) ? 1 : 0;
    t.sequence = ++s_telemetry_sequence;
    t.timestamp_ms = millis();
    send_frame(CP_TYPE_TELEMETRY, (const uint8_t*)&t, sizeof(t));
}

void setup() {
    Serial.begin(115200);
    delay(500);
    if (C3_DE_RE_PIN >= 0) {
        pinMode(C3_DE_RE_PIN, OUTPUT);
        digitalWrite(C3_DE_RE_PIN, LOW);
    }
    C3Serial.begin(115200, SERIAL_8N1, C3_RS485_RX_PIN, C3_RS485_TX_PIN);
    s_rx_len = 0;
    s_last_telemetry_ms = millis();
    Serial.println("C3 coprocessor stub ready");
}

void loop() {
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

/**
 * @file test_coprocessor_protocol.cpp
 * @brief Unit test for coprocessor protocol: CRC16, frame build/parse, validity
 *
 * Run on main ESP32: pio run -e test_coprocessor_protocol
 */

#include <Arduino.h>
#include "../include/coprocessor_protocol.h"

static int s_fails = 0;
#define ASSERT(c) do { if (!(c)) { Serial.printf("FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); s_fails++; } } while(0)

void test_crc16() {
    Serial.println("  CRC16");
    uint8_t empty[] = {};
    ASSERT(cp_crc16(empty, 0) == 0xFFFF);
    uint8_t aa55[] = { 0xAA, 0x55 };
    uint16_t c = cp_crc16(aa55, 2);
    ASSERT(c != 0xFFFF && c != 0);
    uint8_t frame[26];
    frame[0] = CP_SYNC_0;
    frame[1] = CP_SYNC_1;
    frame[2] = 0x01;
    frame[3] = 20;
    for (size_t i = 0; i < 20; i++) frame[4 + i] = (uint8_t)i;
    uint16_t crc = cp_crc16(frame, 4 + 20);
    frame[24] = (uint8_t)(crc & 0xFF);
    frame[25] = (uint8_t)(crc >> 8);
    ASSERT(cp_frame_valid(frame, 26));
}

void test_frame_valid() {
    Serial.println("  frame_valid");
    uint8_t bad_sync[] = { 0x00, 0x00, CP_TYPE_TELEMETRY, 0, 0xFF, 0xFF };
    ASSERT(!cp_frame_valid(bad_sync, 6));
    uint8_t good[CP_HEADER_SIZE + CP_CRC_SIZE];
    good[0] = CP_SYNC_0;
    good[1] = CP_SYNC_1;
    good[2] = CP_TYPE_TELEMETRY;
    good[3] = 0;
    uint16_t crc = cp_crc16(good, 4);
    good[4] = (uint8_t)(crc & 0xFF);
    good[5] = (uint8_t)(crc >> 8);
    ASSERT(cp_frame_valid(good, 6));
}

void test_telemetry_build_parse() {
    Serial.println("  telemetry build/parse");
    cp_telemetry_payload_t t;
    t.conductivity_uS_cm = 1234.5f;
    t.temperature_c = 67.8f;
    t.blowdown_state = 1;
    t.valve_open = 0;
    t.valve_feedback_mA = 4.5f;
    t.solenoid_on = 0;
    t.sensor_ok = 1;
    t.temp_ok = 1;
    t.valve_fault = 0;
    t.comms_lost = 0;
    t.sequence = 42;
    t.timestamp_ms = 10000;

    uint8_t frame[CP_MAX_FRAME];
    frame[0] = CP_SYNC_0;
    frame[1] = CP_SYNC_1;
    frame[2] = CP_TYPE_TELEMETRY;
    frame[3] = (uint8_t)sizeof(cp_telemetry_payload_t);
    memcpy(frame + CP_HEADER_SIZE, &t, sizeof(t));
    uint16_t crc = cp_crc16(frame, CP_HEADER_SIZE + sizeof(t));
    frame[CP_HEADER_SIZE + sizeof(t)] = (uint8_t)(crc & 0xFF);
    frame[CP_HEADER_SIZE + sizeof(t) + 1] = (uint8_t)(crc >> 8);

    ASSERT(cp_frame_valid(frame, CP_HEADER_SIZE + sizeof(t) + CP_CRC_SIZE));
    ASSERT(cp_frame_type(frame) == CP_TYPE_TELEMETRY);
    ASSERT(cp_frame_payload_len(frame) == sizeof(cp_telemetry_payload_t));
    const cp_telemetry_payload_t* r = (const cp_telemetry_payload_t*)cp_frame_payload(frame);
    ASSERT(r->conductivity_uS_cm == t.conductivity_uS_cm);
    ASSERT(r->temperature_c == t.temperature_c);
    ASSERT(r->sequence == 42);
}

void test_ack_nak_build() {
    Serial.println("  ACK/NAK build");
    cp_ack_nak_payload_t a;
    a.ack_sequence = 3;
    a.result = 0;
    uint8_t frame[CP_HEADER_SIZE + CP_ACK_NAK_PAYLOAD_SIZE + CP_CRC_SIZE];
    frame[0] = CP_SYNC_0;
    frame[1] = CP_SYNC_1;
    frame[2] = CP_TYPE_ACK;
    frame[3] = (uint8_t)sizeof(cp_ack_nak_payload_t);
    memcpy(frame + CP_HEADER_SIZE, &a, sizeof(a));
    uint16_t crc = cp_crc16(frame, CP_HEADER_SIZE + sizeof(a));
    frame[CP_HEADER_SIZE + sizeof(a)] = (uint8_t)(crc & 0xFF);
    frame[CP_HEADER_SIZE + sizeof(a) + 1] = (uint8_t)(crc >> 8);
    ASSERT(cp_frame_valid(frame, sizeof(frame)));
    ASSERT(cp_frame_type(frame) == CP_TYPE_ACK);
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("Coprocessor protocol tests");

    Serial.println("test_crc16");
    test_crc16();
    Serial.println("test_frame_valid");
    test_frame_valid();
    Serial.println("test_telemetry_build_parse");
    test_telemetry_build_parse();
    Serial.println("test_ack_nak_build");
    test_ack_nak_build();

    Serial.println(s_fails == 0 ? "All passed." : "Some failed.");
}

void loop() {
    delay(10000);
}

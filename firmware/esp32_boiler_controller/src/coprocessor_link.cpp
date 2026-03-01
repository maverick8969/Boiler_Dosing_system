/**
 * @file coprocessor_link.cpp
 * @brief Main ESP32 RS-485 link to ESP32-C3 coprocessor
 */

#include "coprocessor_link.h"
#include <string.h>

CoprocessorLink::CoprocessorLink(HardwareSerial& serial, int8_t de_re_pin)
    : _serial(serial),
      _de_re_pin(de_re_pin),
      _baud(CP_LINK_BAUD_DEFAULT),
      _comms_lost(true),
      _comms_lost_since_ms(0),
      _cmd_sequence(0),
      _last_cmd_result(CP_CMD_RESULT_NONE),
      _last_nak_result(0),
      _rx_len(0) {
    memset(&_telemetry, 0, sizeof(_telemetry));
}

bool CoprocessorLink::begin(uint32_t baud) {
    _baud = baud;
    if (_de_re_pin >= 0) {
        pinMode((pin_size_t)_de_re_pin, OUTPUT);
        digitalWrite((pin_size_t)_de_re_pin, LOW);
    }
    _serial.begin(baud);
    _rx_len = 0;
    _comms_lost = true;
    _comms_lost_since_ms = 0;
    _telemetry.valid = false;
    return true;
}

void CoprocessorLink::_setDeRe(bool drive) {
    if (_de_re_pin >= 0) {
        digitalWrite((pin_size_t)_de_re_pin, drive ? HIGH : LOW);
    }
}

void CoprocessorLink::_sendFrame(uint8_t type, const uint8_t* payload, uint8_t plen) {
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

    _setDeRe(true);
    delay(1);
    _serial.write(frame, CP_HEADER_SIZE + plen + CP_CRC_SIZE);
    _serial.flush();
    delay(CP_LINK_TURNAROUND_MS);
    _setDeRe(false);
}

cp_cmd_result_t CoprocessorLink::_sendCommandAndWaitAck(uint8_t type, const uint8_t* payload, uint8_t plen) {
    _last_cmd_result = CP_CMD_RESULT_NONE;
    _last_nak_result = 0;
    if (_comms_lost) return CP_CMD_RESULT_LINK_DOWN;

    uint16_t seq = (plen >= 2) ? (uint16_t)payload[0] | ((uint16_t)payload[1] << 8) : 0;

    for (int retry = 0; retry < CP_LINK_CMD_RETRIES; retry++) {
        _sendFrame(type, payload, plen);
        uint8_t rx[CP_MAX_FRAME];
        size_t rx_len = 0;
        if (_readFrame(rx, &rx_len, CP_LINK_CMD_REPLY_TIMEOUT_MS)) {
            uint8_t rtype = cp_frame_type(rx);
            if (rtype == CP_TYPE_ACK) {
                cp_ack_nak_payload_t* a = (cp_ack_nak_payload_t*)cp_frame_payload(rx);
                if (a->ack_sequence == seq) {
                    _last_cmd_result = CP_CMD_RESULT_ACK;
                    return CP_CMD_RESULT_ACK;
                }
            } else if (rtype == CP_TYPE_NAK) {
                cp_ack_nak_payload_t* a = (cp_ack_nak_payload_t*)cp_frame_payload(rx);
                if (a->ack_sequence == seq) {
                    _last_cmd_result = CP_CMD_RESULT_NAK;
                    _last_nak_result = a->result;
                    return CP_CMD_RESULT_NAK;
                }
            }
        }
        delay(10 * (retry + 1));
    }
    _last_cmd_result = CP_CMD_RESULT_TIMEOUT;
    return CP_CMD_RESULT_TIMEOUT;
}

bool CoprocessorLink::_readFrame(uint8_t* out_frame, size_t* out_len, uint32_t timeout_ms) {
    uint32_t start = millis();
    *out_len = 0;
    for (;;) {
        while (_serial.available()) {
            uint8_t b = (uint8_t)_serial.read();
            if (_rx_len == 0 && b != CP_SYNC_0) continue;
            if (_rx_len == 1 && b != CP_SYNC_1) { _rx_len = 0; continue; }
            _rx_buf[_rx_len++] = b;
            if (_rx_len >= CP_HEADER_SIZE) {
                uint8_t plen = _rx_buf[3];
                if (plen > CP_MAX_PAYLOAD) { _rx_len = 0; continue; }
                size_t need = CP_HEADER_SIZE + plen + CP_CRC_SIZE;
                if (_rx_len >= need) {
                    if (cp_frame_valid(_rx_buf, _rx_len)) {
                        memcpy(out_frame, _rx_buf, _rx_len);
                        *out_len = _rx_len;
                        _rx_len = 0;
                        return true;
                    }
                    _rx_len = 0;
                    continue;
                }
            }
            if (_rx_len >= CP_MAX_FRAME) _rx_len = 0;
        }
        if (timeout_ms == 0) break;
        if (millis() - start >= timeout_ms) break;
        delay(1);
    }
    return false;
}

void CoprocessorLink::_processFrame(const uint8_t* frame, size_t len) {
    if (!cp_frame_valid(frame, len)) return;
    uint8_t type = cp_frame_type(frame);
    uint8_t plen = cp_frame_payload_len(frame);
    const uint8_t* pl = cp_frame_payload(frame);

    switch (type) {
    case CP_TYPE_TELEMETRY:
        if (plen >= sizeof(cp_telemetry_payload_t)) {
            const cp_telemetry_payload_t* t = (const cp_telemetry_payload_t*)pl;
            _telemetry.conductivity_uS_cm = t->conductivity_uS_cm;
            _telemetry.temperature_c = t->temperature_c;
            _telemetry.blowdown_state = t->blowdown_state;
            _telemetry.valve_open = t->valve_open ? true : false;
            _telemetry.valve_feedback_mA = t->valve_feedback_mA;
            _telemetry.solenoid_on = t->solenoid_on ? true : false;
            _telemetry.sensor_ok = t->sensor_ok ? true : false;
            _telemetry.temp_ok = t->temp_ok ? true : false;
            _telemetry.valve_fault = t->valve_fault ? true : false;
            _telemetry.comms_lost = t->comms_lost ? true : false;
            _telemetry.sequence = t->sequence;
            _telemetry.timestamp_ms = t->timestamp_ms;
            _telemetry.valid = true;
            _telemetry.last_received_ms = millis();
            _comms_lost = false;
            _comms_lost_since_ms = 0;
        }
        break;
    case CP_TYPE_ACK:
    case CP_TYPE_NAK:
        // Handled in _sendCommandAndWaitAck
        break;
    case CP_TYPE_EVENT:
    case CP_TYPE_ERROR:
        // Main could log or set alarms; for now just consumed
        break;
    default:
        break;
    }
}

void CoprocessorLink::poll() {
    uint8_t frame[CP_MAX_FRAME];
    size_t len = 0;
    while (_readFrame(frame, &len, 0)) {
        _processFrame(frame, len);
    }
    // Comms lost if no telemetry for timeout
    if (_telemetry.valid && (millis() - _telemetry.last_received_ms >= CP_LINK_TELEMETRY_TIMEOUT_MS)) {
        if (!_comms_lost) _comms_lost_since_ms = millis();
        _comms_lost = true;
    }
}

cp_cmd_result_t CoprocessorLink::sendBlowdownOpen() {
    cp_cmd_blowdown_open_t cmd;
    cmd.sequence = ++_cmd_sequence;
    return _sendCommandAndWaitAck(CP_TYPE_CMD_BLOWDOWN_OPEN, (const uint8_t*)&cmd, sizeof(cmd));
}

cp_cmd_result_t CoprocessorLink::sendBlowdownClose() {
    cp_cmd_blowdown_close_t cmd;
    cmd.sequence = ++_cmd_sequence;
    return _sendCommandAndWaitAck(CP_TYPE_CMD_BLOWDOWN_CLOSE, (const uint8_t*)&cmd, sizeof(cmd));
}

cp_cmd_result_t CoprocessorLink::sendSolenoid(bool on) {
    cp_cmd_solenoid_t cmd;
    cmd.sequence = ++_cmd_sequence;
    cmd.on = on ? 1 : 0;
    return _sendCommandAndWaitAck(CP_TYPE_CMD_SOLENOID, (const uint8_t*)&cmd, sizeof(cmd));
}

cp_cmd_result_t CoprocessorLink::sendSampleRequest() {
    cp_cmd_sample_request_t cmd;
    cmd.sequence = ++_cmd_sequence;
    return _sendCommandAndWaitAck(CP_TYPE_CMD_SAMPLE_REQUEST, (const uint8_t*)&cmd, sizeof(cmd));
}

void CoprocessorLink::sendTimeSync(uint32_t unix_sec, uint32_t subsec_ms) {
    cp_time_sync_payload_t pl;
    pl.unix_time_sec = unix_sec;
    pl.unix_time_subsec_ms = subsec_ms;
    _setDeRe(true);
    delay(1);
    _sendFrame(CP_TYPE_TIME_SYNC, (const uint8_t*)&pl, sizeof(pl));
    _setDeRe(false);
}

/**
 * @file coprocessor_link.h
 * @brief Main ESP32 side: RS-485 link to ESP32-C3 coprocessor
 *
 * Sends commands (blowdown open/close, solenoid, etc.), receives telemetry,
 * ACK/NAK, events, and errors. DE/RE for half-duplex; turn-around delay after TX.
 */

#ifndef COPROCESSOR_LINK_H
#define COPROCESSOR_LINK_H

#include <Arduino.h>
#include "config.h"
#include "coprocessor_protocol.h"

// Defaults
#define CP_LINK_TELEMETRY_TIMEOUT_MS   5000   // No telemetry for this long → comms lost
#define CP_LINK_CMD_REPLY_TIMEOUT_MS   300
#define CP_LINK_CMD_RETRIES            3
#define CP_LINK_TURNAROUND_MS          2      // 1–2 character times at 115200
#define CP_LINK_BAUD_DEFAULT          115200

// ============================================================================
// LAST TELEMETRY (mirrors C3 telemetry for main control loop)
// ============================================================================

typedef struct {
    float conductivity_uS_cm;
    float temperature_c;
    uint8_t blowdown_state;
    bool valve_open;
    float valve_feedback_mA;
    bool solenoid_on;
    bool sensor_ok;
    bool temp_ok;
    bool valve_fault;
    bool comms_lost;           // C3 reports loss of main heartbeat
    uint16_t sequence;
    uint32_t timestamp_ms;
    bool valid;                 // At least one telemetry received
    uint32_t last_received_ms;  // millis() when last telemetry arrived
} cp_link_telemetry_t;

// ============================================================================
// COMMAND RESULT (after sendCommand)
// ============================================================================

typedef enum {
    CP_CMD_RESULT_NONE = 0,     // Not sent or no reply yet
    CP_CMD_RESULT_ACK,
    CP_CMD_RESULT_NAK,
    CP_CMD_RESULT_TIMEOUT,
    CP_CMD_RESULT_LINK_DOWN
} cp_cmd_result_t;

// ============================================================================
// COPROCESSOR LINK CLASS
// ============================================================================

class CoprocessorLink {
public:
    /**
     * @brief Constructor
     * @param serial HardwareSerial used for RS-485 (e.g. Serial2)
     * @param de_re_pin GPIO for DE and /RE tied together (high = drive bus)
     */
    CoprocessorLink(HardwareSerial& serial, int8_t de_re_pin);

    /**
     * @brief Initialize serial and DE/RE pin
     * @param baud Baud rate (default CP_LINK_BAUD_DEFAULT)
     * @return true on success
     */
    bool begin(uint32_t baud = CP_LINK_BAUD_DEFAULT);

    /**
     * @brief Call from main loop or a task: receive bytes, parse frames, update last telemetry
     */
    void poll();

    /**
     * @brief Get last received telemetry (valid only if .valid is true)
     */
    const cp_link_telemetry_t& getLastTelemetry() const { return _telemetry; }

    /**
     * @brief True if no telemetry received within CP_LINK_TELEMETRY_TIMEOUT_MS
     */
    bool isCommsLost() const { return _comms_lost; }

    /**
     * @brief millis() when comms were first considered lost (0 if not lost)
     */
    uint32_t getCommsLostSinceMs() const { return _comms_lost_since_ms; }

    /**
     * @brief Send blowdown open command; blocks until ACK/NAK or timeout
     * @return CP_CMD_RESULT_ACK, NAK, TIMEOUT, or LINK_DOWN
     */
    cp_cmd_result_t sendBlowdownOpen();

    /**
     * @brief Send blowdown close command
     */
    cp_cmd_result_t sendBlowdownClose();

    /**
     * @brief Send solenoid on/off
     */
    cp_cmd_result_t sendSolenoid(bool on);

    /**
     * @brief Send sample request (optional)
     */
    cp_cmd_result_t sendSampleRequest();

    /**
     * @brief Send time sync (Unix time)
     */
    void sendTimeSync(uint32_t unix_sec, uint32_t subsec_ms = 0);

    /**
     * @brief Last result from a sent command (ACK/NAK/timeout)
     */
    cp_cmd_result_t getLastCommandResult() const { return _last_cmd_result; }

    /**
     * @brief Last NAK result code if getLastCommandResult() == CP_CMD_RESULT_NAK
     */
    uint8_t getLastNakResult() const { return _last_nak_result; }

private:
    HardwareSerial& _serial;
    int8_t _de_re_pin;
    uint32_t _baud;
    cp_link_telemetry_t _telemetry;
    bool _comms_lost;
    uint32_t _comms_lost_since_ms;
    uint16_t _cmd_sequence;
    cp_cmd_result_t _last_cmd_result;
    uint8_t _last_nak_result;

    uint8_t _rx_buf[CP_MAX_FRAME];
    size_t _rx_len;

    void _setDeRe(bool drive);
    void _sendFrame(uint8_t type, const uint8_t* payload, uint8_t plen);
    cp_cmd_result_t _sendCommandAndWaitAck(uint8_t type, const uint8_t* payload, uint8_t plen);
    void _processFrame(const uint8_t* frame, size_t len);
    bool _readFrame(uint8_t* out_frame, size_t* out_len, uint32_t timeout_ms);
};

#endif // COPROCESSOR_LINK_H

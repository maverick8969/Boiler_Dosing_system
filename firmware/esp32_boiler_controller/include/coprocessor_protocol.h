/**
 * @file coprocessor_protocol.h
 * @brief RS-485 inter-MCU protocol between main ESP32 and panel coprocessor (ESP32 DevKit)
 *
 * Shared definitions for frame format, message types, and payloads.
 * Used by both main (control box) and panel (boiler panel) firmware.
 *
 * Physical: half-duplex RS-485; UART 115200–921600 8N1; DE/RE per node.
 * Frame: SYNC(2) TYPE(1) LEN(1) PAYLOAD(LEN) CRC16(2). LEN excludes header and CRC.
 */

#ifndef COPROCESSOR_PROTOCOL_H
#define COPROCESSOR_PROTOCOL_H

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// FRAME CONSTANTS
// ============================================================================

#define CP_SYNC_0                0xAA
#define CP_SYNC_1                0x55
#define CP_HEADER_SIZE           4   // sync0, sync1, type, len
#define CP_CRC_SIZE              2
#define CP_MAX_PAYLOAD           64
#define CP_MAX_FRAME             (CP_HEADER_SIZE + CP_MAX_PAYLOAD + CP_CRC_SIZE)

// ============================================================================
// MESSAGE TYPES
// ============================================================================

typedef enum {
    CP_TYPE_TELEMETRY = 0x01,    // Panel -> Main: sensor + actuator state
    CP_TYPE_CMD_MASK  = 0x10,    // Commands from Main -> Panel
    CP_TYPE_CMD_BLOWDOWN_OPEN  = 0x11,
    CP_TYPE_CMD_BLOWDOWN_CLOSE = 0x12,
    CP_TYPE_CMD_SOLENOID       = 0x13,  // on/off
    CP_TYPE_CMD_SAMPLE_REQUEST = 0x14,
    CP_TYPE_CMD_CONFIG        = 0x15,   // config blob (optional)
    CP_TYPE_ACK               = 0x20,   // Panel -> Main: command accepted
    CP_TYPE_NAK               = 0x21,   // Panel -> Main: command rejected
    CP_TYPE_EVENT             = 0x30,   // Panel -> Main: alarm, valve timeout, limit fault
    CP_TYPE_ERROR             = 0x31,   // Panel -> Main: CRC/seq error, internal fault
    CP_TYPE_TIME_SYNC         = 0x40,   // Main -> Panel: Unix timestamp
} cp_msg_type_t;

// ============================================================================
// TELEMETRY PAYLOAD (Panel -> Main, 2–10 Hz)
// ============================================================================

typedef struct __attribute__((packed)) {
    float conductivity_uS_cm;   // Calibrated conductivity (uS/cm)
    float temperature_c;        // RTD temperature (°C)
    uint8_t blowdown_state;     // blowdown_state_t equivalent
    uint8_t valve_open;         // 1 = open, 0 = closed
    float valve_feedback_mA;    // 4–20 mA position feedback
    uint8_t solenoid_on;       // 1 = on, 0 = off
    uint8_t sensor_ok;          // Conductivity sensor valid
    uint8_t temp_ok;           // RTD valid
    uint8_t valve_fault;       // 4–20 mA fault
    uint8_t comms_lost;        // Panel has lost main heartbeat
    uint16_t sequence;         // Incrementing telemetry sequence
    uint32_t timestamp_ms;      // Panel millis() (optional for drift check)
} cp_telemetry_payload_t;

#define CP_TELEMETRY_PAYLOAD_SIZE  (sizeof(cp_telemetry_payload_t))

// ============================================================================
// COMMAND PAYLOADS (Main -> Panel)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint16_t sequence;         // Command sequence for ACK/NAK match
} cp_cmd_blowdown_open_t;

typedef struct __attribute__((packed)) {
    uint16_t sequence;
} cp_cmd_blowdown_close_t;

typedef struct __attribute__((packed)) {
    uint16_t sequence;
    uint8_t  on;               // 1 = on, 0 = off
} cp_cmd_solenoid_t;

typedef struct __attribute__((packed)) {
    uint16_t sequence;
} cp_cmd_sample_request_t;

// ============================================================================
// ACK / NAK PAYLOAD (Panel -> Main)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint16_t ack_sequence;     // Sequence of command being acknowledged
    uint8_t  result;           // For ACK: 0; for NAK: error code
} cp_ack_nak_payload_t;

#define CP_ACK_NAK_PAYLOAD_SIZE  (sizeof(cp_ack_nak_payload_t))

// NAK result codes
#define CP_NAK_BUSY            1
#define CP_NAK_INVALID_STATE    2
#define CP_NAK_VALVE_FAULT     3
#define CP_NAK_OTHER           0xFF

// ============================================================================
// EVENT PAYLOAD (Panel -> Main, on change)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint16_t event_code;       // Alarm bitmask or event id
    uint8_t  severity;         // 0=info, 1=warning, 2=alarm
    uint8_t  reserved;
    uint32_t timestamp_ms;     // Panel millis
} cp_event_payload_t;

#define CP_EVENT_PAYLOAD_SIZE  (sizeof(cp_event_payload_t))

// ============================================================================
// ERROR PAYLOAD (Panel -> Main)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t  error_code;       // CRC, sequence, internal
    uint8_t  reserved[3];
    uint32_t timestamp_ms;
} cp_error_payload_t;

#define CP_ERROR_PAYLOAD_SIZE  (sizeof(cp_error_payload_t))

// ============================================================================
// TIME SYNC PAYLOAD (Main -> Panel)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint32_t unix_time_sec;
    uint32_t unix_time_subsec_ms;
} cp_time_sync_payload_t;

#define CP_TIME_SYNC_PAYLOAD_SIZE  (sizeof(cp_time_sync_payload_t))

// ============================================================================
// FRAME BUILD / PARSE HELPERS
// ============================================================================

/**
 * Compute CRC-16 (CCITT: poly 0x1021, init 0xFFFF) over length bytes.
 */
uint16_t cp_crc16(const uint8_t* data, size_t length);

/**
 * Check frame: sync, type, len <= CP_MAX_PAYLOAD, CRC.
 * frame_len = CP_HEADER_SIZE + payload_len + CP_CRC_SIZE.
 */
bool cp_frame_valid(const uint8_t* frame, size_t frame_len);

/**
 * Get payload pointer (after header). Caller must ensure frame_len >= CP_HEADER_SIZE.
 */
static inline const uint8_t* cp_frame_payload(const uint8_t* frame) {
    return frame + CP_HEADER_SIZE;
}

/**
 * Get payload length from frame (byte at index 3).
 */
static inline uint8_t cp_frame_payload_len(const uint8_t* frame) {
    return frame[3];
}

/**
 * Get message type from frame (byte at index 2).
 */
static inline uint8_t cp_frame_type(const uint8_t* frame) {
    return frame[2];
}

#endif // COPROCESSOR_PROTOCOL_H

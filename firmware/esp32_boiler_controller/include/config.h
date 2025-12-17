/**
 * @file config.h
 * @brief System Configuration for Columbia CT-6 Boiler Dosing Controller
 *
 * This file defines all configurable parameters, data structures,
 * and system constants for the boiler dosing controller.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// FIRMWARE VERSION
// ============================================================================

#define FIRMWARE_VERSION_MAJOR  1
#define FIRMWARE_VERSION_MINOR  0
#define FIRMWARE_VERSION_PATCH  0
#define FIRMWARE_VERSION_STRING "1.0.0"
#define FIRMWARE_BUILD_DATE     __DATE__
#define FIRMWARE_BUILD_TIME     __TIME__

// ============================================================================
// SYSTEM IDENTIFICATION
// ============================================================================

#define DEVICE_NAME             "CT6-Boiler-Controller"
#define DEVICE_MODEL            "ESP32-BDC-001"
#define MANUFACTURER            "Custom"

// ============================================================================
// WIFI CONFIGURATION
// ============================================================================

#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASS_MAX_LEN       64
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define WIFI_RECONNECT_DELAY_MS 5000
#define WIFI_HOSTNAME           "boiler-controller"

// Default AP mode credentials (for initial setup)
#define WIFI_AP_SSID            "BoilerController-Setup"
#define WIFI_AP_PASS            "boiler2024"
#define WIFI_AP_CHANNEL         1

// ============================================================================
// TIMESCALEDB CONFIGURATION
// ============================================================================

#define TSDB_HOST_MAX_LEN       64
#define TSDB_PORT_DEFAULT       5432
#define TSDB_DATABASE           "boiler_data"
#define TSDB_TABLE_READINGS     "sensor_readings"
#define TSDB_TABLE_EVENTS       "system_events"
#define TSDB_TABLE_ALARMS       "alarm_history"

// HTTP endpoint for data logging (via TimescaleDB REST API or custom backend)
#define TSDB_HTTP_PORT          8080
#define TSDB_LOG_INTERVAL_MS    10000   // 10 seconds default

// ============================================================================
// SAMPLING MODE DEFINITIONS
// ============================================================================

typedef enum {
    SAMPLE_MODE_CONTINUOUS = 0,         // Mode C - Continuous monitoring
    SAMPLE_MODE_INTERMITTENT = 1,       // Mode I - Periodic sampling
    SAMPLE_MODE_TIMED_BLOWDOWN = 2,     // Mode T - Intermittent with fixed blowdown
    SAMPLE_MODE_TIME_PROPORTIONAL = 3   // Mode P - Blowdown proportional to deviation
} sample_mode_t;

// Sampling Timing Limits
#define SAMPLE_INTERVAL_MIN_SEC     300     // 5 minutes minimum
#define SAMPLE_INTERVAL_MAX_SEC     86400   // 24 hours maximum
#define SAMPLE_DURATION_MIN_SEC     60      // 1 minute minimum
#define SAMPLE_DURATION_MAX_SEC     3599    // ~60 minutes maximum
#define SAMPLE_HOLD_TIME_MIN_SEC    1       // 1 second minimum
#define SAMPLE_HOLD_TIME_MAX_SEC    5999    // ~100 minutes maximum

// ============================================================================
// CHEMICAL FEED MODE DEFINITIONS
// ============================================================================

typedef enum {
    FEED_MODE_DISABLED = 0,             // Feed disabled
    FEED_MODE_A_BLOWDOWN_FEED = 1,      // Feed follows blowdown with lockout
    FEED_MODE_B_PERCENT_BLOWDOWN = 2,   // Feed % of accumulated blowdown time
    FEED_MODE_C_PERCENT_TIME = 3,       // Continuous duty cycle
    FEED_MODE_D_WATER_CONTACT = 4,      // Triggered by water meter contacts
    FEED_MODE_E_PADDLEWHEEL = 5,        // Triggered by paddlewheel volume
    FEED_MODE_S_SCHEDULED = 6           // Time-of-day scheduled feed
} feed_mode_t;

// Feed Timing Limits
#define FEED_LOCKOUT_MIN_SEC        1
#define FEED_LOCKOUT_MAX_SEC        5999    // ~100 minutes
#define FEED_PERCENT_MIN            1       // 0.1% (stored as 1 = 0.1%)
#define FEED_PERCENT_MAX            990     // 99.0%
#define FEED_CYCLE_TIME_MIN_SEC     600     // 10 minutes
#define FEED_CYCLE_TIME_MAX_SEC     3599    // ~60 minutes
#define FEED_TIME_LIMIT_MIN_SEC     60      // 1 minute
#define FEED_TIME_LIMIT_MAX_SEC     5999    // ~100 minutes

// ============================================================================
// HOA (Hand-Off-Auto) MODE DEFINITIONS
// ============================================================================

typedef enum {
    HOA_AUTO = 0,       // Automatic control based on setpoint
    HOA_OFF = 1,        // Output forced OFF
    HOA_HAND = 2        // Output forced ON (with timeout)
} hoa_mode_t;

#define HOA_HAND_TIMEOUT_SEC    600     // 10 minute timeout for HAND mode

// ============================================================================
// CONDUCTIVITY CONFIGURATION STRUCTURE
// ============================================================================

typedef struct {
    // Measurement settings
    uint16_t range_max;             // Maximum range in uS/cm (default 10000)
    float cell_constant;            // K factor (default 1.0)
    float ppm_conversion_factor;    // PPM = uS/cm * factor (0.2-1.0, default 0.666)
    int8_t calibration_percent;     // Calibration offset -50 to +50%
    uint8_t units;                  // 0 = uS/cm, 1 = ppm
    bool temp_comp_enabled;         // Temperature compensation enabled
    float temp_comp_coefficient;    // Temperature coefficient (default 0.02 = 2%/C)
    float manual_temperature;       // Manual temp value if auto disabled (C)

    // Sampling configuration
    sample_mode_t sample_mode;      // Sampling mode (C/I/T/P)
    uint16_t interval_seconds;      // Time between samples (Mode I/T/P)
    uint16_t duration_seconds;      // Sample duration (Mode I/T/P)
    uint16_t hold_time_seconds;     // Hold time for trapped sample (Mode I/T/P)
    uint16_t blow_time_seconds;     // Fixed blowdown time (Mode T)
    uint16_t prop_band;             // Proportional band in uS/cm (Mode P)
    uint16_t max_prop_time_seconds; // Maximum proportional time (Mode P)

    // Anti-flashing feature (for boiler steam flash)
    bool anti_flash_enabled;        // Enable signal dampening
    uint8_t anti_flash_factor;      // Dampening factor (1-10)
} conductivity_config_t;

// Default conductivity configuration
#define COND_DEFAULT_RANGE_MAX          10000
#define COND_DEFAULT_CELL_CONSTANT      1.0
#define COND_DEFAULT_PPM_FACTOR         0.666
#define COND_DEFAULT_CAL_PERCENT        0
#define COND_DEFAULT_UNITS              0   // uS/cm
#define COND_DEFAULT_TEMP_COMP          true
#define COND_DEFAULT_TEMP_COEFF         0.02
#define COND_DEFAULT_SAMPLE_MODE        SAMPLE_MODE_CONTINUOUS
#define COND_DEFAULT_INTERVAL           3600    // 1 hour
#define COND_DEFAULT_DURATION           300     // 5 minutes
#define COND_DEFAULT_HOLD_TIME          60      // 1 minute
#define COND_DEFAULT_BLOW_TIME          600     // 10 minutes
#define COND_DEFAULT_PROP_BAND          200     // 200 uS/cm
#define COND_DEFAULT_MAX_PROP_TIME      600     // 10 minutes

// ============================================================================
// BLOWDOWN CONFIGURATION STRUCTURE
// ============================================================================

typedef struct {
    uint16_t setpoint;              // Target conductivity in uS/cm
    uint16_t deadband;              // Hysteresis band in uS/cm
    uint16_t time_limit_seconds;    // Maximum blowdown time (0 = unlimited)
    uint8_t control_direction;      // 0 = HIGH (blowdown when above), 1 = LOW
    uint8_t ball_valve_delay;       // Motorized valve open/close time (seconds)
    hoa_mode_t hoa_mode;            // Hand-Off-Auto mode
    bool timeout_flag;              // Set when timeout occurs
    uint32_t accumulated_time;      // Total blowdown time (seconds, persistent)
} blowdown_config_t;

// Blowdown limits
#define BLOW_SETPOINT_MIN           0
#define BLOW_SETPOINT_MAX           10000
#define BLOW_DEADBAND_MIN           5
#define BLOW_DEADBAND_MAX           500
#define BLOW_TIME_LIMIT_MIN         60      // 1 minute
#define BLOW_TIME_LIMIT_MAX         32340   // 8:59 hours

// Default blowdown configuration
#define BLOW_DEFAULT_SETPOINT       2500    // 2500 uS/cm
#define BLOW_DEFAULT_DEADBAND       50      // 50 uS/cm
#define BLOW_DEFAULT_TIME_LIMIT     0       // Unlimited
#define BLOW_DEFAULT_DIRECTION      0       // HIGH (blowdown above setpoint)
#define BLOW_DEFAULT_VALVE_DELAY    0       // No delay (solenoid valve)

// ============================================================================
// CHEMICAL PUMP CONFIGURATION STRUCTURE
// ============================================================================

typedef struct {
    bool enabled;                   // Pump enabled
    char name[16];                  // Pump name (e.g., "H2SO3", "NaOH", "Amine")
    feed_mode_t feed_mode;          // Chemical feed mode
    hoa_mode_t hoa_mode;            // Hand-Off-Auto mode

    // Mode A: Blowdown + Feed
    uint16_t lockout_seconds;       // Maximum feed time during blowdown

    // Mode B: % of Blowdown
    uint8_t percent_of_blowdown;    // 5-99%
    uint16_t max_time_seconds;      // Maximum feed time

    // Mode C: % of Time
    uint16_t percent_of_time;       // 1-990 (0.1% units)
    uint16_t cycle_time_seconds;    // Total cycle time

    // Mode D: Water Contactor
    uint16_t time_per_contact_ms;   // Feed time per contact (milliseconds)
    uint8_t contact_divider;        // Divide contacts by this value
    uint8_t assigned_meter;         // 0=WM1, 1=WM2, 2=Both

    // Mode E: Paddlewheel
    uint16_t time_per_volume_ms;    // Feed time per volume unit (milliseconds)
    uint16_t volume_to_initiate;    // Volume units to trigger feed

    // Common limits
    uint16_t time_limit_seconds;    // Maximum accumulated feed time

    // Stepper motor settings
    uint32_t steps_per_ml;          // Steps per milliliter of chemical
    uint16_t max_speed;             // Maximum stepper speed (steps/sec)
    uint16_t acceleration;          // Acceleration (steps/sec^2)

    // Statistics (persistent)
    uint32_t total_steps;           // Total steps run
    uint32_t total_runtime_sec;     // Total runtime in seconds
} pump_config_t;

// Default pump configuration
#define PUMP_DEFAULT_STEPS_PER_ML   200     // Calibration dependent
#define PUMP_DEFAULT_MAX_SPEED      1000
#define PUMP_DEFAULT_ACCELERATION   500

// ============================================================================
// WATER METER CONFIGURATION STRUCTURE
// ============================================================================

typedef enum {
    METER_TYPE_DISABLED = 0,
    METER_TYPE_CONTACTOR = 1,       // Contact closure (dry contact)
    METER_TYPE_PADDLEWHEEL = 2      // Hall effect paddlewheel
} meter_type_t;

typedef struct {
    meter_type_t type;              // Meter type
    uint8_t units;                  // 0 = Gallons, 1 = Liters
    uint16_t volume_per_contact;    // Volume per contact (Contactor mode)
    float k_factor;                 // Pulses per volume unit (Paddlewheel mode)
    uint32_t totalizer;             // Running total (persistent)
    uint32_t last_reset_time;       // Timestamp of last reset
} water_meter_config_t;

// Water meter limits
#define METER_VOL_PER_CONTACT_MIN   1
#define METER_VOL_PER_CONTACT_MAX   500
#define METER_K_FACTOR_MIN          0.01
#define METER_K_FACTOR_MAX          999.99
#define METER_TOTALIZER_MAX         99999999

// ============================================================================
// ALARM CONFIGURATION STRUCTURE
// ============================================================================

typedef struct {
    // Conductivity alarms (can be absolute or percentage)
    bool use_percent_alarms;        // True = % of setpoint, False = absolute
    uint16_t cond_high_absolute;    // Absolute high alarm (uS/cm)
    uint16_t cond_low_absolute;     // Absolute low alarm (uS/cm)
    uint8_t cond_high_percent;      // High alarm % (1-50, 0=disabled)
    uint8_t cond_low_percent;       // Low alarm % (1-50, 0=disabled)

    // Timeout alarms
    bool blowdown_timeout_enabled;
    bool feed_timeout_enabled;

    // Safety alarms
    bool no_flow_enabled;
    bool sensor_error_enabled;
    bool temp_error_enabled;
    bool drum_level_enabled;

    // Alarm relay configuration
    bool dedicated_alarm_relay;     // Use separate alarm output
} alarm_config_t;

// Alarm limits
#define ALARM_PERCENT_MIN           1
#define ALARM_PERCENT_MAX           50

// ============================================================================
// FEED SCHEDULE STRUCTURE
// ============================================================================

typedef struct {
    bool enabled;                   // Schedule entry enabled
    uint8_t pump_index;             // Which pump (0-2)
    uint8_t day_of_week;            // 0-6 (Sun-Sat) or 0xFF for daily
    uint8_t hour;                   // 0-23
    uint8_t minute;                 // 0-59
    uint16_t pre_bleed_setpoint;    // Conductivity before feed (0=skip)
    uint16_t pre_bleed_duration;    // Max pre-bleed time (seconds)
    uint16_t feed_duration;         // Feed time (seconds)
    uint16_t lockout_duration;      // Post-feed lockout (seconds)
} feed_schedule_entry_t;

#define MAX_SCHEDULE_ENTRIES        12

// ============================================================================
// SYSTEM CONFIGURATION STRUCTURE (Main Config)
// ============================================================================

typedef struct {
    // Magic number and version for validation
    uint32_t magic;                 // CONFIG_MAGIC
    uint16_t version;               // Configuration version
    uint16_t checksum;              // CRC16 checksum

    // Subsystem configurations
    conductivity_config_t conductivity;
    blowdown_config_t blowdown;
    pump_config_t pumps[3];         // H2SO3, NaOH, Amine
    water_meter_config_t meters[2]; // WM1, WM2
    alarm_config_t alarms;
    feed_schedule_entry_t schedules[MAX_SCHEDULE_ENTRIES];

    // Network configuration
    char wifi_ssid[WIFI_SSID_MAX_LEN];
    char wifi_password[WIFI_PASS_MAX_LEN];
    char tsdb_host[TSDB_HOST_MAX_LEN];
    uint16_t tsdb_port;
    uint32_t log_interval_ms;

    // Security
    uint16_t access_code;           // 4-digit PIN
    bool access_code_enabled;

    // Display preferences
    uint8_t lcd_contrast;
    uint8_t led_brightness;
    bool display_in_ppm;            // Show conductivity as PPM

    // System timing
    int8_t timezone_offset;         // Hours from UTC
    bool dst_enabled;               // Daylight saving time

} system_config_t;

#define CONFIG_MAGIC                0x43543630  // "CT60" in hex
#define CONFIG_VERSION              1

// ============================================================================
// SYSTEM STATE STRUCTURE (Runtime State)
// ============================================================================

typedef enum {
    STATE_IDLE = 0,
    STATE_SAMPLING,
    STATE_HOLDING,
    STATE_BLOWING_DOWN,
    STATE_FEEDING,
    STATE_WAITING,
    STATE_CALIBRATING,
    STATE_ERROR,
    STATE_ALARM
} system_state_t;

typedef struct {
    // Current readings
    float conductivity_raw;         // Raw conductivity (uS/cm)
    float conductivity_compensated; // Temperature compensated
    float conductivity_calibrated;  // After user calibration
    float temperature_celsius;      // Current temperature

    // System state
    system_state_t state;
    uint32_t state_start_time;      // When current state started

    // Output states
    bool blowdown_active;
    bool pump_active[3];
    uint32_t blowdown_start_time;
    uint32_t pump_start_time[3];

    // Accumulated values (reset on power cycle unless persistent)
    uint32_t blowdown_total_time;   // Today's blowdown time
    uint32_t pump_total_time[3];    // Today's pump times
    uint32_t water_total_today;     // Today's water usage

    // Alarm states
    bool alarm_active;
    uint16_t active_alarms;         // Bitmask of active alarms

    // Network state
    bool wifi_connected;
    bool tsdb_connected;
    uint32_t last_log_time;

    // Timing
    uint32_t sample_timer;
    uint32_t hold_timer;
    uint32_t cycle_position;

} system_state_t_runtime;

// ============================================================================
// ALARM BITMASK DEFINITIONS
// ============================================================================

#define ALARM_NONE                  0x0000
#define ALARM_COND_HIGH             0x0001
#define ALARM_COND_LOW              0x0002
#define ALARM_BLOWDOWN_TIMEOUT      0x0004
#define ALARM_FEED1_TIMEOUT         0x0008
#define ALARM_FEED2_TIMEOUT         0x0010
#define ALARM_FEED3_TIMEOUT         0x0020
#define ALARM_NO_FLOW               0x0040
#define ALARM_SENSOR_ERROR          0x0080
#define ALARM_TEMP_ERROR            0x0100
#define ALARM_DRUM_LEVEL_1          0x0200
#define ALARM_DRUM_LEVEL_2          0x0400
#define ALARM_WIFI_DISCONNECT       0x0800
#define ALARM_CALIBRATION_DUE       0x1000

// ============================================================================
// NVS STORAGE KEYS
// ============================================================================

#define NVS_NAMESPACE               "boiler_cfg"
#define NVS_KEY_CONFIG              "config"
#define NVS_KEY_WM1_TOTAL           "wm1_total"
#define NVS_KEY_WM2_TOTAL           "wm2_total"
#define NVS_KEY_PUMP1_TOTAL         "pump1_tot"
#define NVS_KEY_PUMP2_TOTAL         "pump2_tot"
#define NVS_KEY_PUMP3_TOTAL         "pump3_tot"
#define NVS_KEY_BLOW_TOTAL          "blow_total"
#define NVS_KEY_LAST_CAL_DATE       "last_cal"

// ============================================================================
// FREERTOS TASK CONFIGURATION
// ============================================================================

#define TASK_PRIORITY_SAFETY        5   // Highest priority
#define TASK_PRIORITY_CONTROL       4
#define TASK_PRIORITY_MEASUREMENT   3
#define TASK_PRIORITY_DISPLAY       2
#define TASK_PRIORITY_LOGGING       1   // Lowest priority

#define TASK_STACK_SAFETY           2048
#define TASK_STACK_CONTROL          4096
#define TASK_STACK_MEASUREMENT      4096
#define TASK_STACK_DISPLAY          4096
#define TASK_STACK_LOGGING          8192

#define TASK_PERIOD_SAFETY_MS       100     // 10 Hz
#define TASK_PERIOD_CONTROL_MS      100     // 10 Hz
#define TASK_PERIOD_MEASUREMENT_MS  500     // 2 Hz
#define TASK_PERIOD_DISPLAY_MS      200     // 5 Hz
#define TASK_PERIOD_LOGGING_MS      1000    // 1 Hz (actual log rate configurable)

// ============================================================================
// UTILITY MACROS
// ============================================================================

#define ARRAY_SIZE(arr)             (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b)                   ((a) < (b) ? (a) : (b))
#define MAX(a, b)                   ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi)            (MIN(MAX(x, lo), hi))

#endif // CONFIG_H

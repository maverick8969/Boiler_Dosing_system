/**
 * @file sd_logger.h
 * @brief SD Card Data Logger for Columbia CT-6 Boiler Controller
 *
 * Provides persistent local storage on a micro-SD card module connected
 * to the ESP32 VSPI bus (shared with MAX31865 PT1000 RTD).
 *
 * Features:
 * - Always-on local CSV logging (audit trail independent of WiFi)
 * - Daily log files: /logs/YYYY-MM-DD.csv
 * - Falls back to sequential filenames when NTP time is unavailable
 * - Event and alarm logging in separate files
 * - SPI bus mutex for safe sharing with MAX31865
 *
 * Hardware:
 * - Standard micro-SD card module (SPI interface)
 * - CS = GPIO19, shared VSPI bus (MOSI=23, MISO=39, SCK=18)
 * - FAT32 formatted card (up to 32 GB)
 */

#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "config.h"

// Forward declaration
struct sensor_reading_t;

// ============================================================================
// SD CARD STATUS
// ============================================================================

typedef enum {
    SD_STATUS_OK = 0,           // Mounted and writable
    SD_STATUS_NO_CARD,          // No card physically detected
    SD_STATUS_MOUNT_FAILED,     // Card detected but filesystem mount failed (needs format)
    SD_STATUS_NOT_INITIALIZED   // begin() hasn't been called yet
} sd_card_status_t;

// ============================================================================
// SD LOGGER CONFIGURATION
// ============================================================================

#define SD_LOG_DIR              "/logs"
#define SD_EVENT_DIR            "/events"
#define SD_MAX_FILENAME_LEN     32
#define SD_FLUSH_INTERVAL_MS    30000   // Flush file every 30 seconds
#define SD_CSV_HEADER           "timestamp,conductivity,temperature,wm1_gal,wm2_gal," \
                                "flow_gpm,blowdown,valve_mA,pump1,pump2,pump3," \
                                "fw_pump,fw_cycles,fw_ontime_s,alarms," \
                                "safe_mode,cond_valid,temp_valid," \
                                "dev_operational,dev_faulted,dev_faulted_mask,meas_age_ms"

// ============================================================================
// SD LOGGER CLASS
// ============================================================================

class SDLogger {
public:
    SDLogger();

    /**
     * @brief Initialize SD card on shared VSPI bus
     * @param csPin Chip select pin (GPIO19)
     * @param spi Reference to SPIClass (VSPI)
     * @param mutex FreeRTOS mutex for SPI bus access
     * @return true if SD card detected and mounted
     */
    bool begin(uint8_t csPin, SPIClass& spi, SemaphoreHandle_t mutex);

    /**
     * @brief Check if SD card is available
     * @return true if card is mounted and writable
     */
    bool isAvailable();

    /**
     * @brief Log a sensor reading as CSV row
     * @param reading Pointer to sensor reading structure
     * @return true if written successfully
     */
    bool logReading(const sensor_reading_t* reading);

    /**
     * @brief Log a system event
     * @param event_type Event type string (e.g., "FW_PUMP_ON")
     * @param description Human-readable description
     * @param value Optional numeric value
     * @return true if written successfully
     */
    bool logEvent(const char* event_type, const char* description, int32_t value = 0);

    /**
     * @brief Log an alarm state change
     * @param alarm_code Alarm bitmask code
     * @param alarm_name Human-readable alarm name
     * @param active true if alarm became active, false if cleared
     * @param trigger_value Sensor value that triggered the alarm
     * @return true if written successfully
     */
    bool logAlarm(uint16_t alarm_code, const char* alarm_name,
                  bool active, float trigger_value);

    /**
     * @brief Flush buffered writes to SD card
     */
    void flush();

    /**
     * @brief Call periodically to handle file rotation and flushing
     */
    void update();

    /**
     * @brief Get total records written today
     * @return Record count for current daily file
     */
    uint32_t getRecordsToday();

    /**
     * @brief Get SD card total size
     * @return Size in megabytes
     */
    uint32_t getCardSizeMB();

    /**
     * @brief Get SD card used space
     * @return Used space in megabytes
     */
    uint32_t getUsedSpaceMB();

    /**
     * @brief Get current log filename
     * @return Pointer to filename string
     */
    const char* getCurrentFilename();

    /**
     * @brief Get SD card status
     * @return SD_STATUS_OK, SD_STATUS_NO_CARD, SD_STATUS_MOUNT_FAILED, or SD_STATUS_NOT_INITIALIZED
     */
    sd_card_status_t getCardStatus();

    /**
     * @brief Format the SD card as FAT32 and mount it
     *
     * Destroys all existing data on the card. Use for blank or
     * corrupted cards that fail to mount during begin().
     * Requires SPI mutex — blocks until format completes.
     *
     * @return true if format and mount succeeded
     */
    bool formatCard();

private:
    // Hardware
    uint8_t _csPin;
    SPIClass* _spi;
    SemaphoreHandle_t _spiMutex;

    // State
    bool _available;
    bool _headerWritten;
    sd_card_status_t _card_status;
    File _dataFile;
    char _currentFilename[SD_MAX_FILENAME_LEN];
    char _currentDate[12];              // "YYYY-MM-DD"
    uint32_t _recordsToday;
    uint32_t _lastFlushTime;
    uint32_t _bootSequence;             // Fallback sequence number when no NTP

    // Internal methods
    bool ensureDailyFile();
    bool ensureDirectory(const char* dir);
    void getDateString(char* buf, size_t len);
    bool takeSPI(uint32_t timeout_ms = 1000);
    void giveSPI();
    bool writeCSVRow(const char* line);
    bool writeEventLine(const char* filename, const char* line);
};

extern SDLogger sdLogger;

#endif // SD_LOGGER_H

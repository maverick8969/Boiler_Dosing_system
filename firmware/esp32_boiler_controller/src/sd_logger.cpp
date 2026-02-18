/**
 * @file sd_logger.cpp
 * @brief SD Card Data Logger Implementation
 *
 * Logs sensor readings, events, and alarms to a micro-SD card as CSV files.
 * Shares the ESP32 VSPI bus with the MAX31865 PT1000 RTD; all SPI access
 * is protected by a FreeRTOS mutex.
 */

#include "sd_logger.h"
#include "data_logger.h"
#include "pin_definitions.h"
#include <time.h>

// Global instance
SDLogger sdLogger;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SDLogger::SDLogger()
    : _csPin(0)
    , _spi(nullptr)
    , _spiMutex(nullptr)
    , _available(false)
    , _headerWritten(false)
    , _recordsToday(0)
    , _lastFlushTime(0)
    , _bootSequence(0)
{
    memset(_currentFilename, 0, sizeof(_currentFilename));
    memset(_currentDate, 0, sizeof(_currentDate));
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool SDLogger::begin(uint8_t csPin, SPIClass& spi, SemaphoreHandle_t mutex) {
    _csPin = csPin;
    _spi = &spi;
    _spiMutex = mutex;

    Serial.println("Initializing SD card logger...");
    Serial.printf("  SD CS=GPIO%d, SPI bus shared with MAX31865\n", _csPin);

    if (!takeSPI(2000)) {
        Serial.println("  ERROR: Could not acquire SPI mutex for SD init");
        _available = false;
        return false;
    }

    if (!SD.begin(_csPin, *_spi, SD_SPI_FREQ)) {
        giveSPI();
        Serial.println("  WARNING: SD card not detected or mount failed");
        _available = false;
        return false;
    }

    uint8_t cardType = SD.cardType();
    giveSPI();

    if (cardType == CARD_NONE) {
        Serial.println("  WARNING: No SD card inserted");
        _available = false;
        return false;
    }

    _available = true;

    const char* typeStr = "UNKNOWN";
    if (cardType == CARD_MMC)  typeStr = "MMC";
    if (cardType == CARD_SD)   typeStr = "SD";
    if (cardType == CARD_SDHC) typeStr = "SDHC";

    Serial.printf("  SD card type: %s, size: %lu MB\n", typeStr, getCardSizeMB());

    // Create log directories
    if (!takeSPI()) { _available = false; return false; }
    ensureDirectory(SD_LOG_DIR);
    ensureDirectory(SD_EVENT_DIR);
    giveSPI();

    Serial.println("  SD card logger initialized successfully");
    return true;
}

bool SDLogger::isAvailable() {
    return _available;
}

// ============================================================================
// SENSOR READING LOGGING
// ============================================================================

bool SDLogger::logReading(const sensor_reading_t* reading) {
    if (!_available || !reading) return false;

    if (!takeSPI()) return false;

    // Ensure we have the right daily file open
    if (!ensureDailyFile()) {
        giveSPI();
        return false;
    }

    // Format CSV row
    char line[384];
    snprintf(line, sizeof(line),
        "%lu,%.1f,%.1f,%lu,%lu,%.2f,%d,%.1f,%d,%d,%d,%d,%lu,%lu,0x%04X,"
        "%u,%d,%d,%u,%u,0x%04X,%lu",
        reading->timestamp,
        reading->conductivity,
        reading->temperature,
        reading->water_meter1,
        reading->water_meter2,
        reading->flow_rate,
        reading->blowdown_active ? 1 : 0,
        reading->valve_position_mA,
        reading->pump1_active ? 1 : 0,
        reading->pump2_active ? 1 : 0,
        reading->pump3_active ? 1 : 0,
        reading->feedwater_pump_on ? 1 : 0,
        reading->fw_pump_cycle_count,
        reading->fw_pump_on_time_sec,
        reading->active_alarms,
        reading->safe_mode,
        reading->cond_sensor_valid ? 1 : 0,
        reading->temp_sensor_valid ? 1 : 0,
        reading->devices_operational,
        reading->devices_faulted,
        reading->devices_faulted_mask,
        reading->measurement_age_ms
    );

    bool ok = writeCSVRow(line);
    giveSPI();

    if (ok) {
        _recordsToday++;
    }

    return ok;
}

// ============================================================================
// EVENT LOGGING
// ============================================================================

bool SDLogger::logEvent(const char* event_type, const char* description, int32_t value) {
    if (!_available) return false;

    char dateStr[12];
    getDateString(dateStr, sizeof(dateStr));

    char filename[SD_MAX_FILENAME_LEN];
    snprintf(filename, sizeof(filename), "%s/%s_events.csv", SD_EVENT_DIR, dateStr);

    char line[256];
    snprintf(line, sizeof(line), "%lu,%s,\"%s\",%ld",
             (unsigned long)(millis() / 1000), event_type, description, (long)value);

    if (!takeSPI()) return false;
    bool ok = writeEventLine(filename, line);
    giveSPI();

    return ok;
}

bool SDLogger::logAlarm(uint16_t alarm_code, const char* alarm_name,
                         bool active, float trigger_value) {
    if (!_available) return false;

    char dateStr[12];
    getDateString(dateStr, sizeof(dateStr));

    char filename[SD_MAX_FILENAME_LEN];
    snprintf(filename, sizeof(filename), "%s/%s_events.csv", SD_EVENT_DIR, dateStr);

    char line[256];
    snprintf(line, sizeof(line), "%lu,ALARM_%s,\"%s %s\",%.1f",
             (unsigned long)(millis() / 1000),
             active ? "ON" : "OFF",
             alarm_name,
             active ? "ACTIVE" : "CLEARED",
             trigger_value);

    if (!takeSPI()) return false;
    bool ok = writeEventLine(filename, line);
    giveSPI();

    return ok;
}

// ============================================================================
// MAINTENANCE
// ============================================================================

void SDLogger::flush() {
    if (!_available) return;

    if (!takeSPI()) return;
    if (_dataFile) {
        _dataFile.flush();
    }
    giveSPI();

    _lastFlushTime = millis();
}

void SDLogger::update() {
    if (!_available) return;

    // Periodic flush
    if (millis() - _lastFlushTime >= SD_FLUSH_INTERVAL_MS) {
        flush();
    }
}

// ============================================================================
// STATUS QUERIES
// ============================================================================

uint32_t SDLogger::getRecordsToday() {
    return _recordsToday;
}

uint32_t SDLogger::getCardSizeMB() {
    if (!_available) return 0;
    return (uint32_t)(SD.cardSize() / (1024 * 1024));
}

uint32_t SDLogger::getUsedSpaceMB() {
    if (!_available) return 0;
    return (uint32_t)(SD.usedBytes() / (1024 * 1024));
}

const char* SDLogger::getCurrentFilename() {
    return _currentFilename;
}

// ============================================================================
// INTERNAL METHODS
// ============================================================================

bool SDLogger::ensureDailyFile() {
    // Get current date string
    char dateStr[12];
    getDateString(dateStr, sizeof(dateStr));

    // Check if we need to open a new file (date changed or first call)
    if (strcmp(dateStr, _currentDate) != 0) {
        // Close previous file if open
        if (_dataFile) {
            _dataFile.flush();
            _dataFile.close();
        }

        strncpy(_currentDate, dateStr, sizeof(_currentDate));
        snprintf(_currentFilename, sizeof(_currentFilename),
                 "%s/%s.csv", SD_LOG_DIR, dateStr);

        _headerWritten = false;
        _recordsToday = 0;
    }

    // Open file if not already open
    if (!_dataFile) {
        // Check if file already exists (to skip header)
        _headerWritten = SD.exists(_currentFilename);

        _dataFile = SD.open(_currentFilename, FILE_APPEND);
        if (!_dataFile) {
            Serial.printf("ERROR: Could not open SD log file: %s\n", _currentFilename);
            return false;
        }
    }

    // Write CSV header if this is a new file
    if (!_headerWritten) {
        _dataFile.println(SD_CSV_HEADER);
        _headerWritten = true;
    }

    return true;
}

bool SDLogger::ensureDirectory(const char* dir) {
    if (!SD.exists(dir)) {
        return SD.mkdir(dir);
    }
    return true;
}

void SDLogger::getDateString(char* buf, size_t len) {
    struct tm timeinfo;

    // Try to get NTP time (set by dataLogger.syncTime())
    if (getLocalTime(&timeinfo, 0)) {
        strftime(buf, len, "%Y-%m-%d", &timeinfo);
    } else {
        // No NTP — use boot sequence number
        snprintf(buf, len, "boot_%04lu", _bootSequence);
    }
}

bool SDLogger::takeSPI(uint32_t timeout_ms) {
    if (!_spiMutex) return true;  // No mutex = no contention
    return xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void SDLogger::giveSPI() {
    if (_spiMutex) {
        xSemaphoreGive(_spiMutex);
    }
}

bool SDLogger::writeCSVRow(const char* line) {
    if (!_dataFile) return false;
    _dataFile.println(line);
    return true;
}

bool SDLogger::writeEventLine(const char* filename, const char* line) {
    File f = SD.open(filename, FILE_APPEND);
    if (!f) return false;
    f.println(line);
    f.close();
    return true;
}

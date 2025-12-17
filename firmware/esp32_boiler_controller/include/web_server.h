/**
 * @file web_server.h
 * @brief ESP32 Web Server for Manual Test Input and Status Display
 *
 * Provides a mobile-friendly web interface for:
 * - Entering manual water test results (Alkalinity, Sulfite, pH)
 * - Viewing current system status
 * - Viewing fuzzy logic state and recommendations
 * - Basic configuration
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "config.h"
#include "fuzzy_logic.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

#define WEB_SERVER_PORT         80
#define WEB_API_PREFIX          "/api"
#define WEB_MAX_JSON_SIZE       2048

// ============================================================================
// WEB SERVER CLASS
// ============================================================================

class BoilerWebServer {
public:
    BoilerWebServer();

    /**
     * @brief Initialize web server
     * @param config Pointer to system configuration
     * @param fuzzy Pointer to fuzzy controller
     * @return true if successful
     */
    bool begin(system_config_t* config, FuzzyController* fuzzy);

    /**
     * @brief Process incoming requests (call in loop)
     */
    void handleClient();

    /**
     * @brief Check if server is running
     */
    bool isRunning() { return _running; }

    /**
     * @brief Stop web server
     */
    void stop();

    /**
     * @brief Set callback for when test inputs are updated
     */
    void setTestInputCallback(void (*callback)(fuzzy_input_t, float, bool));

    /**
     * @brief Update current readings for status display
     */
    void updateReadings(float conductivity, float temperature, float flow_rate);

    /**
     * @brief Update fuzzy output for display
     */
    void updateFuzzyOutput(const fuzzy_result_t& result);

private:
    WebServer _server;
    system_config_t* _config;
    FuzzyController* _fuzzy;
    bool _running;

    // Current readings cache
    float _current_conductivity;
    float _current_temperature;
    float _current_flow_rate;
    fuzzy_result_t _current_fuzzy_result;

    // Manual test values (with timestamps)
    struct {
        float value;
        uint32_t timestamp;     // millis() when entered
        bool valid;
    } _manual_tests[3];         // [0]=Alk, [1]=Sulfite, [2]=pH

    // Callback
    void (*_test_input_callback)(fuzzy_input_t, float, bool);

    // Route handlers
    void handleRoot();
    void handleGetStatus();
    void handleGetFuzzy();
    void handlePostTest();
    void handleGetTests();
    void handleClearTests();
    void handleNotFound();

    // HTML generators
    String generateIndexHTML();
    String generateCSS();
    String generateJS();

    // CORS headers
    void sendCORSHeaders();
};

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

extern BoilerWebServer webServer;

#endif // WEB_SERVER_H

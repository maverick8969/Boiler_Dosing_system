/**
 * @file web_server.h
 * @brief ESP32 Web Server for HMI (Modern IoT Stack: REST + WebSocket)
 *
 * Provides:
 * - REST: /api/state, /api/health, /api/command/{name}, /api/config, plus legacy routes
 * - WebSocket /ws for live updates (no polling)
 * - Mobile-friendly web UI for manual tests, status, fuzzy logic
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "fuzzy_logic.h"
#include "device_manager.h"
#include "sensor_health.h"
#include "self_test.h"
#include "sd_logger.h"

#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define WEB_SERVER_PORT         80
#define WEB_API_PREFIX          "/api"
#define WEB_MAX_JSON_SIZE       2048
#define WEB_WS_PATH             "/ws"

// Command handler: return true if command accepted (202), false to reject (400).
// request_id is provided so the handler can queue the command and later broadcast result via broadcastCommandResult(request_id, "completed"|"failed", message).
typedef bool (*WebServerCommandHandlerFn)(const char* request_id, const char* name, const JsonObject& params, String* outMessage);

// ============================================================================
// WEB SERVER CLASS
// ============================================================================

class BoilerWebServer {
public:
    BoilerWebServer();

    /**
     * @brief Initialize async web server and WebSocket
     */
    bool begin(system_config_t* config, FuzzyController* fuzzy);

    /**
     * @brief Call from loop: cleanup closed WebSocket clients
     */
    void handleClient();

    bool isRunning() { return _running; }
    void stop();

    void setTestInputCallback(void (*callback)(fuzzy_input_t, float, bool));

    void updateReadings(float conductivity, float temperature, float flow_rate);
    void updateFuzzyOutput(const fuzzy_result_t& result);
    void checkManualTestExpiry();
    void applyEstimatedPhIfNeeded();

    /** Broadcast a JSON message to all WebSocket clients (Modern IoT Stack) */
    void broadcastWs(const char* type, const char* payloadJson);
    /** Broadcast command result: type "command_result", payload { request_id, result, message } */
    void broadcastCommandResult(const char* request_id, const char* result, const char* message);

    /** Push current state to all WebSocket clients (call periodically, e.g. every 1–2 s). */
    void broadcastState();

    /** Register handler for POST /api/command/{name}. Called from main/control. */
    void setCommandHandler(WebServerCommandHandlerFn fn) { _command_handler = fn; }

    /** Optional: set MQTT connected flag for GET /api/health */
    void setMqttConnected(bool connected) { _mqtt_connected = connected; }

private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;
    system_config_t* _config;
    FuzzyController* _fuzzy;
    bool _running;
    bool _mqtt_connected;

    float _current_conductivity;
    float _current_temperature;
    float _current_flow_rate;
    fuzzy_result_t _current_fuzzy_result;

    struct {
        float value;
        uint32_t timestamp;
        bool valid;
    } _manual_tests[5];

    void (*_test_input_callback)(fuzzy_input_t, float, bool);
    WebServerCommandHandlerFn _command_handler;

    void setupRoutes();
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);

    String buildStateJson();
    String buildHealthJson();

    void handleRoot(AsyncWebServerRequest* request);
    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetState(AsyncWebServerRequest* request);
    void handleGetHealth(AsyncWebServerRequest* request);
    void handleGetFuzzy(AsyncWebServerRequest* request);
    void handleGetDevices(AsyncWebServerRequest* request);
    void handleGetSDStatus(AsyncWebServerRequest* request);
    void handlePostSDFormat(AsyncWebServerRequest* request);
    void handlePostTest(AsyncWebServerRequest* request);
    void handleGetTests(AsyncWebServerRequest* request);
    void handleClearTests(AsyncWebServerRequest* request);
    void handlePostCommand(AsyncWebServerRequest* request);
    void handlePostConfig(AsyncWebServerRequest* request);
    void handleNotFound(AsyncWebServerRequest* request);

    String generateIndexHTML();
    String generateCSS();
    String generateJS();

    void sendCORSHeaders(AsyncWebServerRequest* request);

    /**
     * @brief Check authentication for state-changing POST endpoints (F1).
     * When access_code_enabled, requires X-API-Key header or access_code in JSON body.
     * @return true if auth disabled or credentials valid; false to respond 401.
     */
    bool checkPostAuth(AsyncWebServerRequest* request, const String& body);
};

extern BoilerWebServer webServer;

#endif /* WEB_SERVER_H */

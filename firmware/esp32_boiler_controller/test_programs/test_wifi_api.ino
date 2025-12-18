/**
 * @file test_wifi_api.ino
 * @brief Test program for WiFi connectivity and API communication
 *
 * Tests:
 * - WiFi connection to network
 * - HTTP client to backend API
 * - JSON data posting
 * - Web server (local AP)
 *
 * Hardware:
 * - ESP32 with WiFi capability
 *
 * Usage:
 * - Edit WiFi credentials below
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ============================================================================
// CONFIGURATION - EDIT THESE
// ============================================================================

// Your WiFi network
const char* WIFI_SSID = "YourWiFiSSID";
const char* WIFI_PASS = "YourWiFiPassword";

// Backend API (Raspberry Pi)
const char* API_HOST = "192.168.1.100";
const int API_PORT = 3000;
const char* API_KEY = "your-secret-api-key-here";

// AP mode settings (for local testing)
const char* AP_SSID = "BoilerController-Test";
const char* AP_PASS = "test1234";

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

WebServer server(80);
bool wifiConnected = false;
bool apMode = false;

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  WIFI & API TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();

    Serial.println("WiFi Configuration:");
    Serial.printf("  SSID: %s\n", WIFI_SSID);
    Serial.printf("  API Host: %s:%d\n", API_HOST, API_PORT);
    Serial.println();

    printMenu();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    // Handle web server if running
    if (apMode || wifiConnected) {
        server.handleClient();
    }

    // Process serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        processCommand(cmd);
    }

    delay(10);
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(char cmd) {
    switch (cmd) {
        case 'w':
            connectWiFi();
            break;

        case 'd':
            disconnectWiFi();
            break;

        case 'a':
            startAPMode();
            break;

        case 's':
            scanNetworks();
            break;

        case 'p':
            pingBackend();
            break;

        case 't':
            testPostData();
            break;

        case 'f':
            testPostFuzzy();
            break;

        case 'g':
            testGetLatest();
            break;

        case 'l':
            startLocalServer();
            break;

        case 'i':
            printNetworkInfo();
            break;

        case 'r':
            stressTestAPI();
            break;

        case 'h':
        case '?':
            printMenu();
            break;

        case '\n':
        case '\r':
            break;

        default:
            Serial.printf("Unknown command: '%c'\n", cmd);
            break;
    }
}

// ============================================================================
// WIFI FUNCTIONS
// ============================================================================

void connectWiFi() {
    Serial.println();
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("WiFi connected!");
        Serial.printf("  IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    } else {
        Serial.println("WiFi connection FAILED!");
        Serial.println("Check SSID and password");
    }
}

void disconnectWiFi() {
    WiFi.disconnect();
    wifiConnected = false;
    apMode = false;
    Serial.println("WiFi disconnected");
}

void startAPMode() {
    Serial.println();
    Serial.printf("Starting AP mode: %s\n", AP_SSID);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    apMode = true;
    wifiConnected = false;

    Serial.println("AP started!");
    Serial.printf("  SSID: %s\n", AP_SSID);
    Serial.printf("  Password: %s\n", AP_PASS);
    Serial.printf("  IP: %s\n", WiFi.softAPIP().toString().c_str());

    startLocalServer();
}

void scanNetworks() {
    Serial.println();
    Serial.println("Scanning for WiFi networks...");

    int n = WiFi.scanNetworks();

    if (n == 0) {
        Serial.println("No networks found");
    } else {
        Serial.printf("Found %d networks:\n", n);
        for (int i = 0; i < n; i++) {
            Serial.printf("  %2d: %-32s  Ch:%2d  RSSI:%4d  %s\n",
                          i + 1,
                          WiFi.SSID(i).c_str(),
                          WiFi.channel(i),
                          WiFi.RSSI(i),
                          WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Secured");
        }
    }

    WiFi.scanDelete();
}

void printNetworkInfo() {
    Serial.println();
    Serial.println("=== NETWORK INFO ===");
    Serial.printf("WiFi Mode: %s\n",
                  WiFi.getMode() == WIFI_STA ? "Station" :
                  WiFi.getMode() == WIFI_AP ? "AP" :
                  WiFi.getMode() == WIFI_AP_STA ? "AP+Station" : "Off");

    if (wifiConnected) {
        Serial.println("\nStation Mode:");
        Serial.printf("  Connected to: %s\n", WiFi.SSID().c_str());
        Serial.printf("  IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  Subnet: %s\n", WiFi.subnetMask().toString().c_str());
        Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("  DNS: %s\n", WiFi.dnsIP().toString().c_str());
        Serial.printf("  MAC: %s\n", WiFi.macAddress().c_str());
        Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    }

    if (apMode) {
        Serial.println("\nAP Mode:");
        Serial.printf("  SSID: %s\n", AP_SSID);
        Serial.printf("  IP: %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("  Stations: %d\n", WiFi.softAPgetStationNum());
    }

    Serial.printf("\nFree heap: %d bytes\n", ESP.getFreeHeap());
}

// ============================================================================
// API FUNCTIONS
// ============================================================================

void pingBackend() {
    if (!wifiConnected) {
        Serial.println("ERROR: Not connected to WiFi");
        return;
    }

    Serial.println();
    Serial.printf("Pinging backend at %s:%d...\n", API_HOST, API_PORT);

    HTTPClient http;
    String url = String("http://") + API_HOST + ":" + API_PORT + "/health";

    http.begin(url);
    http.setTimeout(5000);

    int httpCode = http.GET();

    if (httpCode > 0) {
        Serial.printf("Response code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println("Response: " + payload);
            Serial.println("Backend is ONLINE!");
        }
    } else {
        Serial.printf("Connection failed: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
}

void testPostData() {
    if (!wifiConnected) {
        Serial.println("ERROR: Not connected to WiFi");
        return;
    }

    Serial.println();
    Serial.println("Posting test sensor data...");

    HTTPClient http;
    String url = String("http://") + API_HOST + ":" + API_PORT + "/api/readings";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", API_KEY);
    http.setTimeout(5000);

    // Create JSON payload
    JsonDocument doc;
    doc["conductivity"] = 2500 + random(-100, 100);
    doc["temperature"] = 42.0 + random(0, 50) / 10.0;
    doc["water_meter1"] = 1247 + random(0, 10);
    doc["water_meter2"] = 0;
    doc["flow_rate"] = 1.2 + random(-5, 5) / 10.0;
    doc["blowdown_active"] = false;
    doc["pump1_active"] = random(0, 2) == 1;
    doc["pump2_active"] = random(0, 2) == 1;
    doc["pump3_active"] = random(0, 2) == 1;
    doc["active_alarms"] = 0;

    String payload;
    serializeJson(doc, payload);

    Serial.println("Payload: " + payload);

    int httpCode = http.POST(payload);

    if (httpCode > 0) {
        Serial.printf("Response code: %d\n", httpCode);
        String response = http.getString();
        Serial.println("Response: " + response);

        if (httpCode == HTTP_CODE_OK) {
            Serial.println("Data posted successfully!");
        }
    } else {
        Serial.printf("POST failed: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
}

void testPostFuzzy() {
    if (!wifiConnected) {
        Serial.println("ERROR: Not connected to WiFi");
        return;
    }

    Serial.println();
    Serial.println("Posting test fuzzy output data...");

    HTTPClient http;
    String url = String("http://") + API_HOST + ":" + API_PORT + "/api/fuzzy";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", API_KEY);

    JsonDocument doc;
    doc["blowdown_rate"] = random(0, 100);
    doc["caustic_rate"] = random(0, 100);
    doc["sulfite_rate"] = random(0, 100);
    doc["acid_rate"] = random(0, 30);
    doc["confidence"] = "MEDIUM";
    doc["active_rules"] = random(5, 15);
    doc["max_firing"] = random(50, 100) / 100.0;

    String payload;
    serializeJson(doc, payload);

    Serial.println("Payload: " + payload);

    int httpCode = http.POST(payload);

    if (httpCode > 0) {
        Serial.printf("Response code: %d\n", httpCode);
        String response = http.getString();
        Serial.println("Response: " + response);
    } else {
        Serial.printf("POST failed: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
}

void testGetLatest() {
    if (!wifiConnected) {
        Serial.println("ERROR: Not connected to WiFi");
        return;
    }

    Serial.println();
    Serial.println("Getting latest readings from backend...");

    HTTPClient http;
    String url = String("http://") + API_HOST + ":" + API_PORT + "/api/latest";

    http.begin(url);

    int httpCode = http.GET();

    if (httpCode > 0) {
        Serial.printf("Response code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println("Response:");

            // Pretty print JSON
            JsonDocument doc;
            deserializeJson(doc, payload);
            serializeJsonPretty(doc, Serial);
            Serial.println();
        }
    } else {
        Serial.printf("GET failed: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
}

void stressTestAPI() {
    if (!wifiConnected) {
        Serial.println("ERROR: Not connected to WiFi");
        return;
    }

    Serial.println();
    Serial.println("=== API STRESS TEST ===");
    Serial.println("Posting 100 requests...");

    int success = 0;
    int failed = 0;
    uint32_t startTime = millis();

    for (int i = 0; i < 100; i++) {
        HTTPClient http;
        String url = String("http://") + API_HOST + ":" + API_PORT + "/api/readings";

        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-API-Key", API_KEY);
        http.setTimeout(2000);

        JsonDocument doc;
        doc["conductivity"] = 2500;
        doc["temperature"] = 42.0;
        doc["water_meter1"] = i;

        String payload;
        serializeJson(doc, payload);

        int httpCode = http.POST(payload);

        if (httpCode == HTTP_CODE_OK) {
            success++;
        } else {
            failed++;
        }

        http.end();

        Serial.printf("\rProgress: %d/100  Success: %d  Failed: %d", i + 1, success, failed);
    }

    uint32_t elapsed = millis() - startTime;

    Serial.println();
    Serial.println();
    Serial.println("Stress test complete!");
    Serial.printf("  Success: %d\n", success);
    Serial.printf("  Failed: %d\n", failed);
    Serial.printf("  Time: %lu ms\n", elapsed);
    Serial.printf("  Rate: %.1f requests/sec\n", 100.0 / (elapsed / 1000.0));
}

// ============================================================================
// LOCAL WEB SERVER
// ============================================================================

void startLocalServer() {
    Serial.println("Starting local web server...");

    server.on("/", handleRoot);
    server.on("/api/status", handleStatus);
    server.onNotFound(handleNotFound);

    server.begin();

    Serial.println("Web server started!");
    if (apMode) {
        Serial.printf("Access at: http://%s/\n", WiFi.softAPIP().toString().c_str());
    } else if (wifiConnected) {
        Serial.printf("Access at: http://%s/\n", WiFi.localIP().toString().c_str());
    }
}

void handleRoot() {
    String html = "<html><head><title>Boiler Controller Test</title></head>";
    html += "<body><h1>ESP32 Boiler Controller</h1>";
    html += "<p>WiFi Test Server Running</p>";
    html += "<p>Free heap: " + String(ESP.getFreeHeap()) + " bytes</p>";
    html += "<p><a href='/api/status'>View Status JSON</a></p>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleStatus() {
    JsonDocument doc;

    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["test_conductivity"] = 2500;
    doc["test_temperature"] = 42.0;

    String response;
    serializeJson(doc, response);

    server.send(200, "application/json", response);
}

void handleNotFound() {
    server.send(404, "text/plain", "Not Found");
}

// ============================================================================
// HELP
// ============================================================================

void printMenu() {
    Serial.println();
    Serial.println("=== WIFI & API TEST MENU ===");
    Serial.println();
    Serial.println("WiFi:");
    Serial.println("  w - Connect to WiFi");
    Serial.println("  d - Disconnect WiFi");
    Serial.println("  a - Start AP mode");
    Serial.println("  s - Scan for networks");
    Serial.println("  i - Print network info");
    Serial.println();
    Serial.println("Backend API:");
    Serial.println("  p - Ping backend (health check)");
    Serial.println("  t - Test POST sensor data");
    Serial.println("  f - Test POST fuzzy data");
    Serial.println("  g - GET latest readings");
    Serial.println("  r - Stress test (100 requests)");
    Serial.println();
    Serial.println("Local Server:");
    Serial.println("  l - Start local web server");
    Serial.println();
    Serial.println("Other:");
    Serial.println("  h - Show this menu");
    Serial.println();
    Serial.println("NOTE: Edit WIFI_SSID, WIFI_PASS, API_HOST at top of file");
    Serial.println();
}

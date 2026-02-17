/**
 * @file test_ezo_conductivity.cpp
 * @brief Test program for Atlas Scientific EZO-EC + Adafruit MAX31865 PT1000
 *
 * Tests:
 * - Atlas Scientific EZO-EC conductivity circuit via UART
 * - Adafruit MAX31865 PT1000 RTD temperature sensor via SPI
 * - Temperature-compensated conductivity readings
 * - EZO calibration procedures
 * - EZO command passthrough
 *
 * Hardware:
 * - Atlas Scientific EZO-EC on Serial2 (TX=GPIO25, RX=GPIO36)
 * - Adafruit MAX31865 on software SPI (CS=GPIO16, MOSI=GPIO23, MISO=GPIO39, SCK=GPIO18)
 * - Sensorex CS675HTTC/P1K conductivity probe connected to EZO-EC
 * - PT1000 RTD connected to MAX31865
 *
 * Usage:
 * - Upload to ESP32 using: pio run -e test_conductivity_sensor -t upload
 * - Open Serial Monitor at 115200 baud
 * - Follow menu prompts for testing and calibration
 */

#include <Arduino.h>
#include <Adafruit_MAX31865.h>

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// Atlas Scientific EZO-EC UART
#define EZO_TX_PIN          25      // ESP32 TX → EZO RX
#define EZO_RX_PIN          36      // ESP32 RX ← EZO TX
#define EZO_BAUD            9600

// Adafruit MAX31865 Software SPI
#define RTD_CS_PIN          16
#define RTD_MOSI_PIN        23
#define RTD_MISO_PIN        39
#define RTD_SCK_PIN         18

// PT1000 configuration
#define RTD_NOMINAL         1000.0  // Nominal resistance at 0°C
#define RTD_REFERENCE       4300.0  // Reference resistor on MAX31865 board

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

Adafruit_MAX31865 rtd(RTD_CS_PIN, RTD_MOSI_PIN, RTD_MISO_PIN, RTD_SCK_PIN);

bool continuousMode = false;
float lastEC = 0;
float lastTDS = 0;
float lastTemp = 0;
bool ezoReady = false;
bool rtdReady = false;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void printMenu();
void processCommand(String cmd);
void testEZO();
void testRTD();
void readConductivity();
void readTemperature();
void readCombined();
void startContinuous();
void stopContinuous();
void ezoPassthrough(String cmd);
String ezoSendCommand(const String& cmd, uint16_t timeout = 1000);
String ezoReadResponse(uint16_t timeout = 1000);
void ezoCalibrateDry();
void ezoCalibrateSingle();
void ezoCalibrateLow();
void ezoCalibrateHigh();
void ezoCalStatus();
void ezoInfo();
void ezoStatus();
void printRTDFaults();

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("================================================");
    Serial.println("  EZO-EC + MAX31865 PT1000 TEST PROGRAM");
    Serial.println("================================================");
    Serial.println();

    // ---- Initialize EZO-EC UART ----
    Serial.printf("Initializing EZO-EC UART (TX=GPIO%d, RX=GPIO%d, %d baud)...\n",
                  EZO_TX_PIN, EZO_RX_PIN, EZO_BAUD);
    Serial2.begin(EZO_BAUD, SERIAL_8N1, EZO_RX_PIN, EZO_TX_PIN);
    delay(2000);  // Wait for EZO boot

    // Drain any boot messages
    while (Serial2.available()) Serial2.read();

    // Disable continuous mode
    ezoSendCommand("C,0", 600);
    delay(100);
    while (Serial2.available()) Serial2.read();

    // Check EZO identity
    String info = ezoSendCommand("i", 600);
    if (info.startsWith("?I,EC")) {
        ezoReady = true;
        Serial.printf("  EZO-EC found: %s\n", info.c_str());
    } else {
        ezoReady = false;
        Serial.printf("  WARNING: EZO-EC not detected (response: '%s')\n", info.c_str());
    }

    // Enable response codes
    ezoSendCommand("*OK,1", 600);

    // ---- Initialize MAX31865 ----
    Serial.printf("Initializing MAX31865 (CS=GPIO%d, MOSI=GPIO%d, MISO=GPIO%d, SCK=GPIO%d)...\n",
                  RTD_CS_PIN, RTD_MOSI_PIN, RTD_MISO_PIN, RTD_SCK_PIN);

    if (rtd.begin(MAX31865_2WIRE)) {
        float temp = rtd.temperature(RTD_NOMINAL, RTD_REFERENCE);
        uint8_t fault = rtd.readFault();
        if (fault == 0 && temp > -40 && temp < 250) {
            rtdReady = true;
            Serial.printf("  MAX31865 PT1000 OK, current temp: %.2f C\n", temp);
        } else {
            rtdReady = false;
            Serial.printf("  WARNING: MAX31865 fault: 0x%02X, temp: %.2f C\n", fault, temp);
            rtd.clearFault();
        }
    } else {
        rtdReady = false;
        Serial.println("  WARNING: MAX31865 initialization failed");
    }

    Serial.println();
    Serial.println("Status:");
    Serial.printf("  EZO-EC:   %s\n", ezoReady ? "OK" : "NOT DETECTED");
    Serial.printf("  MAX31865: %s\n", rtdReady ? "OK" : "NOT DETECTED");
    Serial.println();

    printMenu();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    // Read serial commands
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            processCommand(input);
        }
    }

    // Continuous mode
    if (continuousMode) {
        readCombined();
        delay(2000);
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(String cmd) {
    cmd.toLowerCase();

    if (cmd == "r") {
        readCombined();
    } else if (cmd == "ec") {
        readConductivity();
    } else if (cmd == "temp") {
        readTemperature();
    } else if (cmd == "c") {
        continuousMode = !continuousMode;
        Serial.printf("Continuous mode: %s\n", continuousMode ? "ON (2s interval)" : "OFF");
    } else if (cmd == "ezo") {
        testEZO();
    } else if (cmd == "rtd") {
        testRTD();
    } else if (cmd == "info") {
        ezoInfo();
    } else if (cmd == "status") {
        ezoStatus();
    } else if (cmd == "caldry") {
        ezoCalibrateDry();
    } else if (cmd == "calsingle") {
        ezoCalibrateSingle();
    } else if (cmd == "callow") {
        ezoCalibrateLow();
    } else if (cmd == "calhigh") {
        ezoCalibrateHigh();
    } else if (cmd == "calstatus") {
        ezoCalStatus();
    } else if (cmd == "find") {
        ezoSendCommand("Find", 600);
        Serial.println("EZO LED blinking white...");
    } else if (cmd == "sleep") {
        ezoSendCommand("Sleep", 600);
        Serial.println("EZO entering sleep mode");
    } else if (cmd == "wake") {
        Serial2.print('\r');
        delay(200);
        while (Serial2.available()) Serial2.read();
        String resp = ezoSendCommand("i", 600);
        Serial.printf("EZO woke up: %s\n", resp.c_str());
    } else if (cmd == "factory") {
        Serial.println("WARNING: This will erase all calibration data!");
        Serial.println("Type 'YES' to confirm factory reset:");
        while (!Serial.available()) delay(10);
        String confirm = Serial.readStringUntil('\n');
        confirm.trim();
        if (confirm == "YES") {
            ezoSendCommand("Factory", 600);
            delay(2000);
            Serial.println("Factory reset complete");
        } else {
            Serial.println("Factory reset cancelled");
        }
    } else if (cmd.startsWith("ezo:")) {
        // Direct EZO command passthrough
        String ezoCmd = cmd.substring(4);
        ezoCmd.trim();
        ezoPassthrough(ezoCmd);
    } else if (cmd == "h" || cmd == "help" || cmd == "?") {
        printMenu();
    } else {
        Serial.printf("Unknown command: '%s' (type 'h' for help)\n", cmd.c_str());
    }
}

// ============================================================================
// READING FUNCTIONS
// ============================================================================

void readCombined() {
    Serial.println();
    Serial.println("--- Combined Reading ---");

    // Read temperature first
    float temp = -999;
    if (rtdReady) {
        temp = rtd.temperature(RTD_NOMINAL, RTD_REFERENCE);
        uint8_t fault = rtd.readFault();
        if (fault) {
            Serial.printf("  RTD FAULT: 0x%02X\n", fault);
            rtd.clearFault();
            temp = 25.0;  // Fall back to default
        }
    } else {
        temp = 25.0;
    }

    lastTemp = temp;
    Serial.printf("  Temperature:    %.2f C (%.2f F)\n", temp, temp * 9.0 / 5.0 + 32.0);

    // Read conductivity with temperature compensation
    if (ezoReady) {
        String cmd = "RT," + String(temp, 1);
        String resp = ezoSendCommand(cmd, 1500);

        if (resp.length() > 0 && isdigit(resp.charAt(0))) {
            // Parse EC,TDS,SAL,SG
            char buf[64];
            resp.toCharArray(buf, sizeof(buf));
            char* tok = strtok(buf, ",");
            int idx = 0;
            float vals[4] = {0, 0, 0, 0};
            while (tok && idx < 4) {
                vals[idx++] = atof(tok);
                tok = strtok(NULL, ",");
            }

            lastEC = vals[0];
            lastTDS = vals[1];

            Serial.printf("  Conductivity:   %.1f uS/cm\n", vals[0]);
            Serial.printf("  TDS:            %.0f ppm\n", vals[1]);
            if (idx >= 3) Serial.printf("  Salinity:       %.2f PSU\n", vals[2]);
            if (idx >= 4) Serial.printf("  Specific Grav:  %.3f\n", vals[3]);
        } else {
            Serial.printf("  EZO read error: '%s'\n", resp.c_str());
        }
    } else {
        Serial.println("  EZO-EC not available");
    }

    Serial.println();
}

void readConductivity() {
    if (!ezoReady) {
        Serial.println("EZO-EC not available");
        return;
    }

    String resp = ezoSendCommand("R", 1000);
    Serial.printf("EZO reading: %s\n", resp.c_str());
}

void readTemperature() {
    if (!rtdReady) {
        Serial.println("MAX31865 not available");
        return;
    }

    Serial.println();
    Serial.println("--- Temperature Readings (10 samples) ---");
    for (int i = 0; i < 10; i++) {
        uint16_t rawRTD = rtd.readRTD();
        float ratio = (float)rawRTD / 32768.0;
        float resistance = ratio * RTD_REFERENCE;
        float temp = rtd.temperature(RTD_NOMINAL, RTD_REFERENCE);
        uint8_t fault = rtd.readFault();

        Serial.printf("  %2d: RTD=0x%04X  ratio=%.5f  R=%.2f ohm  T=%.2f C",
                      i + 1, rawRTD, ratio, resistance, temp);
        if (fault) {
            Serial.printf("  FAULT=0x%02X", fault);
            rtd.clearFault();
        }
        Serial.println();
        delay(500);
    }
    Serial.println();
}

// ============================================================================
// EZO COMMUNICATION
// ============================================================================

String ezoSendCommand(const String& cmd, uint16_t timeout) {
    // Drain buffer
    while (Serial2.available()) Serial2.read();

    // Send command
    Serial2.print(cmd);
    Serial2.print('\r');

    // Read data response
    String response = ezoReadResponse(timeout);

    // If this was data (not a *-prefixed response), read the *OK code
    if (response.length() > 0 && !response.startsWith("*")) {
        String code = ezoReadResponse(timeout);
        // Silently consume the *OK/*ER code
    }

    return response;
}

String ezoReadResponse(uint16_t timeout) {
    String response = "";
    uint32_t start = millis();

    while ((millis() - start) < timeout) {
        if (Serial2.available()) {
            char c = Serial2.read();
            if (c == '\r') {
                response.trim();
                return response;
            }
            if (c != '\n') {
                response += c;
            }
        }
        yield();
    }

    response.trim();
    return response;
}

void ezoPassthrough(String cmd) {
    Serial.printf(">>> %s\n", cmd.c_str());

    // Send raw command
    while (Serial2.available()) Serial2.read();
    Serial2.print(cmd);
    Serial2.print('\r');

    // Print all responses for 3 seconds
    uint32_t start = millis();
    String line = "";
    while ((millis() - start) < 3000) {
        if (Serial2.available()) {
            char c = Serial2.read();
            if (c == '\r') {
                line.trim();
                if (line.length() > 0) {
                    Serial.printf("<<< %s\n", line.c_str());
                }
                line = "";
            } else if (c != '\n') {
                line += c;
            }
        }
        yield();
    }
    if (line.length() > 0) {
        line.trim();
        Serial.printf("<<< %s\n", line.c_str());
    }
}

// ============================================================================
// DIAGNOSTIC FUNCTIONS
// ============================================================================

void testEZO() {
    Serial.println();
    Serial.println("=== EZO-EC DIAGNOSTIC ===");

    // Device info
    String info = ezoSendCommand("i", 600);
    Serial.printf("  Device info: %s\n", info.c_str());

    // Status
    String status = ezoSendCommand("Status", 600);
    Serial.printf("  Status:      %s\n", status.c_str());

    // K value
    String k = ezoSendCommand("K,?", 600);
    Serial.printf("  K value:     %s\n", k.c_str());

    // TDS factor
    String tds = ezoSendCommand("TDS,?", 600);
    Serial.printf("  TDS factor:  %s\n", tds.c_str());

    // Temperature compensation
    String t = ezoSendCommand("T,?", 600);
    Serial.printf("  Temp comp:   %s\n", t.c_str());

    // Output config
    String o = ezoSendCommand("O,?", 600);
    Serial.printf("  Outputs:     %s\n", o.c_str());

    // Calibration
    String cal = ezoSendCommand("Cal,?", 600);
    Serial.printf("  Calibration: %s\n", cal.c_str());

    // LED state
    String led = ezoSendCommand("L,?", 600);
    Serial.printf("  LED:         %s\n", led.c_str());

    // Protocol lock
    String plock = ezoSendCommand("Plock,?", 600);
    Serial.printf("  Plock:       %s\n", plock.c_str());

    Serial.println();
}

void testRTD() {
    Serial.println();
    Serial.println("=== MAX31865 PT1000 DIAGNOSTIC ===");

    uint16_t rawRTD = rtd.readRTD();
    float ratio = (float)rawRTD / 32768.0;
    float resistance = ratio * RTD_REFERENCE;
    float temp = rtd.temperature(RTD_NOMINAL, RTD_REFERENCE);

    Serial.printf("  Raw RTD register: 0x%04X\n", rawRTD);
    Serial.printf("  Ratio:            %.5f\n", ratio);
    Serial.printf("  Resistance:       %.2f ohm\n", resistance);
    Serial.printf("  Temperature:      %.2f C\n", temp);
    Serial.printf("  RTD Nominal:      %.0f ohm (PT%d)\n", RTD_NOMINAL, (int)RTD_NOMINAL);
    Serial.printf("  Ref Resistor:     %.0f ohm\n", RTD_REFERENCE);

    printRTDFaults();
    Serial.println();
}

void printRTDFaults() {
    uint8_t fault = rtd.readFault();
    if (fault) {
        Serial.printf("  FAULT: 0x%02X\n", fault);
        if (fault & MAX31865_FAULT_HIGHTHRESH) Serial.println("    - RTD High Threshold");
        if (fault & MAX31865_FAULT_LOWTHRESH)  Serial.println("    - RTD Low Threshold");
        if (fault & MAX31865_FAULT_REFINLOW)   Serial.println("    - REFIN- > 0.85 x Bias");
        if (fault & MAX31865_FAULT_REFINHIGH)  Serial.println("    - REFIN- < 0.85 x Bias");
        if (fault & MAX31865_FAULT_RTDINLOW)   Serial.println("    - RTDIN- < 0.85 x Bias");
        if (fault & MAX31865_FAULT_OVUV)       Serial.println("    - Under/Over voltage");
        rtd.clearFault();
    } else {
        Serial.println("  No faults detected");
    }
}

// ============================================================================
// CALIBRATION
// ============================================================================

void ezoCalibrateDry() {
    Serial.println();
    Serial.println("=== DRY CALIBRATION ===");
    Serial.println("Ensure the probe is completely dry.");
    Serial.println("Press Enter to proceed...");
    while (!Serial.available()) delay(10);
    while (Serial.available()) Serial.read();

    String resp = ezoSendCommand("Cal,dry", 1600);
    if (resp.startsWith("*OK")) {
        Serial.println("Dry calibration complete!");
    } else {
        Serial.printf("Dry calibration result: %s\n", resp.c_str());
    }
    Serial.println();
}

void ezoCalibrateSingle() {
    Serial.println();
    Serial.println("=== SINGLE POINT CALIBRATION ===");
    Serial.println("Enter calibration standard value in uS/cm:");

    while (!Serial.available()) delay(10);
    String valStr = Serial.readStringUntil('\n');
    valStr.trim();
    float calValue = valStr.toFloat();

    if (calValue <= 0) {
        Serial.println("Invalid value. Cancelled.");
        return;
    }

    Serial.printf("Calibration target: %.0f uS/cm\n", calValue);
    Serial.println("Submerge probe in calibration solution.");
    Serial.println("Wait for readings to stabilize, then press Enter.");
    Serial.println("Live readings:");

    // Show live readings
    while (!Serial.available()) {
        String resp = ezoSendCommand("R", 1000);
        Serial.printf("\r  Current: %s uS/cm    ", resp.c_str());
        delay(1000);
    }
    while (Serial.available()) Serial.read();
    Serial.println();

    String cmd = "Cal," + String(calValue, 0);
    String resp = ezoSendCommand(cmd, 1600);
    if (resp.startsWith("*OK")) {
        Serial.printf("Single-point calibration complete (%.0f uS/cm)!\n", calValue);
    } else {
        Serial.printf("Calibration result: %s\n", resp.c_str());
    }
    Serial.println();
}

void ezoCalibrateLow() {
    Serial.println();
    Serial.println("=== LOW POINT CALIBRATION ===");
    Serial.println("(Do dry calibration first if not already done)");
    Serial.println("Enter LOW calibration standard value in uS/cm:");

    while (!Serial.available()) delay(10);
    String valStr = Serial.readStringUntil('\n');
    valStr.trim();
    float calValue = valStr.toFloat();

    if (calValue <= 0) {
        Serial.println("Invalid value. Cancelled.");
        return;
    }

    Serial.printf("Low-point target: %.0f uS/cm\n", calValue);
    Serial.println("Submerge probe and wait for stabilization, then press Enter.");

    while (!Serial.available()) {
        String resp = ezoSendCommand("R", 1000);
        Serial.printf("\r  Current: %s uS/cm    ", resp.c_str());
        delay(1000);
    }
    while (Serial.available()) Serial.read();
    Serial.println();

    String cmd = "Cal,low," + String(calValue, 0);
    String resp = ezoSendCommand(cmd, 1600);
    if (resp.startsWith("*OK")) {
        Serial.printf("Low-point calibration complete (%.0f uS/cm)!\n", calValue);
        Serial.println("Note: Readings will NOT change yet. Proceed with high-point calibration.");
    } else {
        Serial.printf("Calibration result: %s\n", resp.c_str());
    }
    Serial.println();
}

void ezoCalibrateHigh() {
    Serial.println();
    Serial.println("=== HIGH POINT CALIBRATION ===");
    Serial.println("Enter HIGH calibration standard value in uS/cm:");

    while (!Serial.available()) delay(10);
    String valStr = Serial.readStringUntil('\n');
    valStr.trim();
    float calValue = valStr.toFloat();

    if (calValue <= 0) {
        Serial.println("Invalid value. Cancelled.");
        return;
    }

    Serial.printf("High-point target: %.0f uS/cm\n", calValue);
    Serial.println("Submerge probe and wait for stabilization, then press Enter.");

    while (!Serial.available()) {
        String resp = ezoSendCommand("R", 1000);
        Serial.printf("\r  Current: %s uS/cm    ", resp.c_str());
        delay(1000);
    }
    while (Serial.available()) Serial.read();
    Serial.println();

    String cmd = "Cal,high," + String(calValue, 0);
    String resp = ezoSendCommand(cmd, 1600);
    if (resp.startsWith("*OK")) {
        Serial.printf("High-point calibration complete (%.0f uS/cm)!\n", calValue);
        Serial.println("Two-point calibration is now active.");
    } else {
        Serial.printf("Calibration result: %s\n", resp.c_str());
    }
    Serial.println();
}

void ezoCalStatus() {
    String resp = ezoSendCommand("Cal,?", 600);
    Serial.printf("Calibration status: %s\n", resp.c_str());
    // ?Cal,0 = not calibrated
    // ?Cal,1 = single point
    // ?Cal,2 = two point
}

void ezoInfo() {
    String resp = ezoSendCommand("i", 600);
    Serial.printf("Device info: %s\n", resp.c_str());
}

void ezoStatus() {
    String resp = ezoSendCommand("Status", 600);
    Serial.printf("Device status: %s\n", resp.c_str());
    // ?Status,reason,voltage
    // reason: P=powered on, S=software reset, B=brown out, W=watchdog, U=unknown
}

// ============================================================================
// MENU
// ============================================================================

void printMenu() {
    Serial.println("=== EZO-EC + MAX31865 TEST MENU ===");
    Serial.println();
    Serial.println("Readings:");
    Serial.println("  r          - Combined reading (temp + EC)");
    Serial.println("  ec         - EC reading only (no temp comp)");
    Serial.println("  temp       - Temperature readings (10 samples)");
    Serial.println("  c          - Toggle continuous mode");
    Serial.println();
    Serial.println("Diagnostics:");
    Serial.println("  ezo        - EZO-EC full diagnostic");
    Serial.println("  rtd        - MAX31865 PT1000 diagnostic");
    Serial.println("  info       - EZO device info");
    Serial.println("  status     - EZO status (voltage, reset reason)");
    Serial.println("  find       - Blink EZO LED to locate");
    Serial.println();
    Serial.println("Calibration:");
    Serial.println("  caldry     - Dry calibration (do first!)");
    Serial.println("  calsingle  - Single-point calibration");
    Serial.println("  callow     - Two-point LOW calibration");
    Serial.println("  calhigh    - Two-point HIGH calibration");
    Serial.println("  calstatus  - Query calibration status");
    Serial.println();
    Serial.println("EZO Control:");
    Serial.println("  sleep      - Put EZO in low-power mode");
    Serial.println("  wake       - Wake EZO from sleep");
    Serial.println("  factory    - Factory reset (erases calibration!)");
    Serial.println("  ezo:CMD    - Send raw command (e.g., ezo:K,?)");
    Serial.println();
    Serial.println("  h / help   - Show this menu");
    Serial.println();
}

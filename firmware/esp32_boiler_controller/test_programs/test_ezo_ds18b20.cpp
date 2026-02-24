/**
 * @file test_ezo_ds18b20.cpp
 * @brief Test program for Atlas Scientific EZO-EC + DS18B20 Temperature Sensor
 *
 * Drop-in replacement for test_ezo_conductivity.cpp when the MAX31865 board
 * is unavailable. Uses a DS18B20 OneWire digital temperature sensor instead
 * of the PT1000 RTD for temperature-compensated conductivity readings.
 *
 * Hardware:
 *   - Atlas Scientific EZO-EC on Serial2 (TX=GPIO25, RX=GPIO36)
 *   - DS18B20 OneWire temperature sensor on GPIO16
 *     (reuses the MAX31865 CS pin since that board is absent)
 *
 * Wiring (DS18B20):
 *   - VCC  → 3.3V
 *   - GND  → GND
 *   - DATA → GPIO16 with 4.7kΩ pull-up to 3.3V
 *
 *   If using parasitic power mode (2-wire):
 *   - VCC  → GND (tie together)
 *   - DATA → GPIO16 with 4.7kΩ pull-up to 3.3V
 *
 * Usage:
 *   pio run -e test_ezo_ds18b20 -t upload -t monitor
 *   Serial Monitor at 115200 baud — type 'h' for menu
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// Atlas Scientific EZO-EC UART (same as main firmware)
#define EZO_TX_PIN          25      // ESP32 TX → EZO RX
#define EZO_RX_PIN          36      // ESP32 RX ← EZO TX
#define EZO_BAUD            9600

// DS18B20 OneWire — reuses MAX31865 CS pin (GPIO16) since that board is absent
#define DS18B20_PIN         16
#define DS18B20_RESOLUTION  12      // 12-bit = 0.0625°C (750ms conversion)

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

bool continuousMode = false;
float lastEC = 0;
float lastTDS = 0;
float lastTemp = 0;
bool ezoReady = false;
bool ds18b20Ready = false;
uint8_t ds18b20Address[8];         // ROM address of discovered sensor

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void printMenu();
void processCommand(String cmd);
void testEZO();
void testDS18B20();
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
void scanOneWire();

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("================================================");
    Serial.println("  EZO-EC + DS18B20 TEMPERATURE TEST PROGRAM");
    Serial.println("  (MAX31865 substitute — uses OneWire DS18B20)");
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

    // ---- Initialize DS18B20 ----
    Serial.printf("Initializing DS18B20 OneWire (GPIO%d)...\n", DS18B20_PIN);
    ds18b20.begin();

    int deviceCount = ds18b20.getDeviceCount();
    Serial.printf("  OneWire devices found: %d\n", deviceCount);

    if (deviceCount > 0) {
        if (ds18b20.getAddress(ds18b20Address, 0)) {
            ds18b20.setResolution(ds18b20Address, DS18B20_RESOLUTION);
            ds18b20.setWaitForConversion(true);

            ds18b20.requestTemperatures();
            float temp = ds18b20.getTempC(ds18b20Address);

            if (temp != DEVICE_DISCONNECTED_C && temp > -55.0 && temp < 125.0) {
                ds18b20Ready = true;
                Serial.printf("  DS18B20 OK — ROM: ");
                for (int i = 0; i < 8; i++) {
                    Serial.printf("%02X", ds18b20Address[i]);
                    if (i < 7) Serial.print(":");
                }
                Serial.printf("  temp: %.2f C\n", temp);
                Serial.printf("  Resolution: %d-bit (%.4f C)\n",
                              DS18B20_RESOLUTION,
                              DS18B20_RESOLUTION == 12 ? 0.0625 :
                              DS18B20_RESOLUTION == 11 ? 0.125 :
                              DS18B20_RESOLUTION == 10 ? 0.25 : 0.5);
            } else {
                ds18b20Ready = false;
                Serial.printf("  WARNING: DS18B20 returned invalid temp: %.2f C\n", temp);
            }
        } else {
            ds18b20Ready = false;
            Serial.println("  WARNING: Could not read DS18B20 ROM address");
        }
    } else {
        ds18b20Ready = false;
        Serial.println("  WARNING: No DS18B20 found on OneWire bus");
        Serial.println("  Check wiring: DATA=GPIO16, 4.7k pull-up to 3.3V");
    }

    Serial.println();
    Serial.println("Status:");
    Serial.printf("  EZO-EC:   %s\n", ezoReady ? "OK" : "NOT DETECTED");
    Serial.printf("  DS18B20:  %s\n", ds18b20Ready ? "OK" : "NOT DETECTED");
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
    } else if (cmd == "ds") {
        testDS18B20();
    } else if (cmd == "scan") {
        scanOneWire();
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

    // Read temperature first (DS18B20 takes ~750ms at 12-bit)
    float temp = -999;
    if (ds18b20Ready) {
        ds18b20.requestTemperatures();
        temp = ds18b20.getTempC(ds18b20Address);
        if (temp == DEVICE_DISCONNECTED_C) {
            Serial.println("  DS18B20 DISCONNECTED — using default 25.0 C");
            temp = 25.0;
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
    if (!ds18b20Ready) {
        Serial.println("DS18B20 not available");
        Serial.println("Check wiring: DATA=GPIO16, 4.7k pull-up to 3.3V");
        return;
    }

    Serial.println();
    Serial.println("--- Temperature Readings (10 samples) ---");
    for (int i = 0; i < 10; i++) {
        ds18b20.requestTemperatures();
        float temp = ds18b20.getTempC(ds18b20Address);
        int16_t raw = ds18b20.getTemp(ds18b20Address);

        Serial.printf("  %2d: T=%.4f C  (%.4f F)  raw=%d",
                      i + 1, temp,
                      temp * 9.0 / 5.0 + 32.0,
                      raw);

        if (temp == DEVICE_DISCONNECTED_C) {
            Serial.print("  DISCONNECTED");
        }
        Serial.println();
        delay(100);  // DS18B20 with waitForConversion=true handles its own delay
    }

    // Statistics
    Serial.println();
    Serial.println("  Stability test (20 samples, computing min/max/avg)...");
    float minT = 999, maxT = -999, sumT = 0;
    int validCount = 0;
    for (int i = 0; i < 20; i++) {
        ds18b20.requestTemperatures();
        float t = ds18b20.getTempC(ds18b20Address);
        if (t != DEVICE_DISCONNECTED_C) {
            if (t < minT) minT = t;
            if (t > maxT) maxT = t;
            sumT += t;
            validCount++;
        }
    }
    if (validCount > 0) {
        Serial.printf("  Min: %.4f C  Max: %.4f C  Avg: %.4f C  Spread: %.4f C\n",
                      minT, maxT, sumT / validCount, maxT - minT);
    } else {
        Serial.println("  No valid readings obtained");
    }
    Serial.println();
}

// ============================================================================
// DS18B20 DIAGNOSTIC
// ============================================================================

void testDS18B20() {
    Serial.println();
    Serial.println("=== DS18B20 DIAGNOSTIC ===");

    Serial.printf("  Data pin:     GPIO%d\n", DS18B20_PIN);
    Serial.printf("  Resolution:   %d-bit\n", DS18B20_RESOLUTION);

    // Re-scan bus
    ds18b20.begin();
    int count = ds18b20.getDeviceCount();
    Serial.printf("  Devices found: %d\n", count);

    if (count == 0) {
        Serial.println("  No devices on OneWire bus");
        Serial.println("  Troubleshooting:");
        Serial.println("    1. Check DATA wire to GPIO16");
        Serial.println("    2. Verify 4.7k pull-up resistor to 3.3V");
        Serial.println("    3. Check VCC (3.3V) and GND connections");
        Serial.println("    4. Try parasitic mode: tie VCC to GND");
        Serial.println();
        return;
    }

    // Show all devices on bus
    for (int i = 0; i < count; i++) {
        DeviceAddress addr;
        if (ds18b20.getAddress(addr, i)) {
            Serial.printf("  Device %d ROM: ", i);
            for (int j = 0; j < 8; j++) {
                Serial.printf("%02X", addr[j]);
                if (j < 7) Serial.print(":");
            }

            // Identify chip type from family code (first byte)
            switch (addr[0]) {
                case 0x28: Serial.print("  (DS18B20)"); break;
                case 0x10: Serial.print("  (DS18S20)"); break;
                case 0x22: Serial.print("  (DS1822)"); break;
                default:   Serial.printf("  (Unknown: 0x%02X)", addr[0]); break;
            }

            ds18b20.requestTemperaturesByAddress(addr);
            float t = ds18b20.getTempC(addr);
            Serial.printf("  T=%.2f C", t);
            Serial.println();

            // Check parasitic power
            bool parasitic = !ds18b20.isParasitePowerMode();
            Serial.printf("  Power mode: %s\n", parasitic ? "Parasitic" : "External VCC");
        }
    }

    // Show resolution details
    Serial.println();
    Serial.println("  Resolution vs conversion time:");
    Serial.println("    9-bit:  0.5 C     ~94 ms");
    Serial.println("   10-bit:  0.25 C   ~188 ms");
    Serial.println("   11-bit:  0.125 C  ~375 ms");
    Serial.println("   12-bit:  0.0625 C ~750 ms  (current)");

    Serial.println();
}

void scanOneWire() {
    Serial.println();
    Serial.println("=== OneWire Bus Scan ===");

    oneWire.reset_search();
    uint8_t addr[8];
    int found = 0;

    while (oneWire.search(addr)) {
        found++;
        Serial.printf("  Device %d: ", found);
        for (int i = 0; i < 8; i++) {
            Serial.printf("%02X", addr[i]);
            if (i < 7) Serial.print(":");
        }

        // CRC check
        if (OneWire::crc8(addr, 7) != addr[7]) {
            Serial.print("  CRC ERROR!");
        } else {
            switch (addr[0]) {
                case 0x10: Serial.print("  DS18S20"); break;
                case 0x22: Serial.print("  DS1822"); break;
                case 0x28: Serial.print("  DS18B20"); break;
                case 0x3B: Serial.print("  DS1825/MAX31850"); break;
                default:   Serial.printf("  Family: 0x%02X", addr[0]); break;
            }
        }
        Serial.println();
    }

    if (found == 0) {
        Serial.println("  No devices found on OneWire bus");
    } else {
        Serial.printf("  Total: %d device(s)\n", found);
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
// EZO DIAGNOSTIC
// ============================================================================

void testEZO() {
    Serial.println();
    Serial.println("=== EZO-EC DIAGNOSTIC ===");

    String info = ezoSendCommand("i", 600);
    Serial.printf("  Device info: %s\n", info.c_str());

    String status = ezoSendCommand("Status", 600);
    Serial.printf("  Status:      %s\n", status.c_str());

    String k = ezoSendCommand("K,?", 600);
    Serial.printf("  K value:     %s\n", k.c_str());

    String tds = ezoSendCommand("TDS,?", 600);
    Serial.printf("  TDS factor:  %s\n", tds.c_str());

    String t = ezoSendCommand("T,?", 600);
    Serial.printf("  Temp comp:   %s\n", t.c_str());

    String o = ezoSendCommand("O,?", 600);
    Serial.printf("  Outputs:     %s\n", o.c_str());

    String cal = ezoSendCommand("Cal,?", 600);
    Serial.printf("  Calibration: %s\n", cal.c_str());

    String led = ezoSendCommand("L,?", 600);
    Serial.printf("  LED:         %s\n", led.c_str());

    String plock = ezoSendCommand("Plock,?", 600);
    Serial.printf("  Plock:       %s\n", plock.c_str());

    Serial.println();
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
    Serial.println("Live readings (with DS18B20 temp compensation):");

    // Show live readings with DS18B20 temperature
    while (!Serial.available()) {
        float temp = 25.0;
        if (ds18b20Ready) {
            ds18b20.requestTemperatures();
            temp = ds18b20.getTempC(ds18b20Address);
            if (temp == DEVICE_DISCONNECTED_C) temp = 25.0;
        }
        String cmd = "RT," + String(temp, 1);
        String resp = ezoSendCommand(cmd, 1500);
        Serial.printf("\r  EC: %s uS/cm  T: %.1f C    ", resp.c_str(), temp);
        delay(1500);
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
        float temp = 25.0;
        if (ds18b20Ready) {
            ds18b20.requestTemperatures();
            temp = ds18b20.getTempC(ds18b20Address);
            if (temp == DEVICE_DISCONNECTED_C) temp = 25.0;
        }
        String cmd = "RT," + String(temp, 1);
        String resp = ezoSendCommand(cmd, 1500);
        Serial.printf("\r  EC: %s uS/cm  T: %.1f C    ", resp.c_str(), temp);
        delay(1500);
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
        float temp = 25.0;
        if (ds18b20Ready) {
            ds18b20.requestTemperatures();
            temp = ds18b20.getTempC(ds18b20Address);
            if (temp == DEVICE_DISCONNECTED_C) temp = 25.0;
        }
        String cmd = "RT," + String(temp, 1);
        String resp = ezoSendCommand(cmd, 1500);
        Serial.printf("\r  EC: %s uS/cm  T: %.1f C    ", resp.c_str(), temp);
        delay(1500);
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
}

void ezoInfo() {
    String resp = ezoSendCommand("i", 600);
    Serial.printf("Device info: %s\n", resp.c_str());
}

void ezoStatus() {
    String resp = ezoSendCommand("Status", 600);
    Serial.printf("Device status: %s\n", resp.c_str());
}

// ============================================================================
// MENU
// ============================================================================

void printMenu() {
    Serial.println("=== EZO-EC + DS18B20 TEST MENU ===");
    Serial.println();
    Serial.println("Readings:");
    Serial.println("  r          - Combined reading (DS18B20 temp + EC)");
    Serial.println("  ec         - EC reading only (no temp comp)");
    Serial.println("  temp       - Temperature readings (10 samples + stability)");
    Serial.println("  c          - Toggle continuous mode (2s interval)");
    Serial.println();
    Serial.println("Diagnostics:");
    Serial.println("  ezo        - EZO-EC full diagnostic");
    Serial.println("  ds         - DS18B20 full diagnostic");
    Serial.println("  scan       - OneWire bus scan (find all devices)");
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

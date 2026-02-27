/**
 * @file test_dual_temp_conductivity.cpp
 * @brief Test program for MAX31865 PT1000 RTD + DS18B20 + Atlas Scientific EZO-EC
 *
 * Combines both temperature sensors with the EZO-EC conductivity circuit
 * so you can compare RTD vs DS18B20 readings side by side and select which
 * sensor provides temperature compensation to the EZO.
 *
 * Hardware:
 * - Atlas Scientific EZO-EC on Serial2 (TX=GPIO25, RX=GPIO36)
 * - Adafruit MAX31865 PT1000 on software SPI — adjacent pins on 30-pin DevKitC:
 *     CS=GPIO19, SCK=GPIO18, MOSI=GPIO17, MISO=GPIO16
 * - DS18B20 OneWire sensor on GPIO4
 *
 * 30-pin ESP32 DevKitC right-side layout (relevant pins):
 *   ... D19 [CS] — D18 [SCK] — D5 — D17 [MOSI] — D16 [MISO] — D4 [DS18B20] ...
 *
 * Wiring (DS18B20):
 *   DATA → GPIO4 with 4.7k ohm pull-up to 3.3V
 *   VCC  → 3.3V
 *   GND  → GND
 *
 * Usage:
 *   pio run -e test_dual_temp_conductivity -t upload -t monitor
 *   Serial Monitor at 115200 baud — type 'h' for menu
 */

#include <Arduino.h>
#include <Adafruit_MAX31865.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// Atlas Scientific EZO-EC UART
#define EZO_TX_PIN          25      // ESP32 TX -> EZO RX
#define EZO_RX_PIN          36      // ESP32 RX <- EZO TX
#define EZO_BAUD            9600

// Adafruit MAX31865 Software SPI (adjacent pins on 30-pin ESP32 DevKitC right header)
#define RTD_CS_PIN          19
#define RTD_SCK_PIN         18
#define RTD_MOSI_PIN        17
#define RTD_MISO_PIN        16

// PT1000 configuration
#define RTD_NOMINAL         1000.0  // Nominal resistance at 0C
#define RTD_REFERENCE       4300.0  // Reference resistor on MAX31865 board

// DS18B20 OneWire (GPIO4, adjacent to MAX31865 cluster on right header)
#define DS18B20_PIN         4

// ============================================================================
// TEMPERATURE SOURCE SELECTION
// ============================================================================

typedef enum {
    TEMP_SRC_RTD = 0,       // Use MAX31865 PT1000 for EZO temp compensation
    TEMP_SRC_DS18B20 = 1,   // Use DS18B20 for EZO temp compensation
    TEMP_SRC_AVERAGE = 2,   // Average both sensors
    TEMP_SRC_MANUAL = 3     // Manual fixed temperature
} temp_source_t;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

Adafruit_MAX31865 rtd(RTD_CS_PIN, RTD_MOSI_PIN, RTD_MISO_PIN, RTD_SCK_PIN);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

bool continuousMode = false;
float lastEC = 0;
float lastTDS = 0;
float lastRTDTemp = 0;
float lastDS18B20Temp = 0;
float manualTemp = 25.0;
bool ezoReady = false;
bool rtdReady = false;
bool ds18b20Ready = false;
uint8_t ds18b20Address[8];

temp_source_t tempSource = TEMP_SRC_RTD;   // Default to RTD

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void printMenu();
void processCommand(String cmd);
void readCombined();
void readConductivity();
void readBothTemps();
void readRTDSamples();
void readDS18B20Samples();
void compareTemps();
void startContinuous();
void selectTempSource();
void setManualTemp();
void testEZO();
void testRTD();
void testDS18B20();
void scanOneWire();
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
float getCompensationTemp();
const char* tempSourceName();

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================================");
    Serial.println("  DUAL TEMP (PT1000 + DS18B20) + EZO-EC TEST PROGRAM");
    Serial.println("========================================================");
    Serial.println();

    // ---- Initialize EZO-EC UART ----
    Serial.printf("Initializing EZO-EC UART (TX=GPIO%d, RX=GPIO%d, %d baud)...\n",
                  EZO_TX_PIN, EZO_RX_PIN, EZO_BAUD);
    Serial2.begin(EZO_BAUD, SERIAL_8N1, EZO_RX_PIN, EZO_TX_PIN);
    delay(2000);

    while (Serial2.available()) Serial2.read();
    ezoSendCommand("C,0", 600);
    delay(100);
    while (Serial2.available()) Serial2.read();

    String info = ezoSendCommand("i", 600);
    if (info.startsWith("?I,EC")) {
        ezoReady = true;
        Serial.printf("  EZO-EC found: %s\n", info.c_str());
    } else {
        ezoReady = false;
        Serial.printf("  WARNING: EZO-EC not detected (response: '%s')\n", info.c_str());
    }
    ezoSendCommand("*OK,1", 600);

    // ---- Initialize MAX31865 PT1000 ----
    Serial.printf("Initializing MAX31865 (CS=GPIO%d, MOSI=GPIO%d, MISO=GPIO%d, SCK=GPIO%d)...\n",
                  RTD_CS_PIN, RTD_MOSI_PIN, RTD_MISO_PIN, RTD_SCK_PIN);

    if (rtd.begin(MAX31865_2WIRE)) {
        float temp = rtd.temperature(RTD_NOMINAL, RTD_REFERENCE);
        uint8_t fault = rtd.readFault();
        if (fault == 0 && temp > -40 && temp < 250) {
            rtdReady = true;
            Serial.printf("  MAX31865 PT1000 OK, temp: %.2f C\n", temp);
        } else {
            rtdReady = false;
            Serial.printf("  WARNING: MAX31865 fault: 0x%02X, temp: %.2f C\n", fault, temp);
            rtd.clearFault();
        }
    } else {
        rtdReady = false;
        Serial.println("  WARNING: MAX31865 initialization failed");
    }

    // ---- Initialize DS18B20 ----
    Serial.printf("Initializing DS18B20 OneWire (GPIO%d)...\n", DS18B20_PIN);
    ds18b20.begin();

    int deviceCount = ds18b20.getDeviceCount();
    Serial.printf("  OneWire devices found: %d\n", deviceCount);

    if (deviceCount > 0) {
        if (ds18b20.getAddress(ds18b20Address, 0)) {
            ds18b20.setResolution(ds18b20Address, 12);  // 12-bit = 0.0625C resolution
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
            } else {
                ds18b20Ready = false;
                Serial.printf("  WARNING: DS18B20 invalid temp: %.2f C\n", temp);
            }
        } else {
            ds18b20Ready = false;
            Serial.println("  WARNING: Could not read DS18B20 ROM address");
        }
    } else {
        ds18b20Ready = false;
        Serial.println("  WARNING: No DS18B20 found on OneWire bus");
    }

    // Auto-select best available temp source
    if (rtdReady) {
        tempSource = TEMP_SRC_RTD;
    } else if (ds18b20Ready) {
        tempSource = TEMP_SRC_DS18B20;
    } else {
        tempSource = TEMP_SRC_MANUAL;
    }

    Serial.println();
    Serial.println("Status:");
    Serial.printf("  EZO-EC:   %s\n", ezoReady ? "OK" : "NOT DETECTED");
    Serial.printf("  PT1000:   %s\n", rtdReady ? "OK" : "NOT DETECTED");
    Serial.printf("  DS18B20:  %s\n", ds18b20Ready ? "OK" : "NOT DETECTED");
    Serial.printf("  Temp source for EZO comp: %s\n", tempSourceName());
    Serial.println();

    printMenu();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            processCommand(input);
        }
    }

    if (continuousMode) {
        readCombined();
        delay(2000);
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(String cmd) {
    // Preserve case for EZO passthrough
    String cmdLower = cmd;
    cmdLower.toLowerCase();

    if (cmdLower == "r") {
        readCombined();
    } else if (cmdLower == "ec") {
        readConductivity();
    } else if (cmdLower == "temp") {
        readBothTemps();
    } else if (cmdLower == "rtd") {
        readRTDSamples();
    } else if (cmdLower == "ds") {
        readDS18B20Samples();
    } else if (cmdLower == "cmp") {
        compareTemps();
    } else if (cmdLower == "c") {
        continuousMode = !continuousMode;
        Serial.printf("Continuous mode: %s\n", continuousMode ? "ON (2s interval)" : "OFF");
    } else if (cmdLower == "src") {
        selectTempSource();
    } else if (cmdLower == "mt") {
        setManualTemp();
    } else if (cmdLower == "ezodiag") {
        testEZO();
    } else if (cmdLower == "rtddiag") {
        testRTD();
    } else if (cmdLower == "dsdiag") {
        testDS18B20();
    } else if (cmdLower == "scan") {
        scanOneWire();
    } else if (cmdLower == "info") {
        ezoInfo();
    } else if (cmdLower == "status") {
        ezoStatus();
    } else if (cmdLower == "caldry") {
        ezoCalibrateDry();
    } else if (cmdLower == "calsingle") {
        ezoCalibrateSingle();
    } else if (cmdLower == "callow") {
        ezoCalibrateLow();
    } else if (cmdLower == "calhigh") {
        ezoCalibrateHigh();
    } else if (cmdLower == "calstatus") {
        ezoCalStatus();
    } else if (cmdLower == "find") {
        ezoSendCommand("Find", 600);
        Serial.println("EZO LED blinking white...");
    } else if (cmdLower == "sleep") {
        ezoSendCommand("Sleep", 600);
        Serial.println("EZO entering sleep mode");
    } else if (cmdLower == "wake") {
        Serial2.print('\r');
        delay(200);
        while (Serial2.available()) Serial2.read();
        String resp = ezoSendCommand("i", 600);
        Serial.printf("EZO woke up: %s\n", resp.c_str());
    } else if (cmdLower == "factory") {
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
    } else if (cmdLower.startsWith("ezo:")) {
        String ezoCmd = cmd.substring(4);  // Use original case
        ezoCmd.trim();
        ezoPassthrough(ezoCmd);
    } else if (cmdLower == "h" || cmdLower == "help" || cmdLower == "?") {
        printMenu();
    } else {
        Serial.printf("Unknown command: '%s' (type 'h' for help)\n", cmd.c_str());
    }
}

// ============================================================================
// TEMPERATURE HELPER
// ============================================================================

float readRTDTemp() {
    if (!rtdReady) return -999.0;
    float temp = rtd.temperature(RTD_NOMINAL, RTD_REFERENCE);
    uint8_t fault = rtd.readFault();
    if (fault) {
        rtd.clearFault();
        return -999.0;
    }
    lastRTDTemp = temp;
    return temp;
}

float readDS18B20Temp() {
    if (!ds18b20Ready) return -999.0;
    ds18b20.requestTemperatures();
    float temp = ds18b20.getTempC(ds18b20Address);
    if (temp == DEVICE_DISCONNECTED_C) return -999.0;
    lastDS18B20Temp = temp;
    return temp;
}

float getCompensationTemp() {
    float rtdTemp = readRTDTemp();
    float dsTemp = readDS18B20Temp();

    switch (tempSource) {
        case TEMP_SRC_RTD:
            return (rtdTemp > -900) ? rtdTemp : ((dsTemp > -900) ? dsTemp : manualTemp);
        case TEMP_SRC_DS18B20:
            return (dsTemp > -900) ? dsTemp : ((rtdTemp > -900) ? rtdTemp : manualTemp);
        case TEMP_SRC_AVERAGE:
            if (rtdTemp > -900 && dsTemp > -900) return (rtdTemp + dsTemp) / 2.0;
            if (rtdTemp > -900) return rtdTemp;
            if (dsTemp > -900) return dsTemp;
            return manualTemp;
        case TEMP_SRC_MANUAL:
            // Still read sensors for display purposes
            return manualTemp;
    }
    return manualTemp;
}

const char* tempSourceName() {
    switch (tempSource) {
        case TEMP_SRC_RTD:      return "PT1000 (MAX31865)";
        case TEMP_SRC_DS18B20:  return "DS18B20";
        case TEMP_SRC_AVERAGE:  return "Average (RTD + DS18B20)";
        case TEMP_SRC_MANUAL:   return "Manual (fixed)";
    }
    return "Unknown";
}

// ============================================================================
// READING FUNCTIONS
// ============================================================================

void readCombined() {
    Serial.println();
    Serial.println("--- Combined Reading ---");

    // Read both temperature sensors
    float rtdTemp = readRTDTemp();
    float dsTemp = readDS18B20Temp();

    // Show both readings
    if (rtdTemp > -900) {
        Serial.printf("  PT1000 (RTD):   %.2f C (%.2f F)\n", rtdTemp, rtdTemp * 9.0 / 5.0 + 32.0);
    } else {
        Serial.println("  PT1000 (RTD):   NOT AVAILABLE");
    }

    if (dsTemp > -900) {
        Serial.printf("  DS18B20:        %.2f C (%.2f F)\n", dsTemp, dsTemp * 9.0 / 5.0 + 32.0);
    } else {
        Serial.println("  DS18B20:        NOT AVAILABLE");
    }

    // Show delta if both available
    if (rtdTemp > -900 && dsTemp > -900) {
        float delta = rtdTemp - dsTemp;
        Serial.printf("  Delta (RTD-DS): %+.2f C\n", delta);
    }

    // Get compensation temperature
    float compTemp;
    switch (tempSource) {
        case TEMP_SRC_RTD:
            compTemp = (rtdTemp > -900) ? rtdTemp : ((dsTemp > -900) ? dsTemp : manualTemp);
            break;
        case TEMP_SRC_DS18B20:
            compTemp = (dsTemp > -900) ? dsTemp : ((rtdTemp > -900) ? rtdTemp : manualTemp);
            break;
        case TEMP_SRC_AVERAGE:
            if (rtdTemp > -900 && dsTemp > -900) compTemp = (rtdTemp + dsTemp) / 2.0;
            else if (rtdTemp > -900) compTemp = rtdTemp;
            else if (dsTemp > -900) compTemp = dsTemp;
            else compTemp = manualTemp;
            break;
        case TEMP_SRC_MANUAL:
        default:
            compTemp = manualTemp;
            break;
    }
    Serial.printf("  Comp temp:      %.2f C [source: %s]\n", compTemp, tempSourceName());

    // Read conductivity with temperature compensation
    if (ezoReady) {
        String cmd = "RT," + String(compTemp, 1);
        String resp = ezoSendCommand(cmd, 1500);

        if (resp.length() > 0 && isdigit(resp.charAt(0))) {
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
    Serial.printf("EZO reading (no temp comp): %s\n", resp.c_str());
}

void readBothTemps() {
    Serial.println();
    Serial.println("--- Both Temperature Sensors (10 samples) ---");
    Serial.println();
    Serial.println("  #    PT1000 (C)    DS18B20 (C)    Delta (C)");
    Serial.println("  ----+-----------+-------------+-----------");

    float rtdSum = 0, dsSum = 0, deltaSum = 0;
    int validPairs = 0;

    for (int i = 0; i < 10; i++) {
        float rtdTemp = readRTDTemp();
        float dsTemp = readDS18B20Temp();

        Serial.printf("  %2d   ", i + 1);

        if (rtdTemp > -900) {
            Serial.printf(" %7.2f      ", rtdTemp);
        } else {
            Serial.printf("    ---       ");
        }

        if (dsTemp > -900) {
            Serial.printf(" %7.2f      ", dsTemp);
        } else {
            Serial.printf("    ---       ");
        }

        if (rtdTemp > -900 && dsTemp > -900) {
            float delta = rtdTemp - dsTemp;
            Serial.printf(" %+7.2f", delta);
            rtdSum += rtdTemp;
            dsSum += dsTemp;
            deltaSum += delta;
            validPairs++;
        }

        Serial.println();
        delay(500);
    }

    if (validPairs > 0) {
        Serial.println("  ----+-----------+-------------+-----------");
        Serial.printf("  Avg  %7.2f       %7.2f       %+7.2f\n",
                      rtdSum / validPairs, dsSum / validPairs, deltaSum / validPairs);
    }
    Serial.println();
}

void readRTDSamples() {
    if (!rtdReady) {
        Serial.println("MAX31865 not available");
        return;
    }

    Serial.println();
    Serial.println("--- PT1000 RTD Readings (10 samples) ---");
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

void readDS18B20Samples() {
    if (!ds18b20Ready) {
        Serial.println("DS18B20 not available");
        return;
    }

    Serial.println();
    Serial.println("--- DS18B20 Readings (10 samples) ---");
    for (int i = 0; i < 10; i++) {
        ds18b20.requestTemperatures();
        float temp = ds18b20.getTempC(ds18b20Address);
        int16_t raw = ds18b20.getTemp(ds18b20Address);

        Serial.printf("  %2d: T=%.4f C  (%.4f F)  raw=%d",
                      i + 1, temp, temp * 9.0 / 5.0 + 32.0, raw);
        if (temp == DEVICE_DISCONNECTED_C) {
            Serial.print("  DISCONNECTED");
        }
        Serial.println();
        delay(100);
    }
    Serial.println();
}

void compareTemps() {
    if (!rtdReady && !ds18b20Ready) {
        Serial.println("No temperature sensors available");
        return;
    }

    Serial.println();
    Serial.println("=== TEMPERATURE COMPARISON (60 samples, 1/sec) ===");
    Serial.println("Press any key to stop.");
    Serial.println();
    Serial.println("  Time(s)   PT1000(C)   DS18B20(C)   Delta(C)   EC(uS/cm)");
    Serial.println("  --------+-----------+-----------+----------+----------");

    float deltaMin = 999, deltaMax = -999, deltaSum = 0;
    int count = 0;

    for (int i = 0; i < 60; i++) {
        if (Serial.available()) {
            while (Serial.available()) Serial.read();
            break;
        }

        float rtdTemp = readRTDTemp();
        float dsTemp = readDS18B20Temp();

        Serial.printf("  %4d     ", i);

        if (rtdTemp > -900) Serial.printf(" %7.2f    ", rtdTemp);
        else                Serial.printf("    ---     ");

        if (dsTemp > -900) Serial.printf(" %7.2f    ", dsTemp);
        else               Serial.printf("    ---     ");

        if (rtdTemp > -900 && dsTemp > -900) {
            float delta = rtdTemp - dsTemp;
            Serial.printf(" %+7.2f   ", delta);
            if (delta < deltaMin) deltaMin = delta;
            if (delta > deltaMax) deltaMax = delta;
            deltaSum += delta;
            count++;
        } else {
            Serial.printf("    ---    ");
        }

        // Quick EC read if available
        if (ezoReady) {
            String resp = ezoSendCommand("R", 1000);
            if (resp.length() > 0 && isdigit(resp.charAt(0))) {
                float ec = resp.toFloat();
                Serial.printf(" %8.1f", ec);
            }
        }

        Serial.println();
        delay(1000);
    }

    if (count > 0) {
        Serial.println();
        Serial.println("--- Delta Statistics (RTD - DS18B20) ---");
        Serial.printf("  Min: %+.2f C  Max: %+.2f C  Avg: %+.2f C  Samples: %d\n",
                      deltaMin, deltaMax, deltaSum / count, count);
    }
    Serial.println();
}

// ============================================================================
// TEMP SOURCE SELECTION
// ============================================================================

void selectTempSource() {
    Serial.println();
    Serial.println("=== SELECT TEMPERATURE SOURCE FOR EZO COMPENSATION ===");
    Serial.println();
    Serial.printf("  1 - PT1000 (MAX31865)     %s\n", rtdReady ? "[available]" : "[NOT detected]");
    Serial.printf("  2 - DS18B20               %s\n", ds18b20Ready ? "[available]" : "[NOT detected]");
    Serial.printf("  3 - Average (both)        %s\n",
                  (rtdReady && ds18b20Ready) ? "[available]" : "[need both sensors]");
    Serial.printf("  4 - Manual (%.1f C)       [always available]\n", manualTemp);
    Serial.println();
    Serial.printf("Current: %s\n", tempSourceName());
    Serial.println("Select (1-4):");

    while (!Serial.available()) delay(10);
    String input = Serial.readStringUntil('\n');
    input.trim();

    switch (input.charAt(0)) {
        case '1':
            if (rtdReady) {
                tempSource = TEMP_SRC_RTD;
            } else {
                Serial.println("PT1000 not available, falling back to manual.");
                tempSource = TEMP_SRC_MANUAL;
            }
            break;
        case '2':
            if (ds18b20Ready) {
                tempSource = TEMP_SRC_DS18B20;
            } else {
                Serial.println("DS18B20 not available, falling back to manual.");
                tempSource = TEMP_SRC_MANUAL;
            }
            break;
        case '3':
            tempSource = TEMP_SRC_AVERAGE;
            if (!rtdReady || !ds18b20Ready) {
                Serial.println("Note: Only one sensor available, average will use single sensor.");
            }
            break;
        case '4':
            tempSource = TEMP_SRC_MANUAL;
            break;
        default:
            Serial.println("Invalid. Keeping current setting.");
            return;
    }

    Serial.printf("Temp source set to: %s\n", tempSourceName());
    Serial.println();
}

void setManualTemp() {
    Serial.println();
    Serial.printf("Current manual temperature: %.1f C\n", manualTemp);
    Serial.println("Enter new temperature in C:");

    while (!Serial.available()) delay(10);
    String input = Serial.readStringUntil('\n');
    input.trim();

    float val = input.toFloat();
    if (val >= -10.0 && val <= 120.0) {
        manualTemp = val;
        Serial.printf("Manual temperature set to: %.1f C\n", manualTemp);
    } else {
        Serial.println("Invalid. Enter a value between -10 and 120 C.");
    }
    Serial.println();
}

// ============================================================================
// DIAGNOSTICS
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

void testRTD() {
    Serial.println();
    Serial.println("=== MAX31865 PT1000 DIAGNOSTIC ===");

    if (!rtdReady) {
        Serial.println("  MAX31865 not detected during init.");
        Serial.println("  Check SPI wiring:");
        Serial.printf("    CS=GPIO%d, MOSI=GPIO%d, MISO=GPIO%d, SCK=GPIO%d\n",
                      RTD_CS_PIN, RTD_MOSI_PIN, RTD_MISO_PIN, RTD_SCK_PIN);
        Serial.println();
        return;
    }

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

void testDS18B20() {
    Serial.println();
    Serial.println("=== DS18B20 DIAGNOSTIC ===");

    Serial.printf("  Data pin:     GPIO%d\n", DS18B20_PIN);
    Serial.println("  Resolution:   12-bit (0.0625 C)");

    ds18b20.begin();
    int count = ds18b20.getDeviceCount();
    Serial.printf("  Devices found: %d\n", count);

    if (count == 0) {
        Serial.println("  No devices on OneWire bus");
        Serial.println("  Troubleshooting:");
        Serial.printf("    1. Check DATA wire to GPIO%d\n", DS18B20_PIN);
        Serial.println("    2. Verify 4.7k pull-up resistor to 3.3V");
        Serial.println("    3. Check VCC (3.3V) and GND connections");
        Serial.println();
        return;
    }

    for (int i = 0; i < count; i++) {
        DeviceAddress addr;
        if (ds18b20.getAddress(addr, i)) {
            Serial.printf("  Device %d ROM: ", i);
            for (int j = 0; j < 8; j++) {
                Serial.printf("%02X", addr[j]);
                if (j < 7) Serial.print(":");
            }

            switch (addr[0]) {
                case 0x28: Serial.print("  (DS18B20)"); break;
                case 0x10: Serial.print("  (DS18S20)"); break;
                case 0x22: Serial.print("  (DS1822)"); break;
                default:   Serial.printf("  (Family: 0x%02X)", addr[0]); break;
            }

            ds18b20.requestTemperaturesByAddress(addr);
            float t = ds18b20.getTempC(addr);
            Serial.printf("  T=%.2f C", t);
            Serial.println();

            bool parasitic = !ds18b20.isParasitePowerMode();
            Serial.printf("  Power mode: %s\n", parasitic ? "Parasitic" : "External VCC");
        }
    }
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
    while (Serial2.available()) Serial2.read();

    Serial2.print(cmd);
    Serial2.print('\r');

    String response = ezoReadResponse(timeout);

    if (response.length() > 0 && !response.startsWith("*")) {
        String code = ezoReadResponse(timeout);
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

    while (Serial2.available()) Serial2.read();
    Serial2.print(cmd);
    Serial2.print('\r');

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
    Serial.printf("Live readings (temp source: %s):\n", tempSourceName());

    while (!Serial.available()) {
        float temp = getCompensationTemp();
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
        float temp = getCompensationTemp();
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
        float temp = getCompensationTemp();
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
    Serial.println("=== DUAL TEMP + EZO-EC TEST MENU ===");
    Serial.println();
    Serial.println("Readings:");
    Serial.println("  r          - Combined reading (both temps + EC)");
    Serial.println("  ec         - EC reading only (no temp comp)");
    Serial.println("  temp       - Both temp sensors side by side (10 samples)");
    Serial.println("  rtd        - PT1000 RTD detailed readings (10 samples)");
    Serial.println("  ds         - DS18B20 detailed readings (10 samples)");
    Serial.println("  cmp        - Compare temps for 60 sec with EC + delta stats");
    Serial.println("  c          - Toggle continuous mode (2s interval)");
    Serial.println();
    Serial.println("Temperature Source:");
    Serial.printf("  src        - Select temp source for EZO comp [current: %s]\n", tempSourceName());
    Serial.printf("  mt         - Set manual temperature [current: %.1f C]\n", manualTemp);
    Serial.println();
    Serial.println("Diagnostics:");
    Serial.println("  ezodiag    - EZO-EC full diagnostic");
    Serial.println("  rtddiag    - MAX31865 PT1000 diagnostic");
    Serial.println("  dsdiag     - DS18B20 diagnostic");
    Serial.println("  scan       - OneWire bus scan");
    Serial.println("  info       - EZO device info");
    Serial.println("  status     - EZO status (voltage, reset reason)");
    Serial.println("  find       - Blink EZO LED");
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

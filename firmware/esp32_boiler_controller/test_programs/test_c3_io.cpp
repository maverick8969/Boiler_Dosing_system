/**
 * @file test_c3_io.cpp
 * @brief ESP32 DevKit coprocessor I/O test: relays, internal ADC (valve + 2× CT RMS)
 *
 * Target: ESP32 DevKit (esp32dev). Relays on GPIO4/15; internal ADC for 4–20 mA valve (GPIO36)
 * and two SCT-013-005 CT channels (GPIO39 boiler, GPIO34 low water) with DC bias + software RMS.
 * No ADS1115. RS-485 auto-direction (no DE pin).
 *
 * Build: pio run -e test_c3_io -t upload -t monitor
 */

#include <Arduino.h>
#include <math.h>
#include "../include/c3_pin_definitions.h"

#define MA_CLOSED_MAX      5.0f
#define MA_OPEN_MIN       19.0f
#define MA_FAULT_LOW       3.0f
#define ADC_12BIT_MAX      4095.0f
#define ADC_VREF           3.3f
#define RMS_WINDOW_MS      40
#define RMS_SAMPLE_INTERVAL_MS  2

static bool blowdownRelayOn = false;
static bool solenoidRelayOn = false;

static float rawToVoltage(int raw) {
    return (raw / ADC_12BIT_MAX) * ADC_VREF;
}

float readFeedbackmA() {
    int raw = analogRead(C3_ADC_VALVE_PIN);
    float voltage = rawToVoltage(raw);
    float mA = (voltage / C3_4_20MA_R_SENSE) * 1000.0f;
    if (mA < 0.0f) mA = 0.0f;
    if (mA > 25.0f) mA = 25.0f;
    return mA;
}

// Sample one ADC pin over RMS_WINDOW_MS, return RMS voltage (AC component) for CT with DC bias
static bool readCTChannelRMS(int adcPin, float& out_Vrms, float& out_Arms) {
    out_Vrms = 0.0f;
    out_Arms = 0.0f;
    float samples[C3_CT_RMS_SAMPLES_MAX];
    int n = 0;
    uint32_t endMs = millis() + RMS_WINDOW_MS;
    while (millis() < endMs && n < C3_CT_RMS_SAMPLES_MAX) {
        int raw = analogRead(adcPin);
        samples[n++] = rawToVoltage(raw);
        delay(RMS_SAMPLE_INTERVAL_MS);
    }
    if (n < 2) return false;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += samples[i];
    float mean = sum / (float)n;
    float sumSq = 0.0f;
    for (int i = 0; i < n; i++) {
        float ac = samples[i] - mean;
        sumSq += ac * ac;
    }
    out_Vrms = (float)sqrt((double)(sumSq / (float)n));
    out_Arms = out_Vrms / C3_CT_V_PER_A;
    return true;
}

void printMenu() {
    Serial.println();
    Serial.println("--- ESP32 DevKit Coprocessor I/O Test (internal ADC) ---");
    Serial.println("  O = Blowdown relay ON (open)");
    Serial.println("  C = Blowdown relay OFF (closed)");
    Serial.println("  S = Solenoid relay ON");
    Serial.println("  s = Solenoid relay OFF");
    Serial.println("  T = Cycle both relays (test)");
    Serial.println("  r = Read 4–20 mA valve (GPIO36)");
    Serial.println("  b = Read boiler power CT (GPIO39) RMS");
    Serial.println("  w = Read low water CT (GPIO34) RMS");
    Serial.println("  a = Read all: valve + both CTs (RMS)");
    Serial.println("  h = This menu");
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  ESP32 DevKit Coprocessor I/O TEST");
    Serial.println("========================================");
    Serial.printf("  Blowdown relay:  GPIO%d\n", C3_BLOWDOWN_RELAY_PIN);
    Serial.printf("  Solenoid relay:  GPIO%d\n", C3_SOLENOID_RELAY_PIN);
    Serial.printf("  Valve 4–20 mA:   GPIO%d (internal ADC)\n", C3_ADC_VALVE_PIN);
    Serial.printf("  CT boiler:       GPIO%d  CT low water: GPIO%d (%d Hz line)\n",
                  C3_ADC_CT_BOILER_PIN, C3_ADC_CT_LOW_WATER_PIN, C3_CT_LINE_HZ);
    Serial.println("========================================");
    Serial.println();

    pinMode(C3_BLOWDOWN_RELAY_PIN, OUTPUT);
    digitalWrite(C3_BLOWDOWN_RELAY_PIN, LOW);
    pinMode(C3_SOLENOID_RELAY_PIN, OUTPUT);
    digitalWrite(C3_SOLENOID_RELAY_PIN, LOW);

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    printMenu();
}

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        switch (cmd) {
            case 'O':
                digitalWrite(C3_BLOWDOWN_RELAY_PIN, HIGH);
                blowdownRelayOn = true;
                Serial.println("Blowdown relay ON (valve open)");
                break;
            case 'C':
                digitalWrite(C3_BLOWDOWN_RELAY_PIN, LOW);
                blowdownRelayOn = false;
                Serial.println("Blowdown relay OFF (valve closed)");
                break;
            case 'S':
                digitalWrite(C3_SOLENOID_RELAY_PIN, HIGH);
                solenoidRelayOn = true;
                Serial.println("Solenoid relay ON");
                break;
            case 's':
                digitalWrite(C3_SOLENOID_RELAY_PIN, LOW);
                solenoidRelayOn = false;
                Serial.println("Solenoid relay OFF");
                break;
            case 'T': {
                Serial.println("Cycle: blowdown ON -> 2s -> OFF, solenoid ON -> 2s -> OFF");
                digitalWrite(C3_BLOWDOWN_RELAY_PIN, HIGH);
                delay(2000);
                digitalWrite(C3_BLOWDOWN_RELAY_PIN, LOW);
                digitalWrite(C3_SOLENOID_RELAY_PIN, HIGH);
                delay(2000);
                digitalWrite(C3_SOLENOID_RELAY_PIN, LOW);
                Serial.println("Cycle done.");
                break;
            }
            case 'r': {
                float mA = readFeedbackmA();
                Serial.printf("Valve (4–20 mA): %.2f mA (%s)\n", mA,
                    mA < MA_FAULT_LOW ? "fault/low" : mA < MA_CLOSED_MAX ? "closed" : mA >= MA_OPEN_MIN ? "open" : "moving");
                break;
            }
            case 'b': {
                float vrms, arms;
                if (readCTChannelRMS(C3_ADC_CT_BOILER_PIN, vrms, arms))
                    Serial.printf("Boiler power CT: %.3f V RMS  %.2f A RMS\n", vrms, arms);
                else
                    Serial.println("Boiler CT RMS read failed.");
                break;
            }
            case 'w': {
                float vrms, arms;
                if (readCTChannelRMS(C3_ADC_CT_LOW_WATER_PIN, vrms, arms))
                    Serial.printf("Low water CT:    %.3f V RMS  %.2f A RMS\n", vrms, arms);
                else
                    Serial.println("Low water CT RMS read failed.");
                break;
            }
            case 'a': {
                float mA = readFeedbackmA();
                float v1, a1, v2, a2;
                bool ok1 = readCTChannelRMS(C3_ADC_CT_BOILER_PIN, v1, a1);
                bool ok2 = readCTChannelRMS(C3_ADC_CT_LOW_WATER_PIN, v2, a2);
                Serial.println("--- Internal ADC ---");
                Serial.printf("  Valve:    %.2f mA\n", mA);
                Serial.printf("  Boiler:   %s  %.3f V RMS  %.2f A RMS\n", ok1 ? "" : "(fail)", v1, a1);
                Serial.printf("  Low H2O:  %s  %.3f V RMS  %.2f A RMS\n", ok2 ? "" : "(fail)", v2, a2);
                break;
            }
            case 'h':
            case '?':
                printMenu();
                break;
            case '\n':
            case '\r':
                break;
            default:
                Serial.printf("Unknown: '%c' - press h for menu\n", cmd);
                break;
        }
    }
    delay(50);
}

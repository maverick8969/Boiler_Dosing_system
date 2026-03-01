/**
 * @file c3_pin_definitions.h
 * @brief Pin definitions for ESP32 DevKit (boiler panel coprocessor)
 *
 * Target: ESP32 DevKit (esp32dev). RS-485 via auto-direction module (no DE pin).
 * Internal ADC for 4–20 mA valve feedback and 2× CT (SCT-013-005); no ADS1115.
 * EZO-EC on Serial1; MAX31865 on VSPI.
 */

#ifndef C3_PIN_DEFINITIONS_H
#define C3_PIN_DEFINITIONS_H

#include <Arduino.h>

// ============================================================================
// RS-485 (auto-direction module — no DE/RE pin)
// ============================================================================
#define C3_RS485_TX_PIN     17   // Serial2 TX → RS485 module
#define C3_RS485_RX_PIN     16   // Serial2 RX ← RS485 module
#define C3_RS485_DE_RE_PIN  -1   // No DE pin (auto-direction transceiver)

// ============================================================================
// RELAY OUTPUTS (blowdown valve, solenoid valve)
// ============================================================================
#define C3_BLOWDOWN_RELAY_PIN   4   // GPIO4
#define C3_SOLENOID_RELAY_PIN  15   // GPIO15

// ============================================================================
// INTERNAL ADC — 4–20 mA valve + 2× CT (SCT-013-005 with DC bias)
// ============================================================================
#define C3_ADC_VALVE_PIN        36   // ADC1_CH0 — 4–20 mA valve feedback (150 Ω → 0.6–3.0 V)
#define C3_ADC_CT_BOILER_PIN    39   // ADC1_CH3 — boiler power CT (DC bias + software RMS)
#define C3_ADC_CT_LOW_WATER_PIN 34   // ADC1_CH6 — low water CT (DC bias + software RMS)
#define C3_4_20MA_R_SENSE       150.0f   // Sense resistor (Ω): 4 mA→0.6 V, 20 mA→3.0 V

// SCT-013-005 with DC bias + software RMS
#define C3_CT_BIAS_V            1.65f    // DC bias at divider midpoint (V) for 3.3 V supply
#define C3_CT_V_PER_A            0.2f    // SCT-013-005: 200 mV/A, 1 V RMS at 5 A
#define C3_CT_LINE_HZ            60     // Line frequency (Hz) for RMS window; use 50 for 50 Hz
#define C3_CT_RMS_SAMPLES_MAX    64     // Max samples per RMS window (buffer size)

// ============================================================================
// ATLAS EZO-EC CONDUCTIVITY (Serial1 — HardwareSerial)
// ============================================================================
#define C3_EZO_TX_PIN   10   // Serial1 TX → EZO RX
#define C3_EZO_RX_PIN    9   // Serial1 RX ← EZO TX
#define C3_EZO_BAUD     9600

// ============================================================================
// MAX31865 PT1000 RTD (VSPI)
// ============================================================================
#define C3_SPI_CS_PIN    5   // GPIO5  — MAX31865 CS
#define C3_SPI_SCK_PIN  18   // GPIO18 — VSPI SCK
#define C3_SPI_MOSI_PIN 23   // GPIO23 — VSPI MOSI
#define C3_SPI_MISO_PIN 19   // GPIO19 — VSPI MISO

// ============================================================================
// PIN ASSIGNMENT SUMMARY — ESP32 DevKit coprocessor
// ============================================================================
//
//  GPIO   Function
//  ----   --------
//  4      Blowdown relay
//  5      MAX31865 CS
//  9      EZO-EC RX (Serial1)
//  10     EZO-EC TX (Serial1)
//  15     Solenoid relay
//  16     RS-485 RX (Serial2)
//  17     RS-485 TX (Serial2)
//  18     MAX31865 SCK (VSPI)
//  19     MAX31865 MISO (VSPI)
//  23     MAX31865 MOSI (VSPI)
//  34     ADC1 — low water CT (DC bias)
//  36     ADC1 — 4–20 mA valve feedback
//  39     ADC1 — boiler power CT (DC bias)
//  —      RS-485 DE/RE = -1 (auto-direction, no pin)

#endif // C3_PIN_DEFINITIONS_H

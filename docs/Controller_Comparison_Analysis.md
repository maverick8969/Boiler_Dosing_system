# Controller Comparison Analysis: Lakewood 1575e vs Walchem WBL400/410

## Executive Summary

This document provides a detailed comparison of the Lakewood 1575e and Walchem WBL400/410 boiler controllers to inform the design of a unified ESP32-based controller that incorporates the best features of both systems.

---

## 1. Hardware Architecture Comparison

### 1.1 Electrical Specifications

| Parameter | Lakewood 1575e | Walchem WBL400/410 | ESP32 Design Choice |
|-----------|----------------|--------------------|--------------------|
| Input Power | 120/240 VAC | 100-240 VAC | 24VDC (safer, simpler) |
| Power Consumption | 6W | 8A max | ~5W estimated |
| Relay Count | 4 (3A each, 10A total) | 2 (WBL400) / 6 (WBL410) | 3 stepper drivers + 1 relay |
| Relay Rating | 3A @ line voltage | 6A resistive | External relay board |

### 1.2 Measurement Performance

| Parameter | Lakewood 1575e | Walchem WBL400/410 | ESP32 Design Choice |
|-----------|----------------|--------------------|--------------------|
| Conductivity Range | 50-10,000 µS (cooling) | 0-10,000 µS/cm | 0-10,000 µS/cm |
| Conductivity Resolution | ±10 µS (<5000) / ±100 µS (>5000) | 1 µS/cm | 1 µS/cm target |
| Accuracy | ±1.0% of scale | ±1% of reading | ±1% of reading |
| Temperature Sensor | 500 NTC Thermistor | Pt1000 RTD | Pt1000 RTD (Sensorex) |
| Max Temp Range | 486°F (252°C) boiler | 401°F (205°C) | 205°C (sensor limited) |

### 1.3 User Interface

| Parameter | Lakewood 1575e | Walchem WBL400/410 | ESP32 Design Choice |
|-----------|----------------|--------------------|--------------------|
| Display | 128x64 pixel LCD | 2x16 character LCD | 20x4 LCD (2004) |
| Keypad | 16 tactile buttons | 8 buttons | 4x4 matrix keypad |
| LEDs | 6 (Power, Alarm, 4 Relays) | 8 (various) | WS2812 RGB addressable |
| USB Port | No | Yes (optional) | WiFi instead |

---

## 2. Control Features Comparison

### 2.1 Sampling Modes

| Mode | Lakewood 1575e | Walchem WBL400/410 | Notes |
|------|----------------|--------------------|----- |
| Continuous | Yes | Yes (Mode C) | Both support |
| Intermittent | Yes (Sample/Cycle) | Yes (Mode I) | Both support |
| Timed Blowdown | Via timers | Yes (Mode T) | Walchem more explicit |
| Time Proportional | No | Yes (Mode P) | **Walchem advantage** |

**Walchem Time Proportional Mode (P)**: Calculates blowdown duration proportional to conductivity deviation from setpoint. This is a PID-like control without the integral/derivative components.

### 2.2 Chemical Feed Modes

| Mode | Lakewood 1575e | Walchem WBL400/410 | Notes |
|------|----------------|--------------------|----- |
| Setpoint-based | Yes | No (separate from feed) | Lakewood dedicated |
| Blowdown + Feed | Via relay config | Mode A | Both support |
| % of Blowdown | Percent Blowdown Time | Mode B | Both support |
| % of Time | Percent of Time | Mode C | Both support |
| Water Meter (contact) | Water Meter mode | Mode D | Both support |
| Water Meter (paddlewheel) | Via K-factor | Mode E | Both support |
| Feed Schedule | Yes (12 entries) | No | **Lakewood advantage** |
| Alarm Relay | Yes | Yes (WBL410) | Both support |

### 2.3 Blowdown Control

| Feature | Lakewood 1575e | Walchem WBL400/410 | Notes |
|---------|----------------|--------------------|----- |
| Setpoint Control | Yes | Yes | Both support |
| Deadband | Yes | Yes (Continuous only) | Both support |
| Timeout Protection | Yes (with alarm) | Yes (with lockout) | Both support |
| Ball Valve Delay | Yes (0-99 sec) | No | **Lakewood advantage** |
| HOA Control | Via manual relay | Yes (explicit) | Both support |
| Cycles of Concentration | Yes (via 4-20mA input) | No | **Lakewood advantage** |

### 2.4 Water Meter Features

| Feature | Lakewood 1575e | Walchem WBL400/410 | Notes |
|---------|----------------|--------------------|----- |
| Meter Inputs | 2 | 2 | Same |
| Contact Head | Yes | Yes (Type C) | Both support |
| Paddlewheel | Yes (K-factor) | Yes (Type P) | Both support |
| Autotrol Turbine | Pre-configured | No | Lakewood specific |
| Flow Rate Display | Yes | Yes | Both support |
| Volume Totalizer | Yes (capacitor backup) | Yes (99,999,999 max) | Both support |

---

## 3. Alarm System Comparison

### 3.1 Alarm Types

| Alarm | Lakewood 1575e | Walchem WBL400/410 |
|-------|----------------|-------------------|
| High Conductivity | Yes (absolute) | Yes (% of setpoint) |
| Low Conductivity | Yes (absolute) | Yes (% of setpoint) |
| Blowdown Timeout | Yes | Yes |
| Relay/Feed Timeout | Yes (each relay) | Yes |
| No Flow | Yes | Yes |
| Sensor Error | Via TC alarms | Yes (explicit) |
| Temperature Error | TC Open/Shorted | Yes |
| Drum Level | Yes (2 inputs) | No |
| Min Makeup Cond | Yes (CoC mode) | No |

### 3.2 Alarm Configuration

| Feature | Lakewood 1575e | Walchem WBL400/410 |
|---------|----------------|-------------------|
| High Alarm | Absolute µS value | % of setpoint (1-50%) |
| Low Alarm | Absolute µS value | % of setpoint (1-50%) |
| Dedicated Alarm Relay | Via config | Yes (WBL410) |
| Auto-Clear | Most alarms | Most alarms |
| Manual Reset Required | Timeout alarms | Blowdown timeout |

**Design Choice**: Support both absolute and percentage-based alarm thresholds for flexibility.

---

## 4. Data Management Comparison

### 4.1 Configuration Storage

| Feature | Lakewood 1575e | Walchem WBL400/410 |
|---------|----------------|-------------------|
| Settings Storage | EEPROM | Internal memory |
| Volatile Data Backup | Capacitor (~1 day) | N/A |
| Config Export | No | USB (UCF.ini) |
| Config Import | No | USB |

### 4.2 Data Logging

| Feature | Lakewood 1575e | Walchem WBL400/410 |
|---------|----------------|-------------------|
| Data Logging | No | Optional USB |
| Log Interval | N/A | 10 minutes |
| Event Logging | No | Yes |
| Export Format | N/A | CSV |

**ESP32 Enhancement**: WiFi-based TimescaleDB logging with configurable intervals (1 second to 1 hour), plus local SD card backup.

---

## 5. Security Features Comparison

| Feature | Lakewood 1575e | Walchem WBL400/410 |
|---------|----------------|-------------------|
| Password Protection | Yes (4-digit) | Yes (4-digit) |
| Default Password | 2222 | 1995 |
| Access Levels | 2 (View/Technician) | 2 (Locked/Unlocked) |
| Timeout | Manual drop | 10 minutes |
| Recovery Method | Enter password | Power cycle + keys |

---

## 6. Key Differences Summary

### 6.1 Lakewood 1575e Advantages
1. **Feed Scheduling**: 12-entry time-based scheduler for biocide feeds
2. **Ball Valve Delay**: Built-in motorized valve timing
3. **Cycles of Concentration**: Automatic setpoint based on makeup/blowdown ratio
4. **Drum Level Inputs**: Dedicated safety interlocks
5. **Larger Display**: 128x64 graphical LCD

### 6.2 Walchem WBL400/410 Advantages
1. **Time Proportional Control**: PID-like blowdown based on deviation
2. **USB Data Logging**: Built-in logging capability
3. **Configuration Export/Import**: Portable settings
4. **Higher Temp Rating**: 205°C sensor capability
5. **Explicit HOA Mode**: Clear Hand-Off-Auto control
6. **Self-Test Function**: Built-in electronics verification
7. **Percentage-based Alarms**: Relative to setpoint

---

## 7. Unified ESP32 Design Recommendations

### 7.1 Control Features to Implement

| Feature | Source | Priority |
|---------|--------|----------|
| Continuous Sampling | Both | High |
| Intermittent Sampling | Both | High |
| Time Proportional Blowdown | Walchem Mode P | High |
| Feed Scheduling | Lakewood | Medium |
| Ball Valve Delay | Lakewood | Medium |
| HOA Control | Walchem | High |
| Self-Test | Walchem | Medium |
| Cycles of Concentration | Lakewood | Low |

### 7.2 Chemical Feed Modes to Implement

All five modes from Walchem plus Lakewood scheduling:

| Mode | Description |
|------|-------------|
| A | Blowdown + Feed with Lockout |
| B | Feed % of Blowdown Time |
| C | Feed % of Time (duty cycle) |
| D | Water Contactor Triggered |
| E | Paddlewheel Volume Triggered |
| S | Scheduled Feed (Lakewood) |

### 7.3 Alarm System Design

Combine both approaches:
- Support absolute thresholds (Lakewood style)
- Support percentage thresholds (Walchem style)
- Include drum level inputs (Lakewood)
- Implement self-test (Walchem)

### 7.4 Data Logging Enhancement

Far exceeds both controllers:
- WiFi connectivity for remote monitoring
- TimescaleDB for time-series storage
- Grafana dashboards for visualization
- Configurable logging intervals (1s - 1hr)
- Local SD card backup
- MQTT integration for BMS

---

## 8. Sensorex CS675HTTC-P1K/K=1.0 Integration Notes

Based on web research, this sensor provides:
- **Type**: Analog contacting conductivity sensor (not digital protocol)
- **Temperature Compensation**: Pt1000 RTD built-in
- **Cell Constant**: K=1.0 (for 0-10,000 µS/cm range)
- **Interface**: Requires analog signal conditioning circuit
- **Pressure Rating**: 0-250 psi at up to 250°C (482°F)
- **Connection**: 3/4" NPT male thread

The sensor requires an excitation signal (AC) and measures the resulting current flow. The ESP32 implementation will need:
1. AC excitation generator (using DAC or PWM + filter)
2. Current-to-voltage conversion circuit
3. ADC measurement with oversampling
4. Temperature reading from Pt1000 RTD via resistance measurement
5. Temperature compensation algorithm

---

*Document prepared for ESP32 Columbia CT-6 Boiler Dosing Controller Development*

# Lakewood Instruments Model 1575e - ESP32 Controller Design Specification

## Document Overview

This document provides a comprehensive analysis of the Lakewood Instruments Model 1575e Water Treatment System Conductivity Controller, extracted from the official installation and operation manual. It is intended to serve as the functional specification for designing an ESP32-based boiler dosing controller that replicates and enhances the 1575e's capabilities.

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Hardware Specifications](#2-hardware-specifications)
3. [Input/Output Requirements](#3-inputoutput-requirements)
4. [Menu System Architecture](#4-menu-system-architecture)
5. [Relay Configuration System](#5-relay-configuration-system)
6. [Conductivity Measurement](#6-conductivity-measurement)
7. [Chemical Dosing Control Algorithms](#7-chemical-dosing-control-algorithms)
8. [Alarm System](#8-alarm-system)
9. [Feed Schedule System](#9-feed-schedule-system)
10. [Water Meter Integration](#10-water-meter-integration)
11. [4-20mA Interface](#11-4-20ma-interface)
12. [Boiler-Specific Features](#12-boiler-specific-features)
13. [Security System](#13-security-system)
14. [Data Persistence](#14-data-persistence)
15. [ESP32 Implementation Mapping](#15-esp32-implementation-mapping)
16. [Enhanced Features Recommendations](#16-enhanced-features-recommendations)

---

## 1. System Overview

### 1.1 Product Description

The Model 1575e is a microprocessor-based, menu-driven water treatment controller designed for:
- **Cooling Towers**
- **Chill Loops**
- **Boilers** (primary focus for this implementation)
- **Condensate Systems**

### 1.2 Core Functions

| Function | Description |
|----------|-------------|
| Conductivity Tracking | Continuous monitoring and control of water conductivity |
| Blowdown Control | Automatic purging to maintain conductivity setpoint |
| Chemical Injection | Timed dosing of treatment chemicals |
| Flow Monitoring | Water meter integration for volume-based control |
| Cycles of Concentration | Ratio-based control using makeup/blowdown conductivity |
| Scheduled Feeds | Time-based biocide and chemical scheduling |

### 1.3 Certifications

- ETL Approved
- NEMA 4X Enclosure Rating

---

## 2. Hardware Specifications

### 2.1 Electrical Specifications

| Parameter | Specification |
|-----------|---------------|
| Power Input | 120/240 VAC, 50/60 Hz |
| Power Consumption | 6W |
| Relay Outputs | 4 relays, 3A each, 10A total maximum |
| Fuse F1 | 5x20mm, 10A, 250V, fast-blow |
| Fuse F2 | 5x20mm, 100mA, 250V, slow-blow |

### 2.2 Environmental Specifications

| Parameter | Specification |
|-----------|---------------|
| Operating Temperature | 32°F to 140°F (0°C to 60°C) |
| Storage Temperature | -4°F to 150°F (-20°C to 65°C) |
| Enclosure Rating | NEMA 4X |

### 2.3 Physical Specifications

| Parameter | Specification |
|-----------|---------------|
| Dimensions | 10.54" x 6.25" x 3.45" |
| Shipping Weight | < 5 lbs |
| Display | 128x64 pixel illuminated LCD |
| Keypad | 16 tactile steel-dome push buttons |

### 2.4 Sensor Specifications by Application

| Application | Max Pressure | Max Temperature | Min Flow |
|-------------|--------------|-----------------|----------|
| Cooling Tower | 140 psi (9.65 bar) @100°F | 140°F (60°C) | 1-5 gpm |
| Boiler | 600 psi (41.3 bar) | 486°F (252°C) | Varies w/orifice |
| Condensate | 70 psi (4.8 bar) | 392°F (200°C) | 1 gpm |

---

## 3. Input/Output Requirements

### 3.1 Input Channels

#### 3.1.1 Conductivity Sensor Input (Primary)

| Parameter | Specification |
|-----------|---------------|
| Sensor Type | 2-electrode conductivity sensor |
| Conductivity Range (Cooling Tower) | 50 - 10,000 µS |
| Conductivity Range (Boiler) | 500 - 8,000 µS |
| Conductivity Range (Condensate) | 10 - 100 µS |
| Resolution (< 5000 µS) | ± 10 µS |
| Resolution (> 5000 µS) | ± 100 µS |
| Accuracy | ± 1.0% of scale |
| Max Wire Distance (direct) | 20 feet |
| Temperature Compensation | Automatic (500 NTC thermistor) |
| Connection | Terminal Block P8 |

**Terminal Block P8 Pinout:**
```
Pin 1: GND      - Ground reference
Pin 2: TC+      - Temperature compensator positive
Pin 3: I-       - Current sense negative
Pin 4: I+       - Current sense positive (4-20mA)
Pin 5: 4-20mA IN - 4-20mA input signal
```

#### 3.1.2 4-20mA Secondary Input

| Parameter | Specification |
|-----------|---------------|
| Type | Non-isolated, internally powered |
| Input Range | 4-20 mA |
| Connection | Terminal P2, pins 4 (PWR) and 5 (IN) |

**Configurable Input Types:**
1. Microsiemens (conductivity)
2. pH
3. Millivolts (ORP)
4. PPM (trace chemistry)
5. PPB (trace chemistry)
6. Percent
7. PSI
8. Milliamps
9. GPM (flow rate)
10. LPM (flow rate)

#### 3.1.3 Flow Switch Input

| Parameter | Specification |
|-----------|---------------|
| Type | Digital contact closure |
| Voltage Rating | 24 VDC |
| Current Rating | 500 mA |
| Function | Disables relay outputs on loss of flow |
| Connection | Terminal P7 (FLOW, GND) |
| Default (no switch) | Jumper pins 1 & 2 |

#### 3.1.4 Water Meter Inputs (2 channels)

| Parameter | Specification |
|-----------|---------------|
| Input Types | Contact head, paddle wheel, turbine |
| Voltage (closed) | ~5 VDC |
| Voltage (open) | 0 VDC |
| Connection | Terminal P7 |

**Supported Water Meter Types:**
| Type | Configuration |
|------|---------------|
| Contacting Head | Gallons/Liters per contact |
| Paddle Wheel (Signet 2535/2540) | K-factor |
| Autotrol Turbine 1" | Pre-configured |
| Autotrol Turbine 2" | Pre-configured |

**Terminal Block P7 Pinout:**
```
Pin 1:  GND
Pin 2:  METER 2
Pin 3:  +5VDC
Pin 4:  GND
Pin 5:  METER 1
Pin 6:  +5VDC
Pin 7:  GND
Pin 8:  FLOW
Pin 9:  GND
Pin 10: DIG IN 2
Pin 11: GND
Pin 12: DIG IN 1
```

#### 3.1.5 Drum Switch Inputs (2 channels)

| Parameter | Specification |
|-----------|---------------|
| Type | Digital contact closure |
| Voltage Rating | 24 VDC |
| Current Rating | 500 mA |
| Connection | Terminal P5 (DIG IN 1, DIG IN 2) |

### 3.2 Output Channels

#### 3.2.1 Relay Outputs

| Relay | Function | Contact Type | Rating |
|-------|----------|--------------|--------|
| Relay 1 (BLOW) | Blowdown valve | NC + NO | 3A @ line voltage |
| Relay 2 | Configurable | NO only | 3A @ line voltage |
| Relay 3 | Configurable | NO only | 3A @ line voltage |
| Relay 4 | Configurable | NO only | 3A @ line voltage |

**Total Relay Current:** 10A maximum combined

**Relay 1 Terminal Connections:**
```
NC  - Normally Closed (connects to valve CLOSE)
NO  - Normally Open (connects to valve OPEN)
COM - Common
GND - Earth Ground
```

**Relays 2-4 Terminal Connections:**
```
NO  - Normally Open (HOT when energized)
COM - Neutral
GND - Earth Ground
```

#### 3.2.2 4-20mA Output

| Parameter | Specification |
|-----------|---------------|
| Output Type | Isolated or non-isolated (configurable) |
| Output Range | 4-20 mA |
| Power Options | Internally powered (non-isolated) or externally powered (isolated) |
| Function | Conductivity proportional output |
| Connection | Terminal P2 (+OUT, -OUT) |

**Terminal Block P2 Pinout:**
```
Pin 1: PWR    - 24VDC power for 4-20mA input device
Pin 2: +OUT   - 4-20mA output positive
Pin 3: -OUT   - 4-20mA output negative
Pin 4: GND    - Ground
```

### 3.3 LED Indicators

| LED | Color | Function |
|-----|-------|----------|
| POWER | Green | Power status |
| ALARM | Red | Active alarm indication |
| BLOWDOWN | Yellow | Relay 1 status |
| RELAY 2 | Yellow | Relay 2 status |
| RELAY 3 | Yellow | Relay 3 status |
| RELAY 4 | Yellow | Relay 4 status |

---

## 4. Menu System Architecture

### 4.1 Main Menu Structure

```
MAIN MENU
├── 1. PROCESS
│   ├── Conductivity Display
│   ├── Relay Status Display
│   ├── Setpoint Display
│   ├── Water Meter Totals
│   ├── Flow Rates
│   ├── Secondary Input Display
│   ├── Manual Relay Control (ENT key)
│   └── Calibration (PRO key)
│
├── 2. RELAYS
│   ├── 1. BLOW (Blowdown Relay)
│   │   ├── Based on Setpoint
│   │   │   ├── Setpoint Values
│   │   │   ├── When to Blowdown
│   │   │   ├── Boiler Timers
│   │   │   ├── Ball Valve Delay
│   │   │   └── Cycles of Concentration
│   │   └── Based on Volume
│   │       ├── Blowdown Volume
│   │       └── Ball Valve Delay
│   │
│   ├── 2. RLY2
│   ├── 3. RLY3
│   └── 4. RLY4
│       ├── 1. Disabled
│       ├── 2. Setpoint
│       ├── 3. Water Meter
│       ├── 4. Percent Blowdown
│       ├── 5. Percent of Time
│       ├── 6. Feed Schedule
│       └── 7. Alarm Relay
│
├── 3. FEED SCHEDULE
│   ├── 1. By Weekday
│   ├── 2. By Cycle Calendar
│   └── 3. List Schedule
│
├── 4. ALARMS
│   ├── On-board Conductivity Alarms
│   │   ├── High Alarm
│   │   └── Low Alarm
│   └── Secondary Input Alarms
│       ├── High Alarm
│       └── Low Alarm
│
├── 5. WATER METERS
│   ├── 1. MTR1
│   │   ├── Units (Gallons/Liters)
│   │   ├── Meter Type
│   │   └── Reset Total
│   └── 2. MTR2
│
├── 6. 4-20 MA IN/OUT
│   ├── 1. 4-20 MA OUT SETUP
│   │   ├── Set 4-20 mA Range
│   │   ├── Manual Control
│   │   └── Calibrate
│   └── 2. 4-20 MA IN SETUP
│       ├── Enable/Disable
│       ├── Set 4-20 mA Range
│       └── Calibrate
│
├── 7. SYSTEM SETUP
│   ├── 1. Process Parameters
│   │   ├── Cell Constant
│   │   ├── Temp Compensation
│   │   ├── Anti-flashing
│   │   └── Enable/Disable Conductivity
│   ├── 2. Initialization
│   │   ├── Calibration Reset
│   │   └── Whole Controller Reset
│   ├── 3. Security
│   ├── 4. Firmware Version
│   └── 5. Diagnostics
│
├── 8. CLOCK
│   ├── Day of Week
│   ├── Date
│   ├── Month
│   ├── Year
│   └── Time (HH:MM:SS)
│
└── 0. SECURITY LEVEL (hidden)
    └── Drop to View-Only
```

### 4.2 Keypad Functions

| Key | Function |
|-----|----------|
| 0-9 | Numeric input, menu selection |
| ENT | Accept value, enter submenu, access manual relay control |
| CLR | Cancel, exit menu, return to main menu |
| PRO | Enter calibration mode |
| DSP | Change display format (context sensitive) |
| ↑ | Scroll up, increment value |
| ↓ | Scroll down, decrement value |

### 4.3 Display Screens (Process Mode)

The Process screen cycles through multiple views using ↑/↓ arrows:

| Screen | Content |
|--------|---------|
| 1 | Date/Time + Conductivity |
| 2 | All Relay Status (graphical bars) |
| 3 | Blowdown Setpoint |
| 4 | Relay 2 Settings |
| 5 | Relay 3 Settings |
| 6 | Relay 4 Settings |
| 7 | MTR1 Total Flow |
| 8 | MTR2 Total Flow |
| 9 | MTR1 Flow Rate |
| 10 | Secondary Input Value |

---

## 5. Relay Configuration System

### 5.1 Blowdown Relay (Relay 1) Configuration

#### 5.1.1 Based on Setpoint Mode

**Parameters:**

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Setpoint | 0-99999 µS | 2500 µS | Target conductivity |
| Deadband | 0-9999 µS | 50 µS | Hysteresis band |
| Excess Blowdown Time | 00:00-17:59 | 00:00 | Timeout alarm (HH:MM) |
| When to Blowdown | Above/Below | Above | Setpoint direction |
| Ball Valve Delay | 0-99 sec | 0 sec | Open/close delay for motorized valves |

**Control Logic (Above Setpoint):**
```
IF (conductivity > setpoint + deadband/2) THEN
    OPEN blowdown valve
    Start blowdown timer
ENDIF

IF (conductivity < setpoint - deadband/2) THEN
    CLOSE blowdown valve
    Reset blowdown timer
ENDIF

IF (blowdown_timer > excess_blowdown_time AND excess_blowdown_time > 0) THEN
    TRIGGER "BLOWDOWN TIMEOUT" alarm
    // Note: Alarm does NOT close valve in setpoint mode
ENDIF
```

#### 5.1.2 Boiler Timers (Sample/Cycle Mode)

**Parameters:**

| Parameter | Range | Format | Description |
|-----------|-------|--------|-------------|
| Sample Time | 00:00-59:59 | MM:SS | Duration to sample conductivity |
| Cycle Time | 00:00-17:59 | HH:MM | Interval between samples |

**Control Logic:**
```
// Sample/Cycle Mode State Machine
STATE idle:
    Wait for cycle_timer to expire
    GOTO sampling

STATE sampling:
    OPEN blowdown valve
    Start sample_timer
    Read conductivity continuously
    IF (sample_timer expired) THEN
        IF (conductivity > setpoint) THEN
            Continue blowing down until setpoint reached
        ELSE
            CLOSE blowdown valve
            Reset cycle_timer
            GOTO idle
        ENDIF
    ENDIF
```

**Note:** Sample/Cycle mode is for boilers with < 1000 lbs/hr blowdown requirement.

#### 5.1.3 Cycles of Concentration Mode

**Requirements:** Secondary 4-20mA input must be enabled as conductivity (makeup water)

**Parameters:**

| Parameter | Range | Description |
|-----------|-------|-------------|
| Cycles of Concentration | 1.0-99.9 | Target ratio |
| Min Makeup Conductivity | 0-99999 µS | Minimum valid makeup reading |
| Deadband | 0-9999 µS | Hysteresis |

**Control Logic:**
```
calculated_setpoint = makeup_conductivity * cycles_of_concentration

IF (makeup_conductivity < min_makeup_conductivity) THEN
    calculated_setpoint = min_makeup_conductivity * cycles_of_concentration
    TRIGGER "MIN MAKEUP CONDUCTIVITY" alarm
ENDIF

// Then apply standard setpoint control using calculated_setpoint
```

#### 5.1.4 Based on Volume Mode

**Sub-modes:**

**Time Limited:**
| Parameter | Description |
|-----------|-------------|
| Water Meter Source | MTR1, MTR2, or BOTH |
| Volume Trigger | Gallons/Liters of makeup |
| Blowdown Duration | MM:SS to blow down |

**Volume Limited:**
| Parameter | Description |
|-----------|-------------|
| Makeup Water Meter | MTR1 or MTR2 |
| Makeup Volume | Gallons/Liters before blowdown |
| Blowdown Volume | Gallons/Liters to blow down |
| Excess Blowdown Time | Timeout (closes valve and alarms) |

### 5.2 Configurable Relays (2, 3, 4)

#### 5.2.1 Configuration Options

| Mode | Description |
|------|-------------|
| Disabled | Relay never energizes |
| Setpoint | Energize based on conductivity or secondary input |
| Water Meter | Energize for time after volume |
| Percent Blowdown | Energize for % of blowdown time after blowdown |
| Percent of Time | Continuous duty cycle |
| Feed Schedule | Time-of-day based |
| Alarm Relay | Energize on any alarm condition |

#### 5.2.2 Setpoint Mode Configuration

**Input Selection:**
- On-board Conductivity
- Secondary Input (if enabled)

**Secondary Input Measurement Types:**
- Standard (simple setpoint)
- Trace Chemistry (with correction factor)

**Parameters:**
| Parameter | Range | Description |
|-----------|-------|-------------|
| Setpoint | Varies by input type | Target value |
| Deadband | Varies | Hysteresis |
| Timeout | 00:00-17:59 HH:MM | Max run time (shuts off relay) |
| When to Feed | Above/Below | Setpoint direction |

#### 5.2.3 Trace Chemistry Control

**Additional Parameters:**
| Parameter | Range | Description |
|-----------|-------|-------------|
| Correction Factor | 000-999 | Compensates for system errors |
| Percent On-Time | 0-100% | Duty cycle while below setpoint |

**Control Logic:**
```
corrected_reading = raw_reading * (1 + correction_factor/1000)

IF (corrected_reading < setpoint - deadband/2) THEN
    // Feed using percent on-time duty cycle
    Execute percent_of_time control at configured percentage
ELSE IF (corrected_reading > setpoint + deadband/2) THEN
    // Stop feeding
    De-energize relay
ENDIF
```

#### 5.2.4 Water Meter Mode

**Parameters:**
| Parameter | Range | Description |
|-----------|-------|-------------|
| Water Meter | MTR1, MTR2, BOTH | Trigger source |
| Volume | 00000.0 gal/L | Volume before feed |
| Feed Duration | 00:00 MM:SS | Time to feed |

#### 5.2.5 Percent of Blowdown Time Mode

**Parameters:**
| Parameter | Range | Description |
|-----------|-------|-------------|
| Percent | 0-99% | Percentage of blowdown duration |

**Operation:** After blowdown valve closes, relay energizes for (blowdown_duration × percent/100)

#### 5.2.6 Percent of Time Mode

**Parameters:**
| Parameter | Range | Description |
|-----------|-------|-------------|
| Percent | 0-100% | Duty cycle |

**Timing Table (20-second base intervals):**

| Percent | ON Time | OFF Time |
|---------|---------|----------|
| 1% | 20 sec | 1980 sec (33 min) |
| 5% | 20 sec | 380 sec (6m 20s) |
| 10% | 20 sec | 180 sec (3 min) |
| 25% | 20 sec | 60 sec |
| 33% | 20 sec | 40 sec |
| 50% | 20 sec | 20 sec |
| 66% | 40 sec | 20 sec |
| 75% | 60 sec | 20 sec |
| 90% | 180 sec | 20 sec |
| 95% | 380 sec | 20 sec |
| 99% | 1980 sec | 20 sec |

**Chemical Feed Calculation:**
```
daily_feed = (percent/100) × operating_hours × pump_flow_rate
```

#### 5.2.7 Alarm Relay Mode

**Triggering Alarms:**
- HIGH CONDUCTIVITY
- LOW CONDUCTIVITY
- HIGH SECONDARY
- LOW SECONDARY
- MIN MAKEUP COND
- OPENED TC
- SHORTED TC
- DRUM LEVEL #1
- DRUM LEVEL #2
- BLOWDOWN TIMEOUT
- RELAY #2 TIMEOUT
- RELAY #3 TIMEOUT
- RELAY #4 TIMEOUT
- NO FLOW

---

## 6. Conductivity Measurement

### 6.1 Sensor Cell Constants

| Application | Sensor P/N | Cell Constant | Temp Comp |
|-------------|------------|---------------|-----------|
| Cooling Tower | 1167157, 1167158 | 1.000 | 500 NTC |
| Boiler (SR) | 1168374 | 0.100 | NONE |
| Condensate (0-100 µS) | 1104591, 1168617 | 0.100 | 500 NTC |
| Condensate (0-10 µS) | 1104592, 1169642 | 0.010 | 500 NTC |

### 6.2 Temperature Compensation

**Options:**
- 500 NTC: For sensors with built-in thermistor
- NONE: For boiler sensors (no compensation)

### 6.3 Anti-Flashing Feature

**Purpose:** Dampens rapid conductivity fluctuations caused by steam flashing in hot boiler samples.

**Application:** Enable only for boiler sensors in hot sample applications.

### 6.4 Calibration Procedure

**Single-Point Calibration:**
1. Ensure good flow past sensor
2. Take sample with handheld conductivity meter
3. Press PRO key from Process screen
4. Enter actual conductivity value
5. Press ENT to accept

**For Sample/Cycle Boilers:**
1. Manually energize blowdown relay
2. Allow flow for minimum 60 seconds
3. Take sample
4. Enter calibration value
5. Return to automatic control

### 6.5 Conductivity to TDS Conversion Table

| µS/cm | PPM | µS/cm | PPM | µS/cm | PPM |
|-------|-----|-------|-----|-------|-----|
| 100 | 56 | 500 | 300 | 2000 | 1300 |
| 200 | 115 | 1000 | 630 | 3000 | 2000 |
| 300 | 176 | 1500 | 970 | 5000 | 3500 |
| 400 | 240 | - | - | 10000 | 7400 |

---

## 7. Chemical Dosing Control Algorithms

### 7.1 Setpoint-Based Dosing

```c
// Pseudocode for setpoint-based relay control
void setpoint_control(relay_t *relay, float reading, config_t *cfg) {
    float upper_threshold = cfg->setpoint + (cfg->deadband / 2.0);
    float lower_threshold = cfg->setpoint - (cfg->deadband / 2.0);

    if (cfg->when_to_feed == ABOVE_SETPOINT) {
        if (reading > upper_threshold && !relay->is_on) {
            relay_on(relay);
            start_timeout_timer(relay, cfg->timeout);
        }
        if (reading < lower_threshold && relay->is_on) {
            relay_off(relay);
            stop_timeout_timer(relay);
        }
    } else { // BELOW_SETPOINT
        if (reading < lower_threshold && !relay->is_on) {
            relay_on(relay);
            start_timeout_timer(relay, cfg->timeout);
        }
        if (reading > upper_threshold && relay->is_on) {
            relay_off(relay);
            stop_timeout_timer(relay);
        }
    }

    // Timeout check
    if (relay->is_on && timeout_expired(relay)) {
        relay_off(relay);
        trigger_alarm(RELAY_TIMEOUT, relay->id);
    }
}
```

### 7.2 Volume-Based Dosing

```c
// Pseudocode for water meter-based relay control
void volume_based_control(relay_t *relay, meter_t *meter, config_t *cfg) {
    static uint32_t last_volume = 0;
    uint32_t current_volume = get_meter_total(meter);
    uint32_t delta = current_volume - last_volume;

    if (!relay->is_on && delta >= cfg->volume_trigger) {
        relay_on(relay);
        start_feed_timer(relay, cfg->feed_duration);
        last_volume = current_volume;
    }

    if (relay->is_on && feed_timer_expired(relay)) {
        relay_off(relay);
    }
}
```

### 7.3 Percent-of-Time Dosing

```c
// Pseudocode for percent-of-time relay control
void percent_time_control(relay_t *relay, uint8_t percent) {
    const uint32_t BASE_INTERVAL = 20000; // 20 seconds in milliseconds

    // Calculate on/off times
    uint32_t cycle_time = (100 * BASE_INTERVAL) / gcd(percent, 100-percent);
    uint32_t on_time = (cycle_time * percent) / 100;
    uint32_t off_time = cycle_time - on_time;

    // Ensure minimum 20-second intervals
    if (on_time < BASE_INTERVAL) on_time = BASE_INTERVAL;
    if (off_time < BASE_INTERVAL) off_time = BASE_INTERVAL;

    static uint32_t cycle_start = 0;
    uint32_t elapsed = millis() - cycle_start;

    if (elapsed < on_time) {
        relay_on(relay);
    } else if (elapsed < on_time + off_time) {
        relay_off(relay);
    } else {
        cycle_start = millis();
    }
}
```

### 7.4 Ball Valve Delay Logic

```c
// Pseudocode for motorized ball valve control
void ball_valve_control(relay_t *relay, uint8_t delay_seconds, bool should_open) {
    static valve_state_t state = VALVE_IDLE;
    static uint32_t state_time = 0;

    switch (state) {
        case VALVE_IDLE:
            if (should_open && !relay->is_open) {
                set_relay_open_signal(relay);
                state = VALVE_OPENING;
                state_time = millis();
            } else if (!should_open && relay->is_open) {
                set_relay_close_signal(relay);
                state = VALVE_CLOSING;
                state_time = millis();
            }
            break;

        case VALVE_OPENING:
            if (millis() - state_time >= delay_seconds * 1000) {
                relay->is_open = true;
                state = VALVE_IDLE;
            }
            break;

        case VALVE_CLOSING:
            if (millis() - state_time >= delay_seconds * 1000) {
                relay->is_open = false;
                state = VALVE_IDLE;
            }
            break;
    }
}
```

---

## 8. Alarm System

### 8.1 Alarm Types and Conditions

| Alarm | Condition | Auto-Clear |
|-------|-----------|------------|
| HIGH CONDUCTIVITY | Reading > high_alarm_setpoint | Yes |
| LOW CONDUCTIVITY | Reading < low_alarm_setpoint | Yes |
| HIGH SECONDARY | Secondary > high_alarm | Yes |
| LOW SECONDARY | Secondary < low_alarm | Yes |
| MIN MAKEUP COND | Makeup < minimum (CoC mode) | Yes |
| OPENED TC | Temperature comp open circuit | Yes |
| SHORTED TC | Temperature comp short circuit | Yes |
| DRUM LEVEL #1 | Digital input 1 active | Yes |
| DRUM LEVEL #2 | Digital input 2 active | Yes |
| BLOWDOWN TIMEOUT | Blowdown exceeded max time | Manual |
| RELAY #2 TIMEOUT | Relay 2 exceeded max time | Manual |
| RELAY #3 TIMEOUT | Relay 3 exceeded max time | Manual |
| RELAY #4 TIMEOUT | Relay 4 exceeded max time | Manual |
| NO FLOW | Flow switch open | Yes |

### 8.2 Alarm Behavior

- Alarm condition displays as flashing message in middle of screen
- Multiple alarms display sequentially
- Alarm LED illuminates
- Relay configured as "Alarm Relay" energizes
- **HIGH CONDUCTIVITY alarm overrides setpoint and forces blowdown**

### 8.3 Alarm Configuration

**Conductivity Alarms:**
| Parameter | Range |
|-----------|-------|
| High Alarm | 0-99999 µS |
| Low Alarm | 0-99999 µS |

**Secondary Input Alarms:**
| Parameter | Range |
|-----------|-------|
| High Alarm | Based on configured input type |
| Low Alarm | Based on configured input type |

---

## 9. Feed Schedule System

### 9.1 Schedule Types

#### 9.1.1 By Weekday

7-day repeating schedule using day names (Monday-Sunday).

#### 9.1.2 By Cycle Calendar

Custom cycle length (1-28 days), day numbers instead of names.

### 9.2 Schedule Entry Parameters

| Parameter | Format | Description |
|-----------|--------|-------------|
| Relay | RLY2/RLY3/RLY4 | Which relay to activate |
| Day | 1-28 or Mon-Sun | When to activate |
| Start Time | HH:MM (24-hour) | Time to begin sequence |
| Cond Setpoint | µS | Pre-bleed target (0 disables) |
| Blow Duration | MM:SS | Max pre-bleed time |
| Feed Duration | MM:SS | Chemical feed time |
| Lockout Time | MM:SS | All-relay lockout after feed |

**Maximum Entries:** 12 scheduled feeds total

### 9.3 Feed Schedule Sequence

```
1. At START_TIME on scheduled DAY:
2. IF COND_SETPOINT > 0:
   a. Set temporary blowdown setpoint to COND_SETPOINT
   b. Blowdown until setpoint reached OR BLOW_DURATION expires
   c. Close blowdown valve
3. Energize scheduled RELAY for FEED_DURATION
4. De-energize relay
5. IF LOCKOUT_TIME > 0:
   a. Disable BLOW, RLY2, RLY3, RLY4 for LOCKOUT_TIME
6. Return to normal operation
```

---

## 10. Water Meter Integration

### 10.1 Meter Types and Configuration

#### 10.1.1 Contacting Head Meters

| Parameter | Range |
|-----------|-------|
| Gallons/Liters per Contact | 0.1 - 9999.9 |

#### 10.1.2 Paddle Wheel Meters

| Parameter | Range |
|-----------|-------|
| K-Factor | Pulses per gallon/liter |

**Signet 2535/2540 Wiring:**
```
Black (+24VDC) → Terminal A → 10K resistor → +5VDC
Red (Signal)   → Terminal B → METER input
Shield (GND)   → Terminal C → GND
```

#### 10.1.3 Autotrol Turbine Meters

Pre-configured K-factors:
- 1" Turbine: Factory preset
- 2" Turbine: Factory preset

### 10.2 Flow Rate Calculation

```c
// Flow rate calculation from pulse meter
float calculate_flow_rate(meter_t *meter) {
    static uint32_t last_count = 0;
    static uint32_t last_time = 0;

    uint32_t current_count = meter->pulse_count;
    uint32_t current_time = millis();

    uint32_t delta_count = current_count - last_count;
    uint32_t delta_time = current_time - last_time;

    float volume = delta_count * meter->volume_per_pulse;
    float time_hours = delta_time / 3600000.0;

    float flow_rate = volume / time_hours; // GPH or LPH

    last_count = current_count;
    last_time = current_time;

    return flow_rate / 60.0; // Convert to GPM or LPM
}
```

---

## 11. 4-20mA Interface

### 11.1 Output Configuration

| Parameter | Range | Description |
|-----------|-------|-------------|
| 4mA Value | 0-99999 µS | Conductivity at 4mA |
| 20mA Value | 0-99999 µS | Conductivity at 20mA |

**Output Calculation:**
```c
float conductivity_to_ma(float conductivity, float ma4_value, float ma20_value) {
    float span = ma20_value - ma4_value;
    float ma = 4.0 + (16.0 * (conductivity - ma4_value) / span);
    return constrain(ma, 4.0, 20.0);
}
```

### 11.2 Input Configuration

#### 11.2.1 As Primary Conductivity

Replaces on-board conductivity sensor input.

#### 11.2.2 As Secondary Input

| Input Type | Range Format | Typical Use |
|------------|--------------|-------------|
| Microsiemens | 0-99999 µS | Makeup conductivity |
| pH | 0.00-14.00 pH | pH monitoring |
| Millivolts | -2000 to +2000 mV | ORP |
| PPM | 0-9999.9 ppm | Trace chemistry |
| PPB | 0-9999.9 ppb | Trace chemistry |
| Percent | 0-100% | General |
| PSI | 0-9999 psi | Pressure |
| Milliamps | 0-20.0 mA | General |
| GPM | 0-9999 GPM | Flow |
| LPM | 0-9999 LPM | Flow |

### 11.3 Calibration

**Output Calibration:**
1. Connect milliamp meter in series
2. Enter actual readings for 4mA and 20mA points

**Input Calibration:**
1. Apply known mA signal
2. Enter actual mA reading

---

## 12. Boiler-Specific Features

### 12.1 Sample/Cycle vs. Continuous Sample

| Method | Blowdown Rate | Application |
|--------|---------------|-------------|
| Sample/Cycle | < 1000 lbs/hr | Small boilers, low blowdown |
| Continuous | > 1000 lbs/hr | Large boilers, high blowdown |

### 12.2 Plumbing Requirements

**Critical Points:**
- Use skimmer blowdown line (NOT bottom blowdown)
- Sensor must be 2+ feet below water level
- Orifice plates within 5 feet of sensor
- Horizontal sensor mounting
- Maximum 20 ft wire distance

### 12.3 Orifice Sizing Chart

| Pressure (PSIG) | 5000 lb/hr | 10000 lb/hr | 15000 lb/hr | 20000 lb/hr |
|-----------------|------------|-------------|-------------|-------------|
| 15 | 0.125" | 0.200" | 0.250" | 0.300" |
| 100 | 0.100" | 0.150" | 0.200" | 0.225" |
| 150 | 0.100" | 0.125" | 0.175" | 0.200" |
| 250 | 0.075" | 0.100" | 0.150" | 0.175" |
| 500 | 0.050" | 0.075" | 0.100" | 0.125" |
| 900 | 0.040" | 0.050" | 0.075" | 0.100" |

### 12.4 Ball Valve Delay Settings

| Valve Type | Recommended Delay |
|------------|-------------------|
| Solenoid | 0 seconds |
| Worcester Actuator | 8 seconds |

---

## 13. Security System

### 13.1 Security Levels

| Level | Access |
|-------|--------|
| View-Only | Process screens, manual relay control only |
| Technician | Full menu access |

### 13.2 Password System

- Default password: **2222**
- Password length: 4 digits
- Change via SYSTEM SETUP → SECURITY

### 13.3 Security Flow

```
View-Only Mode:
├── Can view Process screens
├── Can manually operate relays (5-min timeout)
├── Cannot access Main Menu
└── Enter password to unlock

Technician Mode:
├── Full menu access
├── Press "0" from Main Menu to drop to View-Only
└── Requires password knowledge warning
```

---

## 14. Data Persistence

### 14.1 EEPROM Storage (Permanent)

**Survives power loss indefinitely:**
- All setpoints
- Calibration values
- Relay configurations
- Alarm settings
- Feed schedules
- System parameters

### 14.2 Capacitive Backup (Temporary)

**Survives ~1 day after power loss:**
- Water meter totals
- Clock/calendar
- Current relay states

### 14.3 Data Structure Recommendation for ESP32

```c
typedef struct {
    // Header
    uint32_t magic;           // 0x1575E001
    uint16_t version;
    uint16_t checksum;

    // Conductivity settings
    float cell_constant;      // 0.01 - 10.0
    uint8_t temp_comp;        // 0=None, 1=500NTC
    uint8_t anti_flash;       // 0=Off, 1=On
    uint8_t cond_enabled;     // 0=Disabled, 1=Enabled
    float cond_cal_factor;    // Calibration multiplier

    // Blowdown relay config
    struct {
        uint8_t mode;         // 0=Setpoint, 1=Volume
        uint16_t setpoint;    // µS
        uint16_t deadband;    // µS
        uint16_t timeout;     // seconds
        uint8_t direction;    // 0=Above, 1=Below
        uint8_t ball_delay;   // seconds
        uint16_t sample_time; // seconds (boiler)
        uint16_t cycle_time;  // seconds (boiler)
        float coc_setpoint;   // Cycles of concentration
        uint16_t min_makeup;  // µS
    } blow_config;

    // Configurable relay configs (3)
    struct {
        uint8_t mode;         // 0-7 (see modes)
        uint8_t input_select; // 0=Cond, 1=Secondary
        uint16_t setpoint;
        uint16_t deadband;
        uint16_t timeout;     // seconds
        uint8_t direction;    // 0=Above, 1=Below
        uint8_t meter_select; // 0=MTR1, 1=MTR2, 2=Both
        float volume;         // gallons/liters
        uint16_t feed_time;   // seconds
        uint8_t percent;      // 0-100
        uint16_t correction;  // Trace chemistry
    } relay_config[3];

    // Alarms
    struct {
        uint16_t cond_high;
        uint16_t cond_low;
        float sec_high;
        float sec_low;
    } alarms;

    // Water meters
    struct {
        uint8_t type;         // 0=Contact, 1=Paddle, 2=Auto1, 3=Auto2
        uint8_t units;        // 0=Gal, 1=Liter
        float pulse_factor;   // vol per pulse or K-factor
    } meter_config[2];

    // 4-20mA config
    struct {
        float out_4ma_value;
        float out_20ma_value;
        float out_cal_4ma;
        float out_cal_20ma;
        uint8_t in_enabled;   // 0=Off, 1=Cond, 2=Secondary
        uint8_t in_type;      // 0-9 (input types)
        float in_4ma_value;
        float in_20ma_value;
        float in_cal_factor;
    } analog_config;

    // Feed schedule
    struct {
        uint8_t mode;         // 0=Weekday, 1=Cycle
        uint8_t cycle_days;   // 1-28
        uint8_t current_day;  // 1-28
        struct {
            uint8_t relay;    // 0=None, 2=RLY2, 3=RLY3, 4=RLY4
            uint8_t day;      // 0-27 or 0-6
            uint16_t start;   // minutes from midnight
            uint16_t cond_sp; // Pre-bleed setpoint
            uint16_t blow_dur;// seconds
            uint16_t feed_dur;// seconds
            uint16_t lockout; // seconds
        } entries[12];
    } schedule;

    // Security
    uint16_t password;        // 4-digit code

} config_t;
```

---

## 15. ESP32 Implementation Mapping

### 15.1 GPIO Pin Recommendations

| Function | ESP32 Pin | Notes |
|----------|-----------|-------|
| **Conductivity Sensor** | | |
| I+ (Excitation) | GPIO25 (DAC1) | AC excitation signal |
| I- (Sense) | GPIO34 (ADC1_CH6) | Current measurement |
| TC (Thermistor) | GPIO35 (ADC1_CH7) | Temperature comp |
| **4-20mA** | | |
| Output | GPIO26 (DAC2) | Via op-amp driver |
| Input | GPIO36 (ADC1_CH0) | 250Ω burden resistor |
| **Relay Outputs** | | |
| Relay 1 NO | GPIO16 | Blowdown OPEN |
| Relay 1 NC | GPIO17 | Blowdown CLOSE |
| Relay 2 | GPIO18 | Chemical pump 1 |
| Relay 3 | GPIO19 | Chemical pump 2 |
| Relay 4 | GPIO21 | Chemical pump 3 / Alarm |
| **Digital Inputs** | | |
| Flow Switch | GPIO22 | Pull-up enabled |
| Drum Switch 1 | GPIO23 | Pull-up enabled |
| Drum Switch 2 | GPIO27 | Pull-up enabled |
| **Water Meters** | | |
| Meter 1 | GPIO32 | Interrupt capable |
| Meter 2 | GPIO33 | Interrupt capable |
| **Display** | | |
| SDA | GPIO21 | I2C (if using OLED) |
| SCL | GPIO22 | I2C |
| **Keypad** | | |
| Rows | GPIO12-15 | 4x4 matrix |
| Cols | GPIO4,5,13,14 | 4x4 matrix |
| **Status LEDs** | | |
| Power | GPIO2 | Built-in LED |
| Alarm | GPIO0 | External LED |

### 15.2 Peripheral Requirements

| Component | Specification |
|-----------|---------------|
| Display | 128x64 OLED (SSD1306) or LCD (ST7920) |
| RTC | DS3231 module with battery backup |
| EEPROM | AT24C256 (32KB) or use ESP32 NVS |
| Relay Module | 4-channel, 3A rated, optoisolated |
| 4-20mA Output | XTR111 or similar current loop driver |
| 4-20mA Input | 250Ω precision resistor + ADC |
| Power Supply | 24VDC for loop power, 5V for logic |

### 15.3 Software Architecture

```
┌─────────────────────────────────────────────────────┐
│                    Main Loop                        │
├─────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │
│  │  UI Task    │  │ Control Task│  │  Comm Task  │ │
│  │ (Core 0)    │  │  (Core 1)   │  │  (Core 0)   │ │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘ │
│         │                │                │        │
│  ┌──────▼──────┐  ┌──────▼──────┐  ┌──────▼──────┐ │
│  │   Display   │  │   Relays    │  │    WiFi     │ │
│  │   Keypad    │  │   Sensors   │  │    MQTT     │ │
│  │   Menu      │  │   Alarms    │  │    Web UI   │ │
│  └─────────────┘  └─────────────┘  └─────────────┘ │
├─────────────────────────────────────────────────────┤
│                    HAL Layer                        │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐   │
│  │   ADC   │ │   DAC   │ │   GPIO  │ │   I2C   │   │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘   │
└─────────────────────────────────────────────────────┘
```

### 15.4 FreeRTOS Task Structure

```c
// Task priorities (higher = more important)
#define PRIORITY_CONTROL    5   // Real-time control loop
#define PRIORITY_SENSORS    4   // Sensor reading
#define PRIORITY_ALARMS     3   // Alarm processing
#define PRIORITY_UI         2   // Display and keypad
#define PRIORITY_COMM       1   // WiFi/MQTT (lowest)

// Task stack sizes
#define STACK_CONTROL   4096
#define STACK_SENSORS   2048
#define STACK_ALARMS    2048
#define STACK_UI        4096
#define STACK_COMM      8192

// Task periods
#define PERIOD_CONTROL_MS   100   // 10 Hz control loop
#define PERIOD_SENSORS_MS   500   // 2 Hz sensor read
#define PERIOD_ALARMS_MS    1000  // 1 Hz alarm check
#define PERIOD_UI_MS        50    // 20 Hz UI update
```

---

## 16. Enhanced Features Recommendations

The following features are NOT included in the Lakewood 1575e but would significantly improve an ESP32-based implementation:

### 16.1 Connectivity Enhancements

| Feature | Description | Benefit |
|---------|-------------|---------|
| **WiFi Connectivity** | ESP32 native WiFi | Remote monitoring/control |
| **MQTT Integration** | Publish sensor data, subscribe to commands | SCADA integration |
| **Web Dashboard** | Embedded web server with real-time display | Browser-based monitoring |
| **OTA Updates** | Over-the-air firmware updates | Remote maintenance |
| **REST API** | HTTP endpoints for integration | Third-party system integration |
| **Modbus TCP/RTU** | Industrial protocol support | PLC/BMS integration |

### 16.2 Data Logging Improvements

| Feature | Description | Benefit |
|---------|-------------|---------|
| **SD Card Logging** | Continuous data recording | Historical analysis |
| **Cloud Upload** | AWS IoT / Azure / Google Cloud | Long-term storage |
| **Trend Graphs** | On-screen historical plots | Visual troubleshooting |
| **Event Logging** | Timestamped alarm/action history | Audit trail |
| **Data Export** | CSV/JSON download capability | Reporting |

### 16.3 Advanced Control Features

| Feature | Description | Benefit |
|---------|-------------|---------|
| **PID Control** | Proportional-Integral-Derivative | Smoother conductivity control |
| **Adaptive Tuning** | Auto-tune PID parameters | Self-optimizing |
| **Predictive Blowdown** | Machine learning prediction | Reduced water waste |
| **Multi-Setpoint Profiles** | Time-based setpoint changes | Day/night operation |
| **Cascade Control** | CoC based on multiple inputs | Better concentration control |
| **Feedforward Control** | React to load changes | Faster response |

### 16.4 Enhanced Sensor Support

| Feature | Description | Benefit |
|---------|-------------|---------|
| **Multiple Conductivity Inputs** | 2+ sensors | Redundancy, comparison |
| **pH Electrode Input** | Direct pH measurement | Corrosion control |
| **ORP Sensor Input** | Oxidation-reduction potential | Biocide effectiveness |
| **Temperature Logging** | Independent temp measurement | Process monitoring |
| **Pressure Transducer** | Boiler pressure monitoring | Safety interlock |
| **Level Sensors** | Ultrasonic/pressure level | Tank monitoring |

### 16.5 User Interface Improvements

| Feature | Description | Benefit |
|---------|-------------|---------|
| **Color Touchscreen** | 3.5" or larger TFT | Modern interface |
| **Graphical Trends** | Real-time plotting | Visual analysis |
| **Multiple Languages** | i18n support | Global deployment |
| **Mobile App** | iOS/Android companion | Remote access |
| **Voice Alerts** | Audio alarm announcements | Hands-free notification |
| **QR Code Configuration** | Scan to configure | Rapid commissioning |

### 16.6 Safety Enhancements

| Feature | Description | Benefit |
|---------|-------------|---------|
| **Watchdog Timer** | Hardware watchdog | Fail-safe operation |
| **Redundant Relay** | Backup blowdown control | Critical protection |
| **Leak Detection** | Water presence sensor | Equipment protection |
| **Chemical Level Monitoring** | Tank level sensors | Prevent dry running |
| **Pump Current Monitoring** | Verify pump operation | Confirm dosing |
| **Emergency Stop Input** | External E-stop | Safety interlock |

### 16.7 Maintenance Features

| Feature | Description | Benefit |
|---------|-------------|---------|
| **Sensor Health Monitoring** | Drift detection, fouling alerts | Predictive maintenance |
| **Calibration Due Reminders** | Scheduled calibration alerts | Compliance |
| **Pump Runtime Tracking** | Total operating hours | Maintenance planning |
| **Filter Change Alerts** | Usage-based reminders | Preventive maintenance |
| **Diagnostic Mode** | Comprehensive self-test | Troubleshooting |
| **Remote Diagnostics** | Cloud-based support | Faster resolution |

### 16.8 Energy Efficiency Features

| Feature | Description | Benefit |
|---------|-------------|---------|
| **Water Usage Tracking** | Detailed consumption metrics | Cost analysis |
| **Chemical Usage Tracking** | Dosing volume tracking | Inventory management |
| **Efficiency Reports** | CoC, blowdown rate analysis | Optimization |
| **Energy Cost Calculation** | Heat loss per blowdown | ROI demonstration |
| **Setpoint Optimization** | Automatic setpoint tuning | Water/chemical savings |

### 16.9 Implementation Priority Matrix

| Priority | Feature | Complexity | Value |
|----------|---------|------------|-------|
| **High** | WiFi + Web Dashboard | Medium | High |
| **High** | MQTT Integration | Low | High |
| **High** | Data Logging (SD) | Low | High |
| **High** | Event/Alarm History | Low | High |
| **Medium** | OTA Updates | Medium | Medium |
| **Medium** | PID Control | Medium | High |
| **Medium** | Trend Graphs | Medium | Medium |
| **Medium** | Multiple Sensor Inputs | Medium | Medium |
| **Low** | Mobile App | High | Medium |
| **Low** | Predictive Analytics | High | High |
| **Low** | Voice Alerts | Low | Low |

---

## Appendix A: Quick Reference

### A.1 Default Values After Initialization

| Parameter | Default |
|-----------|---------|
| Conductivity Setpoint | 2500 µS |
| Deadband | 50 µS |
| High Alarm | 5000 µS |
| Low Alarm | 0 µS |
| Cell Constant | 1.000 |
| Temp Compensation | 500 NTC |
| Password | 2222 |
| Ball Valve Delay | 0 sec |
| Sample Time | 00:00 |
| Cycle Time | 00:00 |

### A.2 Important Timings

| Function | Minimum | Maximum |
|----------|---------|---------|
| Ball Valve Delay | 0 sec | 99 sec |
| Manual Relay Timeout | Fixed | 5 min |
| Blowdown Timeout | 0 (disabled) | 17h 59m |
| Relay Timeout | 0 (disabled) | 17h 59m |
| Feed Duration | 00:00 | 59:59 |
| Lockout Time | 00:00 | 59:59 |
| Sample Time (boiler) | 00:00 | 59:59 |
| Cycle Time (boiler) | 00:00 | 17:59 |

### A.3 Wiring Color Codes

**Cooling Tower Sensor:**
| Wire | Function |
|------|----------|
| Green | GND |
| White | TC+ |
| Red | I- |
| Black | I+ |

**Boiler Sensor:**
| Wire | Function |
|------|----------|
| Red | I- |
| Black | I+ |

**Condensate Sensor:**
| Wire | Function |
|------|----------|
| Green | GND |
| White | TC+ |
| Red | I- |
| Black | I+ |

---

## Appendix B: Error Messages Reference

| Message | Meaning | Action |
|---------|---------|--------|
| HIGH CONDUCTIVITY | Above high alarm | Check blowdown system |
| LOW CONDUCTIVITY | Below low alarm | Check for overflow |
| BLOWDOWN TIMEOUT | Max blowdown time exceeded | Check valve, strainer |
| NO FLOW | Flow switch open | Check pump, flow switch |
| OPENED TC | Temp comp disconnected | Check wiring, sensor |
| SHORTED TC | Temp comp shorted | Check wiring, sensor |
| DRUM LEVEL #1 | Digital input 1 active | Check boiler level |
| DRUM LEVEL #2 | Digital input 2 active | Check boiler level |
| RELAY #x TIMEOUT | Relay exceeded max time | Check chemical system |
| MIN MAKEUP COND | Makeup below minimum | Check makeup sensor |
| FEED SEQUENCE ACTIVE | Scheduled feed running | Informational only |

---

## Document Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2024-XX-XX | ESP32 Design Team | Initial specification from 1575e manual |

---

*This document is based on the Lakewood Instruments Model 1575e Installation & Operation Manual. All specifications are derived from the original documentation for the purpose of designing a compatible ESP32-based controller.*

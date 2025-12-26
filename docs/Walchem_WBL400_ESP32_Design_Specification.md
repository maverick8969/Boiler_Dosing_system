# Walchem WBL400/410 Series - ESP32 Controller Design Specification

## Document Overview

This document provides a comprehensive analysis of the Walchem WBL400/410 Series Boiler Controller, extracted from the official instruction manual. It is intended to serve as the functional specification for designing an ESP32-based boiler dosing controller that replicates and enhances the WBL400/410's capabilities.

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Hardware Specifications](#2-hardware-specifications)
3. [Input/Output Requirements](#3-inputoutput-requirements)
4. [Menu System Architecture](#4-menu-system-architecture)
5. [Conductivity Measurement System](#5-conductivity-measurement-system)
6. [Sampling Mode System](#6-sampling-mode-system)
7. [Blowdown Control System](#7-blowdown-control-system)
8. [Chemical Feed Control System](#8-chemical-feed-control-system)
9. [Water Meter Integration](#9-water-meter-integration)
10. [Alarm System](#10-alarm-system)
11. [4-20mA Interface](#11-4-20ma-interface)
12. [Data Logging System](#12-data-logging-system)
13. [Configuration Management](#13-configuration-management)
14. [Security System](#14-security-system)
15. [ESP32 Implementation Mapping](#15-esp32-implementation-mapping)
16. [Enhanced Features Recommendations](#16-enhanced-features-recommendations)

---

## 1. System Overview

### 1.1 Product Description

The Walchem WBL Series controllers provide conductivity control of boiler water and control of chemical feed. Two models are available:

| Model | Relay Configuration | Total Relays |
|-------|---------------------|--------------|
| WBL400 | 1 Feed/Auxiliary relay + 1 Blowdown relay | 2 |
| WBL410 | 4 Feed/Auxiliary relays + 1 Blowdown relay + 1 Alarm relay | 6 |

### 1.2 Core Functions

| Function | Description |
|----------|-------------|
| Conductivity Control | Continuous monitoring and setpoint-based blowdown control |
| Chemical Feed | Multiple modes for treatment chemical injection |
| Water Metering | Volume tracking via contactor or paddlewheel sensors |
| Data Logging | USB-based logging with CSV export (optional) |
| Configuration Management | Import/export settings via USB |

### 1.3 Chemical Feed Operating Modes

| Mode | Name | Description |
|------|------|-------------|
| A | Blowdown and Feed | Feed output follows blowdown output with optional lockout timer |
| B | Feed % of Blowdown | Feed time is percentage of accumulated blowdown time |
| C | Feed % of Time | Feed output on for percentage of programmable cycle |
| D | Water Contactor | Feed triggered by water meter contact closures |
| E | Paddlewheel | Feed triggered by paddlewheel flow meter pulses |

### 1.4 Sampling Modes

| Code | Mode | Description |
|------|------|-------------|
| C | Continuous | Conductivity monitored continuously |
| I | Intermittent | Periodic sampling at programmed intervals |
| T | Intermittent w/Timed Blowdown | Intermittent with fixed-duration blowdown |
| P | Intermittent w/Time Proportional | Blowdown time proportional to deviation from setpoint |

### 1.5 Certifications

- UL 61010-1:2012 3rd Ed.
- CSA C22.2 No. 61010-1:2012 3rd Ed.
- IEC 61010-1:2010 3rd Ed.
- EN 61010-1:2010 3rd Ed.
- IEC 61326-1:2005 (EMC)
- EN 61326-1:2006 (EMC)
- NEMA 4X Enclosure Rating

---

## 2. Hardware Specifications

### 2.1 Electrical Specifications

| Parameter | Specification |
|-----------|---------------|
| Input Power | 100-240 VAC, 50/60 Hz, 8A max |
| Power Fuse (F1) | 5 x 20 mm, 1.0A, 250V |
| Relay Fuse (F2) | 5 x 20 mm, 6A, 250V |
| Relay Rating | 6A resistive, 1/8 HP |
| Maximum Total Relay Current | 6A (all relays combined) |

### 2.2 Measurement Performance

| Parameter | Specification |
|-----------|---------------|
| Conductivity Range | 0 - 10,000 µS/cm |
| Conductivity Resolution | 1 µS/cm |
| Conductivity Accuracy | 10 - 10,000 µS/cm: ±1% of reading |
|                        | 0 - 10 µS/cm: ±20% of reading |
| Temperature Range | 32 - 401°F (0 - 205°C) |
| Temperature Resolution | 0.1°C |
| Temperature Accuracy | ±1% of reading |

### 2.3 Electrode Specifications

| Parameter | Specification |
|-----------|---------------|
| Cell Constant | 1.0 |
| Temperature Sensor | Pt1000 RTD |
| Mounting | ¾" NPTF |
| Pressure Rating | 250 psi at 401°F (17.2 bar at 205°C) |
| Material | 316 SS and PEEK |
| Maximum Cable Length | 250 ft (25 ft recommended) |
| Standard Cable Length | 10 ft |

### 2.4 4-20mA Output Specifications (Optional)

| Parameter | Specification |
|-----------|---------------|
| Type | Internally powered, fully isolated |
| Maximum Load | 600 Ohm resistive |
| Resolution | 0.001% of span |
| Accuracy | ±1% of reading |

### 2.5 Environmental Specifications

| Parameter | Specification |
|-----------|---------------|
| Operating Temperature | 32 - 122°F (0 - 50°C) |
| Storage Temperature | -20 - 180°F (-29 - 80°C) |
| Enclosure Rating | NEMA 4X |
| Enclosure Material | Polycarbonate |

### 2.6 Physical Specifications

| Parameter | Specification |
|-----------|---------------|
| Dimensions | 8.5" x 6.5" x 5.5" |
| Display | 2 x 16 character backlit LCD |
| Keypad | 4 directional arrows + 4 function keys |
| USB Port | Front panel USB connector |

### 2.7 Mounting Clearances

| Side | Clearance |
|------|-----------|
| Top | 2" (50 mm) |
| Left | 8" (203 mm) |
| Right | 4" (102 mm) |
| Bottom | 7" (178 mm) |

---

## 3. Input/Output Requirements

### 3.1 Input Channels

| Input | Type | Description | Terminal Labels |
|-------|------|-------------|-----------------|
| Conductivity | Analog | 2-electrode measurement | COND (RED, BLK) |
| Temperature | Analog | Pt1000 RTD | T+, T- (GRN, WHT) |
| Flow Switch 1 | Digital | Dry contact closure | FLOW SW 1 (IN+, IN-) |
| Flow Meter 1 | Digital | Contact closure or Hall effect | FLOW MTR 1 (IN+, IN-) |
| Flow Meter 2 | Digital | Contact closure or Hall effect | FLOW MTR 2 (IN+, IN-) |

### 3.2 Output Channels - WBL400

| Output | Type | Terminals | Function |
|--------|------|-----------|----------|
| Blowdown | Relay (SPDT) | BLEED N.C., N.O. | Blowdown valve control |
| Feed | Relay (SPDT) | FEED N.C., N.O. | Chemical pump control |

### 3.3 Output Channels - WBL410

| Output | Type | Terminals | Function |
|--------|------|-----------|----------|
| Blowdown | Relay (SPDT) | BLOWDOWN N.C., N.O. | Blowdown valve control |
| Aux 1 | Relay (SPDT) | AUX 1 N.C., N.O. | Feed or Alarm |
| Aux 2 | Relay (SPDT) | AUX 2 N.C., N.O. | Feed or Alarm |
| Aux 3 | Relay (SPDT) | AUX 3 N.C., N.O. | Feed or Alarm |
| Aux 4 | Relay (SPDT) | AUX 4 N.C., N.O. | Feed or Alarm |
| Alarm | Relay (SPDT) | ALARM N.C., N.O. | Diagnostic alarm |

### 3.4 Flow Meter Input Types

| Type | Signal | Configuration |
|------|--------|---------------|
| Water Contactor | Contact closure | Dry contact, polarity not critical |
| Reed Switch | Contact closure | Dry contact, polarity not critical |
| Hall Effect Paddlewheel | Square wave | Requires +5V supply |

### 3.5 Wiring Requirements

| Signal | Cable Type | Notes |
|--------|-----------|-------|
| Conductivity Electrode | Shielded 24 AWG | Shield to ground at controller only |
| Flow Meter/Switch | Shielded twisted pair 22-26 AWG | Shield to ground stud |
| 4-20mA Output | Shielded twisted pair 22-26 AWG | Shield to ground stud |
| Low voltage signals | — | Minimum 6" separation from AC wiring |

---

## 4. Menu System Architecture

### 4.1 Main Menu Structure

```
Main Menu
├── Conductivity
│   ├── Calibrate
│   ├── Self Test
│   ├── Units (µS/cm or ppm)
│   ├── ppm C.F. (Conversion Factor)
│   ├── Sample Mode (C/I/T/P)
│   ├── Interval (H:MM) [Intermittent modes]
│   ├── Duration (MM:SS) [Intermittent modes]
│   ├── Hold Time (MM:SS) [Intermittent modes]
│   ├── Blow Time (H:MM) [Mode T]
│   ├── Prop Band [Mode P]
│   ├── Max P Time (H:MM) [Mode P]
│   └── RawCond (display only)
├── Temperature
│   ├── Calibrate / Man Temp
│   ├── Units (°F or °C)
│   └── Mode (Manual or Auto)
├── Blowdown
│   ├── Set Point
│   ├── Dead Band [Continuous mode only]
│   ├── Time Limit
│   ├── Reset Timer [After timeout]
│   ├── Control Dir (H/L)
│   └── H O A (Hand Off Auto)
├── Feed (WBL400) / Aux 1-4 (WBL410)
│   ├── Output Mode (Feed/Alarm) [WBL410 only]
│   ├── Chem Feed Mode (A/B/C/D/E)
│   ├── [Mode-specific settings]
│   └── H O A
├── WM1
│   ├── Reset Total
│   ├── Vol/Cont or K Factor
│   ├── Tot Units (Gal/Liters)
│   └── WM Type (C/P/N)
├── WM2
│   └── [Same as WM1]
├── Time
│   └── Set Clock
├── Alarm (WBL400 only)
│   ├── Alarm % Low
│   └── Alarm % High
├── 4-20mA [If installed]
│   ├── Set 4mA Pt
│   ├── Set 20mA Pt
│   └── Calibrate
├── Access Code
│   ├── Enable (Y/N)
│   └── New Value
├── Datalog [If USB feature enabled]
│   ├── Current Datalog
│   ├── Backup Datalog
│   ├── Copy Event Log
│   └── Copy Reset Log
├── Config [If USB feature enabled]
│   ├── Export Config
│   └── Import Config
└── Upgrade
    └── Start Upgrade
```

### 4.2 Keypad Functions

| Key | Function |
|-----|----------|
| NEXT | Scroll forward through menu items |
| PREV | Scroll backward through menu items |
| ENTER | Enter submenu or confirm value |
| EXIT | Return to previous menu level |
| UP/DOWN | Change values or scroll options |
| LEFT/RIGHT | Move cursor between digits |

### 4.3 Display Format

The summary display shows:
```
┌────────────────────────┐
│ [bargraph]S     1546   │  <- Conductivity with bar graph (S = setpoint)
│ Normal                 │  <- Status message
└────────────────────────┘
```

Bar graph features:
- Center (S) represents setpoint
- Each vertical bar = 1% deviation
- Small breaks at each 5%
- Range: ±20% from setpoint
- L/H indicators at extremes for low/high alarms

### 4.4 Status Messages

| Status | Description |
|--------|-------------|
| Normal | No abnormal conditions |
| Blowdown | Blowdown relay is active |
| Chem Feed | Feed relay is active |
| Sampling | In sampling phase (intermittent mode) |
| Waiting | Waiting for next sample interval |
| Holding | Sample trapped for hold time measurement |
| Sensor Error | Conductivity signal invalid |
| Temp Error | Temperature signal invalid |
| Blowdown Timeout | Blowdown exceeded time limit |
| Feed Timeout | Feed exceeded time limit |
| No Flow | Flow switch indicates no flow |
| Hi Alarm | High conductivity alarm active |
| Lo Alarm | Low conductivity alarm active |

---

## 5. Conductivity Measurement System

### 5.1 Measurement Parameters

| Parameter | Range | Default |
|-----------|-------|---------|
| Conductivity | 0 - 10,000 µS/cm | — |
| ppm Conversion Factor | 0.200 - 1.000 ppm/µS/cm | 0.666 |
| Calibration Range | -50% to +50% | 0% |

### 5.2 Units Selection

| Unit | Display Format |
|------|----------------|
| µS/cm | Direct conductivity reading |
| ppm | Conductivity × ppm C.F. |

**Note:** Changing units does NOT automatically convert setpoints. All setpoints must be manually adjusted.

### 5.3 Temperature Compensation

| Mode | Description |
|------|-------------|
| Automatic | Uses Pt1000 RTD for real-time compensation |
| Manual | User enters fixed temperature value |

If automatic mode is selected but temperature element is not detected, controller reverts to manual mode and displays "Temp Error".

### 5.4 Calibration Procedure

1. Enter Calibrate menu
2. Use reference (handheld meter or standard solution)
3. Adjust displayed value using arrow keys
4. Press ENTER to activate new calibration
5. Press EXIT to exit calibration mode

**Important:**
- Bleed output unaffected during calibration
- In intermittent sampling, bleed valve opens automatically when entering calibrate menu
- Maximum cumulative calibration adjustment: ±50%

### 5.5 Self Test Function

- Internally simulates conductivity sensor
- Expected reading: 1000 ±100 µS/cm
- If reading outside range: electronics fault
- If reading within range but calibration fails: sensor or wiring fault

### 5.6 RawCond Display

Displays temperature-compensated conductivity before user calibration is applied. Useful for diagnostics.

---

## 6. Sampling Mode System

### 6.1 Continuous Sampling (Mode C)

- Conductivity monitored continuously
- Blowdown activated when conductivity > setpoint
- Blowdown deactivated when conductivity < (setpoint - dead band)

### 6.2 Intermittent Sampling (Mode I)

**Parameters:**

| Parameter | Range | Description |
|-----------|-------|-------------|
| Interval | 5 min - 24:00 hrs | Time between samples |
| Duration | 1:00 - 59:59 min:sec | Length of each sample |
| Hold Time | 0:01 - 99:59 min:sec | Trapped sample measurement time |

**Sequence:**
1. Open blowdown valve (sample)
2. Sample for Duration time
3. If conductivity > setpoint, continue until below setpoint
4. Close valve, trap sample for Hold Time
5. Re-check conductivity
6. If still above setpoint → repeat from step 1
7. If below setpoint → wait for Interval

**Status Display:**
- `Cond Wait XX:XX` - Waiting for next sample
- `Cond Samp XX:XX` - Currently sampling
- `Cond Xtend XX:XX` - Extended sampling beyond duration
- `Cond BlwDn XX:XX` - Blowdown active
- `Cond Hold XX:XX` - Hold time countdown

### 6.3 Intermittent with Timed Blowdown (Mode T)

**Additional Parameter:**

| Parameter | Range | Description |
|-----------|-------|-------------|
| Blow Time | 1:00 - 8:20 hrs:min | Fixed blowdown duration |

**Sequence:**
1. Open blowdown valve at Interval
2. Sample for Duration time
3. Close valve, trap sample for Hold Time
4. If conductivity > setpoint → open valve for Blow Time
5. Re-check trapped sample
6. If still above setpoint → repeat blowdown cycle
7. If below setpoint → wait for Interval

### 6.4 Intermittent with Time Proportional Blowdown (Mode P)

**Additional Parameters:**

| Parameter | Range | Description |
|-----------|-------|-------------|
| Prop Band | 1 - 10,000 µS/cm | Deviation for 100% blowdown time |
| Max P Time | 1:00 - 8:20 hrs:min | Maximum blowdown time |

**Blowdown Time Calculation:**
```
deviation = measured_conductivity - setpoint
percentage = deviation / prop_band
blowdown_time = percentage × max_p_time

If deviation >= prop_band:
    blowdown_time = max_p_time
```

**Example:**
- Setpoint: 2000 µS/cm
- Prop Band: 200 µS/cm
- Max P Time: 10 minutes
- Measured: 2100 µS/cm
- Deviation: 100 µS/cm (50% of prop band)
- Blowdown Time: 5 minutes

---

## 7. Blowdown Control System

### 7.1 Blowdown Parameters

| Parameter | Range | Default |
|-----------|-------|---------|
| Set Point | 0 - 10,000 µS/cm | — |
| Dead Band | 5 - 500 µS/cm | — |
| Time Limit | 0:01 - 8:59 hrs:min (0 = unlimited) | 0 |
| Control Dir | H (High) / L (Low) | H |

### 7.2 Control Direction

| Setting | Action |
|---------|--------|
| High (H) | Output ON when conductivity > setpoint |
| Low (L) | Output ON when conductivity < setpoint |

### 7.3 Dead Band Operation (Continuous Mode Only)

```
ON threshold  = setpoint
OFF threshold = setpoint - dead_band

Example:
  Setpoint = 1500 µS/cm
  Dead Band = 200 µS/cm

  Blowdown ON when conductivity > 1500
  Blowdown OFF when conductivity < 1300
```

### 7.4 Time Limit and Reset

- If blowdown exceeds Time Limit → valve closes, "Blowdown Timeout" displayed
- Blowdown will not re-open until Reset Timer is manually set to "Y"
- Time Limit = 0 disables the timeout feature

### 7.5 HOA (Hand Off Auto) Control

| Mode | Behavior |
|------|----------|
| Hand | Output ON immediately (max 10 minutes, then returns to Auto) |
| Off | Output forced OFF indefinitely |
| Auto | Output controlled by conductivity vs setpoint |

### 7.6 Blowdown Control Algorithm (Continuous Mode)

```python
def blowdown_control():
    if hoa_mode == HAND:
        activate_blowdown()
        start_10min_timer()
        return

    if hoa_mode == OFF:
        deactivate_blowdown()
        return

    # Auto mode
    if no_flow_detected():
        deactivate_blowdown()
        display_status("NO FLOW")
        return

    if control_dir == HIGH:
        if conductivity > setpoint:
            if not blowdown_active:
                activate_blowdown()
                start_time_limit_timer()
        elif conductivity < (setpoint - dead_band):
            deactivate_blowdown()
    else:  # LOW control direction
        if conductivity < setpoint:
            if not blowdown_active:
                activate_blowdown()
                start_time_limit_timer()
        elif conductivity > (setpoint + dead_band):
            deactivate_blowdown()

    if time_limit_exceeded():
        deactivate_blowdown()
        set_timeout_flag()
        display_status("TIMEOUT")
```

---

## 8. Chemical Feed Control System

### 8.1 Feed Mode A: Blowdown and Feed with Lockout

**Parameters:**

| Parameter | Range | Description |
|-----------|-------|-------------|
| Lockout | 0:01 - 99:59 min:sec (0 = no lockout) | Maximum feed time |

**Operation:**
- Feed output mirrors blowdown output
- If Lockout > 0 and feed time exceeds lockout, feed turns OFF
- Feed remains OFF until blowdown turns OFF

```python
def mode_a_feed():
    if blowdown_active:
        if lockout_time == 0 or feed_time < lockout_time:
            activate_feed()
        else:
            deactivate_feed()  # Locked out
    else:
        deactivate_feed()
        reset_lockout()
```

### 8.2 Feed Mode B: Feed % of Blowdown

**Parameters:**

| Parameter | Range | Description |
|-----------|-------|-------------|
| % of Blowdown | 5 - 99% | Percentage of blowdown time |
| Max Time | 1:00 - 99:59 min:sec | Maximum feed time |

**Operation:**
- Accumulate blowdown time while blowdown is active
- When blowdown turns OFF, calculate feed time
- Feed time = blowdown_time × (% of Blowdown / 100)
- Feed time capped at Max Time

```python
def mode_b_feed():
    if blowdown_active:
        accumulated_blowdown_time += delta_time
        deactivate_feed()  # Don't feed during blowdown
    else:
        if accumulated_blowdown_time > 0:
            feed_time = accumulated_blowdown_time * (percent_of_blowdown / 100)
            feed_time = min(feed_time, max_time)
            if remaining_feed_time > 0:
                activate_feed()
                remaining_feed_time -= delta_time
            else:
                deactivate_feed()
                accumulated_blowdown_time = 0
```

### 8.3 Feed Mode C: Feed % of Time

**Parameters:**

| Parameter | Range | Description |
|-----------|-------|-------------|
| % of Time | 0.1 - 99% | Percentage of cycle for feed |
| Cycle Time | 10:00 - 59:59 min:sec | Total cycle duration |

**Operation:**
- Continuous cycling
- Feed ON for (Cycle Time × % of Time)
- Feed OFF for remainder of cycle

```python
def mode_c_feed():
    feed_on_time = cycle_time * (percent_of_time / 100)
    feed_off_time = cycle_time - feed_on_time

    if cycle_position < feed_on_time:
        activate_feed()
    else:
        deactivate_feed()

    cycle_position += delta_time
    if cycle_position >= cycle_time:
        cycle_position = 0
```

### 8.4 Feed Mode D: Water Contactor Input

**Parameters:**

| Parameter | Range | Description |
|-----------|-------|-------------|
| Time/Cont. | 0:01 - 59:59 min:sec | Feed time per contact |
| ÷ Contacts By | 1 - 100 | Contact divider |
| Assign Meter | 1, 2, Both | Which water meter to use |
| Time Limit | 1:00 - 99:59 min:sec | Maximum accumulated feed time |

**Operation:**
- Count contacts from water meter
- When contact count reaches divider, trigger feed
- Accumulate feed time (contacts can "bank" up)
- Time Limit prevents excessive accumulation

```python
def mode_d_feed():
    if new_contact_detected():
        contact_count += 1
        if contact_count >= divider:
            accumulated_feed_time += time_per_contact
            accumulated_feed_time = min(accumulated_feed_time, time_limit)
            contact_count = 0

    if accumulated_feed_time > 0:
        activate_feed()
        accumulated_feed_time -= delta_time
    else:
        deactivate_feed()
```

### 8.5 Feed Mode E: Paddlewheel Input

**Parameters:**

| Parameter | Range | Description |
|-----------|-------|-------------|
| Time/Vol | 0:01 - 59:59 min:sec | Feed time per volume unit |
| Vol to Init. | 1 - 9,999 | Volume to trigger feed |
| Assign Meter | 1, 2, Both | Which water meter to use |
| Time Limit | 1:00 - 99:59 min:sec | Maximum accumulated feed time |

**Operation:**
- Track volume from paddlewheel meter (using K factor)
- When volume reaches Vol to Init., trigger feed
- Feed for Time/Vol duration

```python
def mode_e_feed():
    accumulated_volume += pulses / k_factor

    if accumulated_volume >= vol_to_initiate:
        accumulated_feed_time += time_per_volume
        accumulated_feed_time = min(accumulated_feed_time, time_limit)
        accumulated_volume -= vol_to_initiate

    if accumulated_feed_time > 0:
        activate_feed()
        accumulated_feed_time -= delta_time
    else:
        deactivate_feed()
```

### 8.6 Auxiliary Output Configuration (WBL410 Only)

Each Aux output (1-4) can be configured as:

| Mode | Function |
|------|----------|
| FEED | Operates as chemical feed relay with modes A-E |
| ALARM | Operates as conductivity alarm relay |

---

## 9. Water Meter Integration

### 9.1 Water Meter Types

| Type | Code | Signal Type |
|------|------|-------------|
| Water Contactor | C | Dry contact closure |
| Paddlewheel | P | Hall effect (square wave) |
| Not Used | N | Disabled |

### 9.2 Water Contactor Configuration

| Parameter | Range | Description |
|-----------|-------|-------------|
| Gal/Cont. or Lit/Cont. | 1 - 500 | Volume per contact |

### 9.3 Paddlewheel Configuration

| Parameter | Range | Description |
|-----------|-------|-------------|
| K Factor | 0.01 - 999.99 pulses/vol | Pulses per unit volume |

### 9.4 Totalizer

| Parameter | Description |
|-----------|-------------|
| Tot Units | Gallons or Liters |
| Reset Total | Reset totalizer to zero |
| Maximum Count | 99,999,999 (auto-resets to 0) |

### 9.5 Volume Calculation

**Water Contactor:**
```
volume = contact_count × volume_per_contact
```

**Paddlewheel:**
```
volume = pulse_count / k_factor
```

---

## 10. Alarm System

### 10.1 WBL400 Alarm Configuration

Alarm function displays status only (no dedicated alarm relay).

| Parameter | Range | Description |
|-----------|-------|-------------|
| Alarm % Low | 1 - 50% (0 = disabled) | Low alarm threshold |
| Alarm % High | 1 - 50% (0 = disabled) | High alarm threshold |

**Threshold Calculation:**
```
low_alarm_point = setpoint - (setpoint × alarm_percent_low / 100)
high_alarm_point = setpoint + (setpoint × alarm_percent_high / 100)

Example:
  Setpoint = 1000 µS/cm
  % Low = 20%
  % High = 20%

  Low Alarm at: 800 µS/cm
  High Alarm at: 1200 µS/cm
```

### 10.2 WBL410 Alarm Configuration

Auxiliary outputs can be configured as ALARM mode with same % Low/High settings.

Dedicated diagnostic alarm relay activates on:
- Sensor Error
- Temp Error
- No Flow
- Blowdown Timeout
- Feed Timeout

### 10.3 Error Conditions

| Condition | Effect | Recovery |
|-----------|--------|----------|
| HIGH ALARM | Display warning, control continues | Conductivity drops below threshold |
| LOW ALARM | Display warning, control continues | Conductivity rises above threshold |
| TEMP ERROR | Stops conductivity control | Restore temperature signal |
| SENSOR ERROR | Stops conductivity control | Fix sensor/wiring |
| NO FLOW | Deactivates all outputs | Restore flow |
| BLOWDOWN TIMEOUT | Stops blowdown output | Manual Reset Timer |
| CAL FAIL | Rejects calibration | Clean electrode, retry |

---

## 11. 4-20mA Interface

### 11.1 Output Configuration

| Parameter | Range | Description |
|-----------|-------|-------------|
| Set 4mA Pt | 0 - 10,000 µS/cm | Conductivity for 4mA output |
| Set 20mA Pt | 0 - 10,000 µS/cm | Conductivity for 20mA output |

### 11.2 Output Calculation

```
span = set_20ma_point - set_4ma_point
percentage = (conductivity - set_4ma_point) / span
output_ma = 4 + (percentage × 16)
output_ma = clamp(output_ma, 4, 20)
```

### 11.3 Calibration Function

Provides fixed outputs for calibrating connected equipment:
- 4mA fixed output mode
- 20mA fixed output mode

---

## 12. Data Logging System

### 12.1 Log Types (USB Feature Required)

| Log | Contents | Behavior |
|-----|----------|----------|
| Current Datalog | Conductivity, Temperature, WM1 Total, WM2 Total | Erased after download |
| Backup Datalog | Same as Current | Never erased |
| Event Log | Relay states, flow switch, timestamps | Tens of thousands of events |
| Reset Log | Power loss/restore timestamps, reset cause | — |

### 12.2 Data Recording

| Parameter | Interval |
|-----------|----------|
| Measurement Data | Every 10 minutes |
| Event Data | On state change |
| Storage Duration | Minimum 60 days |

### 12.3 File Format

- CSV format (Microsoft Excel compatible)
- Filenames include serial number, date, time
- Example: `Datalog123456_20240115_143022.csv`

### 12.4 USB Operations

| Operation | Result |
|-----------|--------|
| Transfer Success | File copied successfully |
| Transfer Fail 1 | USB access error |

---

## 13. Configuration Management

### 13.1 Export Configuration

- Saves all setpoints to USB flash drive
- Filename: `UCF.ini`
- Can be renamed (must keep .ini extension)

### 13.2 Import Configuration

| Status | Description |
|--------|-------------|
| Import Success | Reboot required to apply |
| Import Failure | USB access problem |
| File Open Failed | No config file found |
| File Read Failed | File incomplete or empty |
| Invalid CFG File | Not a valid config file |
| Invalid Model | Config file for different model |
| Wrong SW Version | Incompatible software version |
| Corrupt CFG File | Checksum failed |
| Wrong File Size | File size incorrect |

---

## 14. Security System

### 14.1 Access Code Configuration

| Parameter | Range | Default |
|-----------|-------|---------|
| Enable | Y/N | N (disabled) |
| New Value | 0 - 9999 | 1995 |

### 14.2 Access Code Behavior

| State | Can View Settings | Can Change Settings |
|-------|-------------------|---------------------|
| Disabled | Yes | Yes |
| Enabled, not entered | Yes | No |
| Enabled, entered correctly | Yes | Yes (10 min timeout) |

### 14.3 Access Code Recovery

If code is forgotten:
1. Turn off power
2. Wait 10 seconds
3. Press and hold UP + DOWN arrows
4. Turn on power while holding keys
5. Read displayed access code
6. Release keys

---

## 15. ESP32 Implementation Mapping

### 15.1 Recommended GPIO Assignments

| Function | GPIO | Notes |
|----------|------|-------|
| **Analog Inputs** | | |
| Conductivity | GPIO34 (ADC1_CH6) | With signal conditioning circuit |
| Temperature (Pt1000) | GPIO35 (ADC1_CH7) | Requires reference resistor bridge |
| **Digital Inputs** | | |
| Flow Switch | GPIO27 | Internal pull-up |
| Water Meter 1 | GPIO26 | Interrupt-capable |
| Water Meter 2 | GPIO25 | Interrupt-capable |
| **Relay Outputs** | | |
| Blowdown | GPIO32 | Via relay driver |
| Aux 1 / Feed | GPIO33 | Via relay driver |
| Aux 2 | GPIO23 | Via relay driver (WBL410) |
| Aux 3 | GPIO22 | Via relay driver (WBL410) |
| Aux 4 | GPIO21 | Via relay driver (WBL410) |
| Alarm | GPIO19 | Via relay driver (WBL410) |
| **4-20mA Output** | | |
| DAC Output | GPIO25 (DAC1) | Or external DAC via I2C |
| **User Interface** | | |
| LCD SDA | GPIO21 | I2C LCD module |
| LCD SCL | GPIO22 | I2C LCD module |
| Keypad Row 1-4 | GPIO13-16 | Matrix keypad |
| Keypad Col 1-2 | GPIO17-18 | Matrix keypad |
| **USB/Storage** | | |
| USB D+ | GPIO20 | USB OTG (ESP32-S2/S3) |
| USB D- | GPIO19 | USB OTG (ESP32-S2/S3) |
| SD Card (Alt) | SPI GPIOs | For data logging |
| **Status LEDs** | | |
| Blowdown LED | GPIO4 | |
| Feed/Aux LEDs | GPIO5, 12, 13, 14, 15 | |
| Alarm LED | GPIO2 | |
| USB LED | GPIO0 | |

### 15.2 Recommended Hardware Components

| Component | Specification | Purpose |
|-----------|---------------|---------|
| ESP32-S3-WROOM-1 | Dual-core, USB OTG | Main controller |
| ADS1115 | 16-bit ADC, I2C | Precision conductivity measurement |
| MCP4725 | 12-bit DAC, I2C | 4-20mA output generation |
| PCF8574 | I2C I/O expander | Additional GPIO for relays |
| LCD2004 | 20x4 I2C LCD | Display (upgrade from 2x16) |
| ULN2803 | Darlington array | Relay drivers |
| Pt1000 Interface | Wheatstone bridge + amp | Temperature measurement |
| Optocouplers | PC817 or similar | Input isolation |

### 15.3 FreeRTOS Task Architecture

```c
// Task priorities (higher number = higher priority)
#define PRIORITY_SAFETY       5  // Safety monitoring
#define PRIORITY_MEASUREMENT  4  // Conductivity/temp reading
#define PRIORITY_CONTROL      3  // Blowdown/feed control
#define PRIORITY_LOGGING      2  // Data logging
#define PRIORITY_UI           1  // Display/keypad

// Recommended tasks
xTaskCreate(safety_task,       "Safety",      2048, NULL, PRIORITY_SAFETY, NULL);
xTaskCreate(measurement_task,  "Measure",     4096, NULL, PRIORITY_MEASUREMENT, NULL);
xTaskCreate(control_task,      "Control",     4096, NULL, PRIORITY_CONTROL, NULL);
xTaskCreate(water_meter_task,  "WaterMeter",  2048, NULL, PRIORITY_MEASUREMENT, NULL);
xTaskCreate(logging_task,      "Logging",     4096, NULL, PRIORITY_LOGGING, NULL);
xTaskCreate(ui_task,           "UI",          4096, NULL, PRIORITY_UI, NULL);
```

### 15.4 Data Structures

```c
// Conductivity configuration
typedef struct {
    uint16_t range_max;           // 10000 µS/cm
    float ppm_conversion_factor;  // 0.200 - 1.000
    int8_t calibration_percent;   // -50 to +50
    uint8_t units;                // US_CM or PPM
    uint8_t sample_mode;          // CONTINUOUS, INTERMITTENT, TIMED_BLOW, TIME_PROP
    uint16_t interval_minutes;    // 5 - 1440
    uint16_t duration_seconds;    // 60 - 3599
    uint16_t hold_time_seconds;   // 1 - 5999
    uint16_t blow_time_minutes;   // 1 - 500 (Mode T)
    uint16_t prop_band;           // 1 - 10000 (Mode P)
    uint16_t max_p_time_minutes;  // 1 - 500 (Mode P)
} conductivity_config_t;

// Blowdown configuration
typedef struct {
    uint16_t setpoint;            // 0 - 10000 µS/cm
    uint16_t dead_band;           // 5 - 500 µS/cm
    uint16_t time_limit_minutes;  // 0 - 539
    uint8_t control_direction;    // HIGH or LOW
    uint8_t hoa_mode;             // HAND, OFF, AUTO
    bool timeout_flag;
} blowdown_config_t;

// Feed configuration
typedef struct {
    uint8_t mode;                 // MODE_A through MODE_E
    uint16_t lockout_seconds;     // Mode A: 0 - 5999
    uint8_t percent_of_blowdown;  // Mode B: 5 - 99
    uint16_t max_time_seconds;    // Mode B: 60 - 5999
    uint8_t percent_of_time;      // Mode C: 1 - 990 (0.1% units)
    uint16_t cycle_time_seconds;  // Mode C: 600 - 3599
    uint16_t time_per_contact;    // Mode D: 1 - 3599
    uint8_t contact_divider;      // Mode D: 1 - 100
    uint16_t time_per_volume;     // Mode E: 1 - 3599
    uint16_t volume_to_initiate;  // Mode E: 1 - 9999
    uint8_t assigned_meter;       // WM1, WM2, BOTH
    uint16_t time_limit_seconds;  // Mode D/E: 60 - 5999
    uint8_t hoa_mode;             // HAND, OFF, AUTO
} feed_config_t;

// Water meter configuration
typedef struct {
    uint8_t type;                 // CONTACTOR, PADDLEWHEEL, NOT_USED
    uint16_t volume_per_contact;  // 1 - 500
    float k_factor;               // 0.01 - 999.99
    uint8_t units;                // GALLONS or LITERS
    uint32_t totalizer;           // 0 - 99999999
} water_meter_config_t;

// Alarm configuration
typedef struct {
    uint8_t percent_low;          // 0 - 50 (0 = disabled)
    uint8_t percent_high;         // 0 - 50 (0 = disabled)
} alarm_config_t;

// 4-20mA configuration
typedef struct {
    uint16_t point_4ma;           // 0 - 10000
    uint16_t point_20ma;          // 0 - 10000
} analog_output_config_t;
```

### 15.5 NVS Storage Keys

```c
// NVS namespace: "wbl_config"
const char* NVS_KEY_COND_CONFIG    = "cond_cfg";
const char* NVS_KEY_BLOWDOWN_CFG   = "blowdn_cfg";
const char* NVS_KEY_FEED1_CFG      = "feed1_cfg";
const char* NVS_KEY_FEED2_CFG      = "feed2_cfg";
const char* NVS_KEY_FEED3_CFG      = "feed3_cfg";
const char* NVS_KEY_FEED4_CFG      = "feed4_cfg";
const char* NVS_KEY_WM1_CFG        = "wm1_cfg";
const char* NVS_KEY_WM2_CFG        = "wm2_cfg";
const char* NVS_KEY_ALARM_CFG      = "alarm_cfg";
const char* NVS_KEY_ANALOG_OUT_CFG = "analog_cfg";
const char* NVS_KEY_ACCESS_CODE    = "access_code";
const char* NVS_KEY_ACCESS_ENABLED = "access_en";

// NVS namespace: "wbl_totals"
const char* NVS_KEY_WM1_TOTAL      = "wm1_total";
const char* NVS_KEY_WM2_TOTAL      = "wm2_total";
```

---

## 16. Enhanced Features Recommendations

### 16.1 Connectivity Enhancements

| Feature | Priority | Description |
|---------|----------|-------------|
| WiFi Connectivity | High | Remote monitoring and configuration |
| Web Dashboard | High | Browser-based real-time monitoring |
| MQTT Support | Medium | Integration with building management systems |
| Modbus TCP/RTU | Medium | Industrial protocol compatibility |
| Email/SMS Alerts | Medium | Remote alarm notification |
| OTA Updates | High | Wireless firmware updates |
| REST API | Medium | Integration with third-party systems |

### 16.2 Data Logging Enhancements

| Feature | Priority | Description |
|---------|----------|-------------|
| SD Card Logging | High | Expanded local storage |
| Cloud Data Sync | Medium | Automatic backup to cloud services |
| Configurable Intervals | Medium | User-selectable logging frequency |
| Extended History | Low | Years of data vs 60 days |
| Data Export Formats | Low | JSON, XML in addition to CSV |

### 16.3 Advanced Control Features

| Feature | Priority | Description |
|---------|----------|-------------|
| PID Control | Medium | Proportional blowdown control |
| Trend Analysis | Medium | Predictive maintenance alerts |
| Multi-Boiler Support | Low | Coordinate multiple boiler systems |
| Auto-Tuning | Low | Automatic setpoint optimization |
| Feed Scheduling | Medium | Time-of-day based feed control |

### 16.4 User Interface Enhancements

| Feature | Priority | Description |
|---------|----------|-------------|
| Touchscreen Display | Medium | Modern 3.5"+ color LCD |
| Graphical Trends | Medium | Real-time conductivity graphs |
| Mobile App | Low | iOS/Android companion app |
| Multi-Language | Low | Localization support |
| Quick Setup Wizard | Medium | Guided initial configuration |

### 16.5 Sensor Enhancements

| Feature | Priority | Description |
|---------|----------|-------------|
| Multiple Conductivity Ranges | Medium | Auto-ranging for different applications |
| pH Input | High | Combined conductivity/pH control |
| ORP Input | Medium | Oxidation-reduction potential monitoring |
| Pressure Sensor | Low | Boiler pressure monitoring |
| Flow Rate Display | Medium | Real-time GPM/LPM display |

### 16.6 Safety Enhancements

| Feature | Priority | Description |
|---------|----------|-------------|
| Watchdog Timer | High | System health monitoring |
| Redundant Sensors | Low | Backup conductivity measurement |
| Power Failure Recovery | High | Graceful shutdown/restart |
| Audit Trail | Medium | Log all configuration changes |
| User Authentication | Medium | Role-based access control |

### 16.7 Maintenance Features

| Feature | Priority | Description |
|---------|----------|-------------|
| Sensor Diagnostics | High | Automated electrode health check |
| Calibration Reminders | Medium | Scheduled maintenance alerts |
| Runtime Counters | Medium | Pump/valve operating hours |
| Chemical Usage Tracking | Medium | Estimate chemical consumption |
| Service Mode | Medium | Bypass controls for maintenance |

### 16.8 Feature Priority Matrix

```
                    │ Implementation │ User    │ Overall │
Feature             │ Complexity     │ Value   │ Priority│
────────────────────┼────────────────┼─────────┼─────────┤
WiFi Connectivity   │ Medium         │ High    │ HIGH    │
Web Dashboard       │ Medium         │ High    │ HIGH    │
SD Card Logging     │ Low            │ High    │ HIGH    │
OTA Updates         │ Medium         │ High    │ HIGH    │
Watchdog Timer      │ Low            │ High    │ HIGH    │
Power Recovery      │ Low            │ High    │ HIGH    │
pH Input            │ Medium         │ High    │ HIGH    │
Sensor Diagnostics  │ Medium         │ High    │ HIGH    │
MQTT Support        │ Medium         │ Medium  │ MEDIUM  │
Modbus Support      │ Medium         │ Medium  │ MEDIUM  │
Email/SMS Alerts    │ Medium         │ Medium  │ MEDIUM  │
PID Control         │ High           │ Medium  │ MEDIUM  │
Trend Analysis      │ High           │ Medium  │ MEDIUM  │
Touchscreen Display │ Medium         │ Medium  │ MEDIUM  │
Feed Scheduling     │ Low            │ Medium  │ MEDIUM  │
Calibration Remind  │ Low            │ Medium  │ MEDIUM  │
Cloud Data Sync     │ High           │ Medium  │ LOW     │
Multi-Boiler        │ High           │ Low     │ LOW     │
Auto-Tuning         │ High           │ Low     │ LOW     │
Mobile App          │ High           │ Medium  │ LOW     │
```

---

## Appendix A: Parameter Limits Summary

| Parameter | Min | Max | Units |
|-----------|-----|-----|-------|
| **Conductivity Menu** | | | |
| PPM Conversion Factor | 0.200 | 1.000 | ppm/µS/cm |
| Interval Time | 5 | 1440 | minutes |
| Duration Time | 1 | 59:59 | min:sec |
| Calibration Range | -50 | +50 | % |
| Hold Time | 1 | 99:59 | min:sec |
| Blow Time | 1 | 500 | minutes |
| Prop Band | 1 | 10,000 | µS/cm |
| Max P Time | 1 | 500 | minutes |
| **Blowdown Menu** | | | |
| Set Point | 0 | 10,000 | µS/cm |
| Dead Band | 5 | 500 | µS/cm |
| Time Limit | 1 | 539 | minutes |
| **Feed Menu** | | | |
| Lockout (Mode A) | 1 | 99:59 | min:sec |
| % of Blowdown (Mode B) | 5 | 99 | % |
| Max Time (Mode B) | 1 | 99:59 | min:sec |
| % of Time (Mode C) | 0.1 | 99 | % |
| Cycle Time (Mode C) | 10 | 59:59 | min:sec |
| Time/Contact (Mode D) | 1 | 59:59 | min:sec |
| ÷ Contacts By (Mode D) | 1 | 100 | contacts |
| Time Limit (Mode D/E) | 1 | 99:59 | min:sec |
| Time/Vol (Mode E) | 1 | 59:59 | min:sec |
| Vol to Initiate (Mode E) | 1 | 9,999 | units |
| **Water Meter Menu** | | | |
| Volume per Contact | 1 | 500 | gal or L |
| K Factor | 0.01 | 999.99 | pulses/vol |
| **4-20mA Menu** | | | |
| 4mA/20mA Settings | 0 | 10,000 | µS/cm |
| **Access Code Menu** | | | |
| Code Value | 0 | 9999 | — |
| **Alarm Menu** | | | |
| High/Low % | 1 | 50 | % |

---

## Appendix B: Comparison with Lakewood 1575e

| Feature | Walchem WBL400/410 | Lakewood 1575e |
|---------|-------------------|----------------|
| Conductivity Range | 0-10,000 µS/cm | 50-10,000 µS/cm |
| Relays (Base) | 2 (WBL400) | 4 |
| Relays (Extended) | 6 (WBL410) | 4 |
| Water Meter Inputs | 2 | 2 |
| Display | 2x16 LCD | 128x64 LCD |
| Feed Modes | 5 (A-E) | 5 (Setpoint, Volume, Percent, Schedule, Alarm) |
| Sampling Modes | 4 (C/I/T/P) | 2 (Continuous, Intermittent) |
| Data Logging | Optional USB | N/A |
| Temperature Range | 0-205°C | 0-60°C (ambient) |
| Electrode Pressure | 250 psi | 600 psi (boiler sensor) |
| USB Port | Yes | No |
| Config Import/Export | Yes (USB) | No |

---

*Document generated for ESP32 controller development*
*Source: Walchem WBL400/410 Instruction Manual (180325.K, March 2014)*

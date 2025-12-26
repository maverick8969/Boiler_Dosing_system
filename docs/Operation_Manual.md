# Columbia CT-6 Boiler Dosing Controller
## Operation Manual

**Model:** ESP32-BDC-001
**Firmware Version:** 1.0.0
**Boiler:** Columbia CT-6 (16 gallon capacity)
**Feedwater Tank:** 25 gallons

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Hardware Components](#hardware-components)
3. [Control Modes](#control-modes)
4. [Display & Navigation](#display--navigation)
5. [Web Interface](#web-interface)
6. [Chemical Dosing](#chemical-dosing)
7. [Blowdown Control](#blowdown-control)
8. [Alarms & Troubleshooting](#alarms--troubleshooting)
9. [Maintenance](#maintenance)
10. [Safety Information](#safety-information)

---

## System Overview

The Boiler Dosing Controller is an ESP32-based system that manages chemical treatment for the Columbia CT-6 steam boiler. It implements fuzzy logic control for intelligent dosing based on water chemistry test results.

### Key Features

| Feature | Description |
|---------|-------------|
| **Fuzzy Logic Control** | Intelligent multi-parameter chemical dosing |
| **Manual Test Input** | Enter lab test results via web or LCD |
| **Three Chemical Pumps** | H₂SO₃ (acid), NaOH (caustic), Amine/Sulfite |
| **Water Metering** | 1 pulse/gallon makeup water tracking |
| **Blowdown Recommendation** | TDS-based blowdown guidance (manual valve) |
| **Remote Monitoring** | WiFi connectivity with TimescaleDB/Grafana |
| **Multiple Feed Modes** | A through F plus scheduled dosing |

### System Parameters

```
Boiler Capacity:        16 gallons
Feedwater Tank:         25 gallons
Water Meter:            1 pulse per gallon
Operating Pressure:     15 PSI (low pressure)
Temperature Sensor:     Pt1000 RTD
Conductivity Sensor:    Sensorex CS675HTTC
```

---

## Hardware Components

### Front Panel

```
┌─────────────────────────────────────────────────────────┐
│  ┌──────────────────────────────────────────────────┐  │
│  │              20x4 LCD DISPLAY                     │  │
│  │  Line 1: Status / Alarm Messages                  │  │
│  │  Line 2: Temperature / Conductivity               │  │
│  │  Line 3: Pump Status (H2SO3 | NaOH | Amine)      │  │
│  │  Line 4: Water Total / Flow Rate                  │  │
│  └──────────────────────────────────────────────────┘  │
│                                                         │
│     [▲]  [▼]  [ENTER]  [MENU]     ● ● ● Status LEDs   │
│      UP  DOWN                      R G B              │
│                                                         │
│  ┌─────┐  ┌─────┐  ┌─────┐                             │
│  │PUMP1│  │PUMP2│  │PUMP3│    HOA Switches (optional)  │
│  │H2SO3│  │NaOH │  │Amine│                             │
│  └─────┘  └─────┘  └─────┘                             │
└─────────────────────────────────────────────────────────┘
```

### Status LED Indicators

| LED Color | State | Meaning |
|-----------|-------|---------|
| Green solid | Normal | System operating normally |
| Green blink | Active | Pump running or blowdown active |
| Yellow solid | Warning | Test data stale or low confidence |
| Yellow blink | Attention | Maintenance due |
| Red solid | Alarm | Active alarm condition |
| Red blink | Critical | Immediate attention required |

### Rear Panel Connections

| Connection | Description |
|------------|-------------|
| **POWER** | 24VDC input (2A minimum) |
| **COND PROBE** | 4-pin conductivity sensor |
| **TEMP PROBE** | Pt1000 RTD (2 or 3 wire) |
| **WATER METER** | Pulse input (dry contact) |
| **PUMP 1-3** | Stepper motor outputs (4-pin each) |
| **BLOWDOWN** | Relay output for valve (dry contact) |
| **ALARM** | Relay output for external alarm |
| **AUX IN 1-2** | Auxiliary inputs (drum level, etc.) |

---

## Control Modes

### Hand-Off-Auto (HOA) Modes

Each pump and the blowdown controller have independent HOA control:

| Mode | Description |
|------|-------------|
| **AUTO** | Automatic control based on fuzzy logic and feed mode |
| **OFF** | Output forced off, ignores all control signals |
| **HAND** | Output forced on (10-minute timeout for safety) |

### Chemical Feed Modes

| Mode | Name | Description |
|------|------|-------------|
| **Disabled** | None | Pump disabled |
| **A** | Blowdown Feed | Pump runs during blowdown cycle |
| **B** | % Blowdown | Pump runs X% of blowdown duration |
| **C** | % Time | Continuous duty cycle (X% on per cycle) |
| **D** | Water Contact | Pump triggered by water meter pulses |
| **E** | Paddlewheel | Pump triggered by flow volume |
| **F** | Fuzzy Logic | **Recommended** - Intelligent dosing based on chemistry |
| **S** | Scheduled | Time-of-day based feeding |

### Feed Mode F (Fuzzy Logic) - Recommended

Mode F combines fuzzy logic output with makeup water volume:

```
Dose (ml) = Water Volume (gal) × ml_per_gallon_at_100% × (Fuzzy Rate / 100%)
```

**Example:**
- 2 gallons of makeup water used
- Fuzzy logic says sulfite needs 60% dosing
- `ml_per_gallon_at_100pct` = 2.0 ml
- Dose = 2.0 × 2.0 × 0.60 = **2.4 ml sulfite**

---

## Display & Navigation

### Main Screen

```
┌────────────────────────────────┐
│ BOILER CONTROL    [AUTO]      │  ← Status and mode
│ 42.3°C     2847 µS/cm         │  ← Temperature and conductivity
│ H2SO3:-- NaOH:-- AMINE:RUN    │  ← Pump status
│ WM: 1,247 gal   1.2 GPM       │  ← Water meter totals
└────────────────────────────────┘
```

### Button Functions

| Button | Short Press | Long Press (3s) |
|--------|-------------|-----------------|
| **▲ UP** | Previous screen | Increase value (in menu) |
| **▼ DOWN** | Next screen | Decrease value (in menu) |
| **ENTER** | Select/Confirm | Access quick actions |
| **MENU** | Enter/Exit menu | Reset to main screen |

### Menu Structure

```
MAIN MENU
├── View Status
│   ├── Conductivity Details
│   ├── Temperature Details
│   ├── Pump Status
│   ├── Water Meter Totals
│   └── Alarm History
├── Manual Test Input         ← Enter lab results here
│   ├── TDS (ppm)
│   ├── Alkalinity (ppm)
│   ├── Sulfite (ppm)
│   ├── pH
│   └── Clear All Values
├── Fuzzy Control
│   ├── Enable/Disable
│   ├── View Outputs (%)
│   ├── Setpoints
│   ├── Deadbands
│   └── Advanced Settings
├── Pump Configuration
│   ├── Pump 1 (H2SO3)
│   ├── Pump 2 (NaOH)
│   ├── Pump 3 (Amine)
│   └── Prime All Pumps
├── Blowdown Settings
│   ├── Setpoint
│   ├── Deadband
│   ├── Time Limit
│   └── HOA Mode
├── Alarms
│   ├── Active Alarms
│   ├── Alarm History
│   └── Alarm Settings
├── Calibration
│   ├── Conductivity Cal
│   ├── Temperature Cal
│   └── Pump Calibration
├── Network
│   ├── WiFi Settings
│   ├── IP Address
│   └── TimescaleDB Config
└── System
    ├── Date/Time
    ├── Access Code
    ├── Factory Reset
    └── Firmware Info
```

---

## Web Interface

### Accessing the Web UI

1. Connect to the same WiFi network as the controller
2. Find the controller's IP address on the LCD (Network menu)
3. Open a web browser to `http://<controller-ip>/`

### Web UI Features

| Section | Function |
|---------|----------|
| **Sensor Reading** | Current temperature from Pt1000 |
| **Test Entry Form** | Enter TDS, Alkalinity, Sulfite, pH |
| **Control Recommendations** | Fuzzy logic outputs with progress bars |
| **Confidence Level** | HIGH, MEDIUM, or LOW based on valid inputs |
| **Target Ranges** | Reference table for acceptable values |

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Current readings and test ages |
| `/api/fuzzy` | GET | Fuzzy outputs and confidence |
| `/api/tests` | GET | Current manual test values |
| `/api/tests` | POST | Submit new test values |
| `/api/tests` | DELETE | Clear all manual values |

---

## Chemical Dosing

### Chemicals Used

| Pump | Chemical | Purpose | Target Range |
|------|----------|---------|--------------|
| **Pump 1** | H₂SO₃ (Hydrogen Sulfite) | Oxygen scavenging | 20-40 ppm sulfite residual |
| **Pump 2** | NaOH (Sodium Hydroxide) | pH/Alkalinity control | 200-400 ppm alkalinity |
| **Pump 3** | Amine | Condensate treatment | Per manufacturer spec |

### Dosing Parameters

Each pump has configurable parameters:

| Parameter | Description | Typical Value |
|-----------|-------------|---------------|
| `ml_per_gallon_at_100pct` | Max dose per gallon makeup | 1.0 - 5.0 ml |
| `steps_per_ml` | Pump calibration factor | 200 (adjust per pump) |
| `max_speed` | Stepper max speed | 1000 steps/sec |
| `time_limit_seconds` | Maximum run time | 300 sec (5 min) |

### Calibrating Pumps

1. Navigate to **Menu → Calibration → Pump Calibration**
2. Select pump to calibrate
3. Enter number of steps to run (e.g., 1000)
4. Measure actual volume dispensed (use graduated cylinder)
5. Calculate: `steps_per_ml = steps_run / ml_dispensed`
6. Enter new calibration value

---

## Blowdown Control

### IMPORTANT: Manual Valve Control

**The blowdown valve is controlled MANUALLY by the operator.**

The controller provides a **RECOMMENDATION** only (0-100%) based on TDS/conductivity readings. The operator must:

1. Observe the blowdown recommendation on the display or web UI
2. Verify boiler water level is adequate
3. Open the blowdown valve manually
4. Monitor the blowdown process
5. Close the valve when complete

### Blowdown Recommendation Levels

| Recommendation | Meaning | Suggested Action |
|----------------|---------|------------------|
| 0-20% | Low/Normal | No blowdown needed |
| 21-50% | Moderate | Brief blowdown (30-60 sec) |
| 51-75% | High | Extended blowdown (1-2 min) |
| 76-100% | Very High | Maximum blowdown, check for issues |

### Setpoint Configuration

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| **Setpoint** | 2500 µS/cm | 0-10000 | Target conductivity |
| **Deadband** | 50 µS/cm | 5-500 | Hysteresis band |
| **Time Limit** | 0 (unlimited) | 0-32340 sec | Max blowdown duration |

---

## Alarms & Troubleshooting

### Alarm Types

| Alarm | Cause | Action |
|-------|-------|--------|
| **HIGH CONDUCTIVITY** | TDS above high limit | Increase blowdown, check feed water |
| **LOW CONDUCTIVITY** | TDS below low limit | Check probe, verify readings |
| **BLOWDOWN TIMEOUT** | Blowdown exceeded time limit | Check valve, reduce blowdown |
| **FEED TIMEOUT** | Pump ran too long | Check chemical supply, calibration |
| **NO FLOW** | Flow switch not detecting flow | Check flow switch, boiler operation |
| **SENSOR ERROR** | Conductivity probe fault | Clean/replace probe |
| **TEMP ERROR** | Temperature sensor fault | Check RTD connections |
| **LOW CONFIDENCE** | Insufficient test data | Enter current test results |

### Troubleshooting Guide

#### No Display
1. Verify 24VDC power is connected
2. Check power LED on controller board
3. Verify LCD contrast setting
4. Check I2C cable connections

#### Incorrect Conductivity Reading
1. Check probe is submerged properly
2. Clean probe with appropriate solution
3. Verify probe cable connections
4. Perform calibration with standard solution

#### Pump Not Running
1. Check HOA mode is set to AUTO or HAND
2. Verify pump is enabled in configuration
3. Check stepper driver connections
4. Verify chemical supply is available
5. Check for alarm/lockout conditions

#### Web Interface Not Accessible
1. Verify WiFi is connected (check LCD)
2. Confirm IP address on Network menu
3. Check device is on same network
4. Try clearing browser cache
5. Restart controller if needed

---

## Maintenance

### Daily Tasks
- Observe conductivity and temperature readings
- Check pump status indicators
- Review blowdown recommendations
- Enter water test results (if testing daily)

### Weekly Tasks
- Enter water chemistry test results
- Check chemical tank levels
- Review alarm history
- Verify water meter readings match actual usage

### Monthly Tasks
- Clean conductivity probe
- Inspect pump tubing for wear
- Check all electrical connections
- Backup configuration if available
- Review and adjust setpoints if needed

### Quarterly Tasks
- Calibrate conductivity probe with standard solution
- Recalibrate pump output (steps per ml)
- Replace pump tubing if worn
- Clean LCD screen and enclosure
- Update firmware if available

### Annual Tasks
- Full system inspection
- Replace conductivity probe if degraded
- Replace temperature sensor if needed
- Review and update all setpoints
- Professional calibration verification

---

## Safety Information

### Warnings

⚠️ **ELECTRICAL HAZARD**
- Disconnect power before servicing
- 24VDC power can cause injury
- Keep electrical connections dry

⚠️ **CHEMICAL HAZARD**
- H₂SO₃, NaOH, and Amine are hazardous
- Wear appropriate PPE when handling
- Follow chemical manufacturer's SDS
- Ensure adequate ventilation

⚠️ **BURN HAZARD**
- Boiler and steam lines are hot
- Allow equipment to cool before servicing
- Use caution near blowdown discharge

⚠️ **PRESSURE HAZARD**
- Boiler operates under pressure
- Never blowdown when boiler is overpressured
- Follow boiler manufacturer's procedures

### Safety Interlocks

The controller includes these safety features:

1. **Manual Blowdown** - Operator must control valve manually
2. **HAND Mode Timeout** - 10-minute limit on forced operation
3. **Feed Time Limit** - Prevents pump runaway
4. **Chemical Lockout** - Post-blowdown delay prevents waste
5. **Alarm Outputs** - External notification of faults
6. **Low Confidence Warning** - Alerts when insufficient test data

### Emergency Procedures

**Controller Failure:**
1. Set all HOA switches to OFF
2. Manually control blowdown as needed
3. Contact service technician

**Chemical Spill:**
1. Stop all pumps immediately
2. Contain spill with appropriate materials
3. Neutralize per chemical SDS
4. Ventilate area

**High TDS Emergency:**
1. Perform manual blowdown
2. Check feedwater source
3. Verify chemical feed is working
4. Test water chemistry manually

---

## Specifications

### Electrical
| Parameter | Value |
|-----------|-------|
| Input Voltage | 24 VDC |
| Power Consumption | 20W typical, 40W max |
| Fuse Rating | 2A slow-blow |

### Environmental
| Parameter | Value |
|-----------|-------|
| Operating Temperature | 0°C to 50°C |
| Storage Temperature | -20°C to 70°C |
| Humidity | 0-95% non-condensing |
| Enclosure Rating | IP54 |

### Inputs/Outputs
| I/O | Specification |
|-----|---------------|
| Conductivity Input | 0-10000 µS/cm |
| Temperature Input | Pt1000 RTD, -50°C to +200°C |
| Water Meter Input | Dry contact, max 50 Hz |
| Stepper Outputs | 3x 2A per phase |
| Relay Outputs | 2x 5A @ 250VAC |

---

## Support

**Firmware Updates:** Check GitHub repository for latest releases

**Documentation:** See `/docs/` folder for additional guides:
- `Fuzzy_Logic_Control_System.md` - Detailed fuzzy logic documentation
- `Manual_Test_Input_Guide.md` - Test entry procedures
- `webui_preview.html` - Web interface preview

**Troubleshooting:** Review alarm codes and troubleshooting guide above

---

*Document Revision: 1.0*
*Last Updated: December 2024*

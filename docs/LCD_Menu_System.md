# CT-6 Boiler Controller - LCD Menu System

## Overview

The controller uses a 20x4 character LCD display with a rotary encoder for navigation. The menu system provides access to all configuration parameters, real-time monitoring, and manual controls.

---

## Navigation Controls

### Rotary Encoder Functions
| Action | Function |
|--------|----------|
| Rotate Clockwise | Move down / Increase value |
| Rotate Counter-clockwise | Move up / Decrease value |
| Short Press (< 500ms) | Select / Enter / Confirm |
| Long Press (> 1500ms) | Back / Cancel / Exit to parent menu |
| Double Press | Quick access to Main Status screen |

### Display Indicators
```
[*] = Selected item
[>] = Submenu available
[=] = Current value being edited
[!] = Alarm/Warning indicator
```

---

## Menu Tree Structure

```
┌─────────────────────────────────────────────────────────────────────┐
│                         MAIN STATUS SCREEN                          │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │ Cond: 2450 uS/cm    ←─ Conductivity reading                    │ │
│  │ Temp: 85.2°C  BD:ON ←─ Temperature, Blowdown status            │ │
│  │ Pumps: H+ N- A-     ←─ Pump status (+ = running)               │ │
│  │ WM1: 12345 gal      ←─ Water meter total                       │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                     [Press encoder to enter menu]                   │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                            MAIN MENU                                │
├─────────────────────────────────────────────────────────────────────┤
│  1. Process Monitor    >  │  Real-time readings and status         │
│  2. Conductivity       >  │  Sensor settings and calibration       │
│  3. Blowdown          >  │  Blowdown control settings              │
│  4. Chemical Pumps    >  │  Pump configuration (H2SO3/NaOH/Amine) │
│  5. Water Meters      >  │  Water meter settings                   │
│  6. Alarms            >  │  Alarm thresholds and history          │
│  7. Schedule          >  │  Feed scheduling                        │
│  8. Network           >  │  WiFi and data logging                  │
│  9. System            >  │  System settings and diagnostics        │
│  0. Manual Control    >  │  Manual override controls               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 1. Process Monitor Menu

Real-time display screens (rotate encoder to cycle through):

```
┌─────────────────────────────────────────────────────────────────────┐
│ 1.1 CONDUCTIVITY DETAIL                                             │
├─────────────────────────────────────────────────────────────────────┤
│  == Conductivity ==                                                 │
│  Raw:    2480.5 uS/cm   ←─ Before temperature compensation         │
│  Comp:   2465.2 uS/cm   ←─ After temperature compensation          │
│  Final:  2450.0 uS/cm   ←─ After user calibration                  │
│  [████████████S░░░░░░]  ←─ Bar graph (S = setpoint)                │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ 1.2 TEMPERATURE DETAIL                                              │
├─────────────────────────────────────────────────────────────────────┤
│  == Temperature ==                                                  │
│  Celsius:    85.2 °C                                               │
│  Fahrenheit: 185.4 °F                                              │
│  Sensor: OK  Mode: AUTO                                            │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ 1.3 BLOWDOWN STATUS                                                 │
├─────────────────────────────────────────────────────────────────────┤
│  === Blowdown ===                                                   │
│  State: BLOWING DOWN                                               │
│  Current: 00:02:45                                                 │
│  Today:   00:15:30                                                 │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ 1.4 PUMP STATUS                                                     │
├─────────────────────────────────────────────────────────────────────┤
│  === Pumps ===                                                      │
│  H2SO3: RUNNING  2.5 ml                                            │
│  NaOH:  IDLE     0.0 ml                                            │
│  Amine: IDLE     0.0 ml                                            │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ 1.5 WATER METER STATUS                                              │
├─────────────────────────────────────────────────────────────────────┤
│  === Water Meters ===                                               │
│  WM1: 12,345 gal  2.5 GPM                                          │
│  WM2: Disabled                                                     │
│  Flow Switch: OK                                                   │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ 1.6 NETWORK STATUS                                                  │
├─────────────────────────────────────────────────────────────────────┤
│  === Network ===                                                    │
│  WiFi: Connected                                                   │
│  RSSI: -45 dBm                                                     │
│  Server: OK  Pending: 0                                            │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 2. Conductivity Menu

```
┌─────────────────────────────────────────────────────────────────────┐
│ 2. CONDUCTIVITY SETTINGS                                            │
├─────────────────────────────────────────────────────────────────────┤
│  2.1 Calibrate           >  │  Single-point calibration            │
│  2.2 Self Test           >  │  Electronics verification            │
│  2.3 Units              [ ] │  uS/cm / ppm                         │
│  2.4 PPM Factor        [ ] │  0.200 - 1.000 (default 0.666)       │
│  2.5 Cell Constant     [ ] │  0.01 - 10.0 (default 1.0)           │
│  2.6 Sample Mode       [ ] │  C / I / T / P                        │
│  2.7 Sample Settings    >  │  Interval, Duration, Hold Time        │
│  2.8 Temp Compensation  >  │  Auto/Manual, Coefficient             │
│  2.9 Anti-Flash        [ ] │  OFF / ON (1-10)                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.1 Calibration Screen
```
┌────────────────────┐
│ == CALIBRATE ==    │
│                    │
│ Current: 2450 uS   │
│ Enter:   [____] uS │
│                    │
│ Rotate to adjust   │
│ Press to confirm   │
└────────────────────┘
```

### 2.6 Sample Mode Options
| Code | Mode | Description |
|------|------|-------------|
| C | Continuous | Monitor conductivity continuously |
| I | Intermittent | Periodic sampling at intervals |
| T | Timed Blowdown | Fixed blowdown duration after sample |
| P | Time Proportional | Blowdown time proportional to deviation |

### 2.7 Sample Settings (for modes I, T, P)
```
┌─────────────────────────────────────────────────────────────────────┐
│ 2.7 SAMPLE SETTINGS                                                 │
├─────────────────────────────────────────────────────────────────────┤
│  Interval:      [01:00:00]  │  Time between samples (HH:MM:SS)     │
│  Duration:      [00:05:00]  │  Sample duration (HH:MM:SS)          │
│  Hold Time:     [00:01:00]  │  Trapped sample time (HH:MM:SS)      │
│  Blow Time:     [00:10:00]  │  Fixed blow time - Mode T (HH:MM:SS) │
│  Prop Band:     [200] uS    │  Proportional band - Mode P          │
│  Max Prop Time: [00:10:00]  │  Maximum blow time - Mode P          │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.8 Temperature Compensation
```
┌─────────────────────────────────────────────────────────────────────┐
│ 2.8 TEMP COMPENSATION                                               │
├─────────────────────────────────────────────────────────────────────┤
│  Mode:          [AUTO]      │  AUTO / MANUAL                       │
│  Manual Temp:   [25.0] °C   │  Manual temperature value            │
│  Coefficient:   [2.0] %/°C  │  Temperature coefficient             │
│  Reference:     25.0 °C     │  Reference temperature (fixed)       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. Blowdown Menu

```
┌─────────────────────────────────────────────────────────────────────┐
│ 3. BLOWDOWN SETTINGS                                                │
├─────────────────────────────────────────────────────────────────────┤
│  3.1 Setpoint         [2500] uS  │  Target conductivity            │
│  3.2 Deadband         [50] uS    │  Hysteresis band                │
│  3.3 Control Dir      [HIGH]     │  HIGH / LOW                     │
│  3.4 Time Limit       [00:00]    │  Max blowdown (00:00 = off)     │
│  3.5 Ball Valve Delay [0] sec    │  Motorized valve delay (0-99)   │
│  3.6 HOA Mode         [AUTO]     │  HAND / OFF / AUTO              │
│  3.7 Reset Timeout    [ ]        │  Clear timeout alarm            │
│  3.8 Statistics       >          │  View blowdown statistics       │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.3 Control Direction
| Setting | Blowdown Activates When |
|---------|-------------------------|
| HIGH | Conductivity > Setpoint |
| LOW | Conductivity < Setpoint |

### 3.8 Statistics Screen
```
┌────────────────────┐
│ == BD STATS ==     │
│ Today:   00:45:30  │
│ Total:   125:30:00 │
│ Cycles:  45        │
│ [Reset Stats?]     │
└────────────────────┘
```

---

## 4. Chemical Pumps Menu

```
┌─────────────────────────────────────────────────────────────────────┐
│ 4. CHEMICAL PUMPS                                                   │
├─────────────────────────────────────────────────────────────────────┤
│  4.1 Pump 1 (H2SO3)   >  │  Hydrogen Sulfite pump settings        │
│  4.2 Pump 2 (NaOH)    >  │  Sodium Hydroxide pump settings        │
│  4.3 Pump 3 (Amine)   >  │  Amine pump settings                   │
│  4.4 Prime All        >  │  Prime all pumps                       │
│  4.5 Calibrate        >  │  Calibrate steps/ml                    │
└─────────────────────────────────────────────────────────────────────┘
```

### 4.x Individual Pump Settings
```
┌─────────────────────────────────────────────────────────────────────┐
│ 4.1 PUMP 1 - H2SO3                                                  │
├─────────────────────────────────────────────────────────────────────┤
│  Enabled:       [YES]           │  Enable/disable pump             │
│  Feed Mode:     [A]             │  A/B/C/D/E/S (see below)         │
│  HOA:           [AUTO]          │  HAND / OFF / AUTO               │
│  ─── Mode Settings ───          │                                  │
│  [Mode-specific parameters]     │                                  │
│  ─── Motor Settings ───         │                                  │
│  Steps/mL:      [200]           │  Calibration value               │
│  Max Speed:     [1000] stp/s    │  Maximum step rate               │
│  Acceleration:  [500] stp/s²   │  Acceleration rate               │
│  ─── Statistics ───             │                                  │
│  Runtime:       12:30:45        │  Total runtime                   │
│  Volume:        2,450 mL        │  Estimated volume dispensed      │
└─────────────────────────────────────────────────────────────────────┘
```

### Feed Mode Parameters

**Mode A: Blowdown + Feed**
```
│  Lockout Time:  [05:00] MM:SS   │  Max feed time per blowdown     │
```

**Mode B: % of Blowdown**
```
│  % of Blowdown: [25] %          │  Feed time = BD time × %        │
│  Max Time:      [10:00] MM:SS   │  Maximum feed duration          │
```

**Mode C: % of Time**
```
│  % of Time:     [10.0] %        │  Duty cycle percentage          │
│  Cycle Time:    [20:00] MM:SS   │  Total cycle duration           │
```

**Mode D: Water Contactor**
```
│  Time/Contact:  [00:30] MM:SS   │  Feed time per contact          │
│  ÷ Contacts:    [1]             │  Contact divider (1-100)        │
│  Meter:         [WM1]           │  WM1 / WM2 / BOTH               │
│  Time Limit:    [10:00] MM:SS   │  Max accumulated feed time      │
```

**Mode E: Paddlewheel**
```
│  Time/Volume:   [00:30] MM:SS   │  Feed time per volume unit      │
│  Volume Init:   [100] gal       │  Volume to trigger feed         │
│  Meter:         [WM1]           │  WM1 / WM2 / BOTH               │
│  Time Limit:    [10:00] MM:SS   │  Max accumulated feed time      │
```

**Mode S: Scheduled**
```
│  See Schedule Menu (7)          │  Time-based scheduled feeds     │
```

---

## 5. Water Meters Menu

```
┌─────────────────────────────────────────────────────────────────────┐
│ 5. WATER METERS                                                     │
├─────────────────────────────────────────────────────────────────────┤
│  5.1 Water Meter 1    >  │  WM1 configuration                      │
│  5.2 Water Meter 2    >  │  WM2 configuration                      │
│  5.3 Flow Switch      >  │  Flow switch settings                   │
└─────────────────────────────────────────────────────────────────────┘
```

### 5.x Water Meter Settings
```
┌─────────────────────────────────────────────────────────────────────┐
│ 5.1 WATER METER 1                                                   │
├─────────────────────────────────────────────────────────────────────┤
│  Type:          [CONTACTOR]     │  DISABLED / CONTACTOR / PADDLE   │
│  Units:         [GALLONS]       │  GALLONS / LITERS                │
│  ─── Contactor Mode ───         │                                  │
│  Vol/Contact:   [1] gal         │  Volume per contact (1-500)      │
│  ─── Paddlewheel Mode ───       │                                  │
│  K-Factor:      [1.00]          │  Pulses per unit (0.01-999.99)   │
│  ─── Totalizer ───              │                                  │
│  Total:         12,345 gal      │  Running total                   │
│  Flow Rate:     2.5 GPM         │  Current flow rate               │
│  [Reset Total]                  │  Reset totalizer to zero         │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 6. Alarms Menu

```
┌─────────────────────────────────────────────────────────────────────┐
│ 6. ALARMS                                                           │
├─────────────────────────────────────────────────────────────────────┤
│  6.1 Active Alarms    >  │  View current active alarms             │
│  6.2 Alarm History    >  │  View alarm log                         │
│  6.3 Conductivity     >  │  High/Low conductivity alarms           │
│  6.4 Timeouts         >  │  Blowdown/Feed timeout settings         │
│  6.5 Safety           >  │  Flow, sensor, drum level alarms        │
│  6.6 Clear All        >  │  Acknowledge all alarms                 │
└─────────────────────────────────────────────────────────────────────┘
```

### 6.3 Conductivity Alarms
```
┌─────────────────────────────────────────────────────────────────────┐
│ 6.3 CONDUCTIVITY ALARMS                                             │
├─────────────────────────────────────────────────────────────────────┤
│  Mode:          [ABSOLUTE]      │  ABSOLUTE / PERCENT              │
│  ─── Absolute Mode ───          │                                  │
│  High Alarm:    [5000] uS       │  0 = disabled                    │
│  Low Alarm:     [0] uS          │  0 = disabled                    │
│  ─── Percent Mode ───           │                                  │
│  High %:        [20] %          │  % above setpoint (0 = off)      │
│  Low %:         [0] %           │  % below setpoint (0 = off)      │
└─────────────────────────────────────────────────────────────────────┘
```

### 6.5 Safety Alarms
```
┌─────────────────────────────────────────────────────────────────────┐
│ 6.5 SAFETY ALARMS                                                   │
├─────────────────────────────────────────────────────────────────────┤
│  No Flow:       [ENABLED]       │  Alarm on flow switch open       │
│  Sensor Error:  [ENABLED]       │  Alarm on conductivity fault     │
│  Temp Error:    [ENABLED]       │  Alarm on temperature fault      │
│  Drum Level 1:  [ENABLED]       │  Alarm on aux input 1            │
│  Drum Level 2:  [ENABLED]       │  Alarm on aux input 2            │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 7. Schedule Menu

```
┌─────────────────────────────────────────────────────────────────────┐
│ 7. FEED SCHEDULE                                                    │
├─────────────────────────────────────────────────────────────────────┤
│  7.1 Schedule Mode    [WEEKDAY]  │  WEEKDAY / CYCLE                │
│  7.2 Cycle Length     [7] days   │  1-28 days (Cycle mode)         │
│  7.3 Entry 1          >          │  Schedule entry 1               │
│  7.4 Entry 2          >          │  Schedule entry 2               │
│  ...                             │                                  │
│  7.14 Entry 12        >          │  Schedule entry 12              │
│  7.15 View Schedule   >          │  List all scheduled feeds       │
└─────────────────────────────────────────────────────────────────────┘
```

### Schedule Entry Screen
```
┌─────────────────────────────────────────────────────────────────────┐
│ 7.3 SCHEDULE ENTRY 1                                                │
├─────────────────────────────────────────────────────────────────────┤
│  Enabled:       [YES]           │                                  │
│  Pump:          [H2SO3]         │  H2SO3 / NaOH / Amine            │
│  Day:           [MON]           │  Day of week or cycle day        │
│  Time:          [08:00]         │  Start time (24-hour)            │
│  Pre-Bleed SP:  [2000] uS       │  Pre-bleed setpoint (0 = skip)   │
│  Pre-Bleed Max: [05:00] MM:SS   │  Max pre-bleed duration          │
│  Feed Duration: [02:00] MM:SS   │  Chemical feed time              │
│  Lockout:       [10:00] MM:SS   │  Post-feed lockout time          │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 8. Network Menu

```
┌─────────────────────────────────────────────────────────────────────┐
│ 8. NETWORK                                                          │
├─────────────────────────────────────────────────────────────────────┤
│  8.1 WiFi Settings    >  │  SSID and password                      │
│  8.2 Server Settings  >  │  TimescaleDB host and port              │
│  8.3 Logging         >  │  Log interval and enable                │
│  8.4 Status          >  │  Connection status and diagnostics      │
│  8.5 AP Mode         >  │  Start access point for setup           │
│  8.6 Sync Time       >  │  Force NTP time sync                    │
└─────────────────────────────────────────────────────────────────────┘
```

### 8.1 WiFi Settings
```
┌─────────────────────────────────────────────────────────────────────┐
│ 8.1 WIFI SETTINGS                                                   │
├─────────────────────────────────────────────────────────────────────┤
│  SSID:          [MyNetwork____]  │  WiFi network name              │
│  Password:      [************]   │  WiFi password                  │
│  [Connect Now]                   │  Apply and connect              │
│  [Scan Networks]                 │  Scan for available networks    │
└─────────────────────────────────────────────────────────────────────┘
```

### 8.2 Server Settings
```
┌─────────────────────────────────────────────────────────────────────┐
│ 8.2 SERVER SETTINGS                                                 │
├─────────────────────────────────────────────────────────────────────┤
│  Host:          [192.168.1.100]  │  TimescaleDB server IP/hostname │
│  Port:          [8080]           │  HTTP API port                  │
│  [Test Connection]               │  Verify server connectivity     │
└─────────────────────────────────────────────────────────────────────┘
```

### 8.3 Logging Settings
```
┌─────────────────────────────────────────────────────────────────────┐
│ 8.3 LOGGING SETTINGS                                                │
├─────────────────────────────────────────────────────────────────────┤
│  Enabled:       [YES]            │  Enable data logging            │
│  Interval:      [10] seconds     │  Log interval (1-3600)          │
│  Pending:       0 records        │  Buffered records               │
│  [Force Upload]                  │  Upload buffered data now       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 9. System Menu

```
┌─────────────────────────────────────────────────────────────────────┐
│ 9. SYSTEM                                                           │
├─────────────────────────────────────────────────────────────────────┤
│  9.1 Date & Time      >  │  Set system clock                       │
│  9.2 Display          >  │  Backlight, brightness, contrast        │
│  9.3 Security         >  │  Access code settings                   │
│  9.4 Diagnostics      >  │  Self-test and diagnostics              │
│  9.5 Firmware         >  │  Version info and OTA update            │
│  9.6 Factory Reset    >  │  Reset all settings to defaults         │
│  9.7 Save Settings    >  │  Force save to NVS                      │
│  9.8 Export Config    >  │  Export configuration (future)          │
└─────────────────────────────────────────────────────────────────────┘
```

### 9.1 Date & Time
```
┌─────────────────────────────────────────────────────────────────────┐
│ 9.1 DATE & TIME                                                     │
├─────────────────────────────────────────────────────────────────────┤
│  Date:          [2024-01-15]     │  YYYY-MM-DD                     │
│  Time:          [14:30:00]       │  HH:MM:SS (24-hour)             │
│  Timezone:      [-5] hours       │  UTC offset                     │
│  DST:           [OFF]            │  Daylight saving time           │
│  [Sync from NTP]                 │  Get time from internet         │
└─────────────────────────────────────────────────────────────────────┘
```

### 9.2 Display Settings
```
┌─────────────────────────────────────────────────────────────────────┐
│ 9.2 DISPLAY SETTINGS                                                │
├─────────────────────────────────────────────────────────────────────┤
│  Backlight:     [ON]             │  ON / OFF / AUTO (timeout)      │
│  Auto Timeout:  [60] sec         │  Backlight auto-off time        │
│  LED Brightness:[128]            │  WS2812 brightness (0-255)      │
│  Units:         [uS/cm]          │  uS/cm / ppm display            │
│  Temp Units:    [°C]             │  °C / °F                        │
└─────────────────────────────────────────────────────────────────────┘
```

### 9.3 Security
```
┌─────────────────────────────────────────────────────────────────────┐
│ 9.3 SECURITY                                                        │
├─────────────────────────────────────────────────────────────────────┤
│  Access Code:   [DISABLED]       │  ENABLED / DISABLED             │
│  Current Code:  [2222]           │  4-digit PIN                    │
│  [Change Code]                   │  Set new access code            │
│  [Lock Now]                      │  Lock controller immediately    │
└─────────────────────────────────────────────────────────────────────┘
```

### 9.4 Diagnostics
```
┌─────────────────────────────────────────────────────────────────────┐
│ 9.4 DIAGNOSTICS                                                     │
├─────────────────────────────────────────────────────────────────────┤
│  [Conductivity Self-Test]        │  Test electronics               │
│  [Test Relays]                   │  Cycle all relay outputs        │
│  [Test Pumps]                    │  Run each pump briefly          │
│  [Test LEDs]                     │  Cycle LED colors               │
│  [View Raw ADC]                  │  Show raw sensor values         │
│  Free Memory:   125,432 bytes    │  Available heap memory          │
│  Uptime:        12:34:56         │  Time since boot                │
└─────────────────────────────────────────────────────────────────────┘
```

### 9.5 Firmware
```
┌─────────────────────────────────────────────────────────────────────┐
│ 9.5 FIRMWARE                                                        │
├─────────────────────────────────────────────────────────────────────┤
│  Version:       1.0.0                                              │
│  Build Date:    Jan 15 2024                                        │
│  Model:         CT6-ESP32-001                                      │
│  [Check for Updates]             │  Check OTA server               │
│  [Update Firmware]               │  Start OTA update               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 0. Manual Control Menu

```
┌─────────────────────────────────────────────────────────────────────┐
│ 0. MANUAL CONTROL                                                   │
├─────────────────────────────────────────────────────────────────────┤
│  0.1 Blowdown HOA     [AUTO]     │  HAND / OFF / AUTO              │
│  0.2 Pump 1 HOA       [AUTO]     │  HAND / OFF / AUTO              │
│  0.3 Pump 2 HOA       [AUTO]     │  HAND / OFF / AUTO              │
│  0.4 Pump 3 HOA       [AUTO]     │  HAND / OFF / AUTO              │
│  0.5 Prime Pump 1     >          │  Prime H2SO3 pump               │
│  0.6 Prime Pump 2     >          │  Prime NaOH pump                │
│  0.7 Prime Pump 3     >          │  Prime Amine pump               │
│  0.8 Force Blowdown   >          │  Manual blowdown trigger        │
└─────────────────────────────────────────────────────────────────────┘
```

**Note:** HAND mode automatically reverts to AUTO after 10 minutes.

---

## Persistent Storage System

All settings are automatically saved to ESP32 Non-Volatile Storage (NVS) when changed. The system uses:

### NVS Namespace: `boiler_cfg`

| Key | Data | Description |
|-----|------|-------------|
| `config` | Binary blob | Complete system configuration |
| `wm1_total` | uint32 | Water meter 1 totalizer |
| `wm2_total` | uint32 | Water meter 2 totalizer |
| `pump1_tot` | uint32 | Pump 1 total runtime |
| `pump2_tot` | uint32 | Pump 2 total runtime |
| `pump3_tot` | uint32 | Pump 3 total runtime |
| `blow_total` | uint32 | Total blowdown time |
| `last_cal` | uint32 | Last calibration timestamp |

### Auto-Save Triggers
- Settings are saved immediately when exiting edit mode
- Water meter totals saved every 5 minutes
- Pump statistics saved on shutdown/restart
- Configuration backed up before firmware update

### Factory Reset
Factory reset clears all NVS data and restores defaults:
- Access via System Menu → Factory Reset
- Requires confirmation (hold encoder for 5 seconds)
- WiFi credentials are also cleared

---

## Quick Reference Card

### Navigation
| Action | Result |
|--------|--------|
| Rotate CW | Down / Increase |
| Rotate CCW | Up / Decrease |
| Press | Select / Enter |
| Long Press | Back / Cancel |
| Double Press | Home screen |

### Status Indicators (LEDs)
| LED | Color | Meaning |
|-----|-------|---------|
| Power | Green | System OK |
| WiFi | Blue | Connected |
| WiFi | Yellow | AP Mode |
| Cond | Green/Yellow/Red | Relative to setpoint |
| Blowdown | Yellow | Valve open |
| Pumps | Cyan/Magenta/Yellow | Running |
| Alarm | Red (flashing) | Active alarm |

### Default Settings
| Parameter | Default |
|-----------|---------|
| Setpoint | 2500 µS/cm |
| Deadband | 50 µS/cm |
| High Alarm | 5000 µS/cm |
| Sample Mode | Continuous |
| Access Code | 2222 |
| Log Interval | 10 seconds |

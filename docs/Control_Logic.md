# Control Logic

## Overview

The Columbia CT-6 Boiler Dosing Controller runs on an ESP32-WROOM-32 and uses
FreeRTOS to schedule four cooperating tasks. This document maps the firmware's
execution lifecycle, state machines, decision logic, and key subroutines to
the source files in `firmware/esp32_boiler_controller/`.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         ESP32 (Dual-Core)                           │
│                                                                     │
│  Core 1                              Core 0                         │
│  ┌─────────────────────────┐         ┌─────────────────────────┐   │
│  │ taskControlLoop (pri 4) │         │ taskDisplayLoop (pri 2) │   │
│  │   100 ms period         │         │   200 ms period         │   │
│  │   blowdown, pumps,      │         │   LCD + WS2812 LEDs     │   │
│  │   fuzzy logic, alarms   │         └─────────────────────────┘   │
│  ├─────────────────────────┤         ┌─────────────────────────┐   │
│  │ taskMeasurementLoop     │         │ taskLoggingLoop (pri 1) │   │
│  │   (pri 3)  500 ms       │         │   1000 ms period        │   │
│  │   EZO-EC, MAX31865,     │         │   WiFi, HTTP POST to    │   │
│  │   water meter update    │         │   TimescaleDB           │   │
│  └─────────────────────────┘         └─────────────────────────┘   │
│                                                                     │
│  Arduino loop()  ← processInputs() (encoder polling, 10 ms yield)  │
│                                                                     │
│  ISR layer (IRAM)                                                   │
│  ├── Water meter pulse interrupt (GPIO34)                           │
│  └── Rotary encoder A/B edge interrupts (GPIO15/GPIO2)              │
└─────────────────────────────────────────────────────────────────────┘
```

### Source Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | `setup()`, `loop()`, FreeRTOS task bodies, config I/O, alarm logic |
| `include/config.h` | All `typedef` structs, enums, defaults, NVS keys, task parameters |
| `include/pin_definitions.h` | GPIO assignments, hardware constants |
| `include/conductivity.h` / `src/conductivity.cpp` | `ConductivitySensor` — EZO-EC UART + MAX31865 SPI |
| `include/blowdown.h` / `src/blowdown.cpp` | `BlowdownController` — valve state machine, ADS1115 feedback |
| `include/chemical_pump.h` / `src/chemical_pump.cpp` | `ChemicalPump`, `PumpManager` — A4988 stepper control, feed modes A–F |
| `include/water_meter.h` / `src/water_meter.cpp` | `WaterMeter`, `WaterMeterManager` — pulse counting, flow rate, NVS persistence |
| `include/fuzzy_logic.h` / `src/fuzzy_logic.cpp` | `FuzzyController` — Mamdani inference, membership functions, rule base |
| `include/display.h` / `src/display.cpp` | `Display` — LCD screens, WS2812 LEDs, bar graphs |
| `include/data_logger.h` / `src/data_logger.cpp` | `DataLogger` — WiFi, HTTP POST, buffered uploads, NTP sync |

---

## Execution Lifecycle

### 1. Boot Sequence (`setup()` — `main.cpp:81`)

```
Power on / Reset
       │
       ▼
Serial.begin(115200)
       │
       ▼
Wire.begin(SDA=21, SCL=22, 400 kHz)    ← shared I2C: LCD + ADS1115
       │
       ▼
loadConfiguration()                      ← NVS "boiler_cfg" → system_config_t
  ├── Config blob present + magic OK?  → use stored config
  └── Missing / corrupt?              → initializeDefaults() → saveConfiguration()
       │
       ▼
display.begin()                          ← LCD init + custom chars + LED strip
       │
       ▼
conductivitySensor.begin()               ← UART2 init (9600), MAX31865 SW-SPI init
  └── configure(systemConfig.conductivity)  ← K, TDS factor, output selection
       │
       ▼
pumpManager.begin()                      ← 3x AccelStepper init, ENABLE pin HIGH (disabled)
  └── configure(systemConfig.pumps)
       │
       ▼
waterMeterManager.begin()                ← GPIO34 interrupt attach
  └── configure + loadAllFromNVS()       ← restore totalizers
       │
       ▼
blowdownController.begin()              ← GPIO4 OUTPUT LOW, ADS1115 probe
  └── configure(blowdown) + setConductivityConfig(conductivity)
       │
       ▼
dataLogger.begin()                       ← WiFi connect (if SSID set), NTP sync
       │
       ▼
xTaskCreatePinnedToCore × 4             ← Control, Measurement, Display, Logging
       │
       ▼
loop() runs  ← processInputs() every 10 ms
```

### 2. Steady-State Task Schedule

| Task | Core | Period | Stack | Priority | What It Does |
|------|------|--------|-------|----------|--------------|
| **Control** | 1 | 100 ms | 4 KiB | 4 | Blowdown update, fuzzy evaluate, pump feed modes, pump stepper update, alarm check |
| **Measurement** | 1 | 500 ms | 6 KiB | 3 | EZO-EC read (with RT temp comp), water meter update, system state update |
| **Display** | 0 | 200 ms | 4 KiB | 2 | LCD screen draw, WS2812 LED update |
| **Logging** | 0 | 1000 ms | 8 KiB | 1 | WiFi reconnect, periodic `logSensorData()` at `log_interval_ms` |

### 3. `loop()` — Input Polling (`main.cpp:218`)

Runs on whichever core is free (default Core 1). Calls `processInputs()` then yields for 10 ms.

---

## Control Task Detail (`taskControlLoop` — `main.cpp:233`)

Each 100 ms tick executes the following pipeline:

```
┌─ updateFeedwaterPumpMonitor() ───────────────────────────────────┐
│   └── GPIO35 edge detect → cycle count, on-time, event logging  │
│                                                                   │
├─ Get conductivity from last Measurement reading ─────────────────┤
│                                                                   │
├─ blowdownController.update(conductivity) ────────────────────────┤
│   └── state machine: IDLE → VALVE_OPENING → BLOWING_DOWN →       │
│       VALVE_CLOSING → IDLE  (see Blowdown State Machine below)   │
│                                                                   │
├─ Get water contacts + volume from waterMeterManager ─────────────┤
│                                                                   │
├─ Build fuzzy_inputs_t (cond, temp, manual alkalinity/sulfite/pH) ┤
│                                                                   │
├─ fuzzyController.evaluate(inputs) → fuzzy_result_t ──────────────┤
│   └── Mamdani inference: fuzzify → rule evaluation → defuzzify   │
│                                                                   │
├─ Map fuzzy outputs to pump array:                                 │
│   rates[H2SO3] = acid_rate                                        │
│   rates[NaOH]  = caustic_rate                                     │
│   rates[Amine] = sulfite_rate                                     │
│                                                                   │
├─ pumpManager.processFeedModes(blowdown_active, bd_time,          │
│                                contacts, volume, rates)           │
│   └── per-pump: check HOA → dispatch feed mode A–F               │
│                                                                   │
├─ pumpManager.update()  ← runs AccelStepper step calculations ────┤
│                                                                   │
└─ checkAlarms()  ← bitmask comparison, rising/falling edge detect ┘
```

---

## Blowdown State Machine (`blowdown.h` / `blowdown.cpp`)

### States

| State | Meaning |
|-------|---------|
| `BD_STATE_IDLE` | Valve closed, waiting for trigger |
| `BD_STATE_VALVE_OPENING` | GPIO4 HIGH → relay energized → 20 mA → ball valve opening |
| `BD_STATE_BLOWING_DOWN` | Valve open confirmed (feedback > 19 mA), draining |
| `BD_STATE_VALVE_CLOSING` | GPIO4 LOW → relay de-energized → 4 mA → ball valve closing |
| `BD_STATE_SAMPLING` | Intermittent mode: sample intake period |
| `BD_STATE_HOLDING` | Intermittent mode: trapped sample stabilization |
| `BD_STATE_WAITING` | Intermittent mode: interval between cycles |
| `BD_STATE_TIMEOUT` | Blowdown exceeded `time_limit_seconds` |
| `BD_STATE_ERROR` | Valve fault (feedback < 3 mA) or other error |

### Continuous Mode Transition Diagram

```
                   cond > setpoint
        IDLE ────────────────────────► VALVE_OPENING
         ▲                                    │
         │                           feedback > 19 mA
         │                           (or ball_valve_delay)
         │                                    │
         │                                    ▼
   VALVE_CLOSING ◄───────────────── BLOWING_DOWN
         │        cond < (setpoint             │
         │          - deadband)                │
         │                              timeout?
    feedback < 5 mA                           │
    (or delay elapsed)                        ▼
         │                              BD_STATE_TIMEOUT
         ▼                                    │
        IDLE                           (requires manual
                                        resetTimeout())
```

### Intermittent Mode (I / T / P)

```
  IDLE → SAMPLING → HOLDING → {decide} → BLOWING_DOWN → VALVE_CLOSING → WAITING → IDLE
                                  │
                                  └── Mode T: fixed blow_time_seconds
                                  └── Mode P: proportional to (cond - setpoint)
                                  └── Mode I: blow if cond > setpoint
```

### Key Methods

| Method | Purpose | Inputs | Side Effects |
|--------|---------|--------|--------------|
| `update(conductivity, flow_ok)` | Main tick — runs state machine | float, bool | Drives GPIO4, reads ADS1115, updates `_status` |
| `processHOA()` | Checks HOA mode, forces open/close/auto | — | May override state machine |
| `processContinuousMode(cond)` | Continuous setpoint comparison | float | Transitions state |
| `processIntermittentMode(cond)` | I/T/P interval logic | float | Manages sample/hold/blow timers |
| `readFeedback()` | Reads ADS1115 CH0 → mA | — | Updates `_status.feedback_mA`, `position_confirmed`, `valve_fault` |
| `setRelayState(energize)` | Writes GPIO4 HIGH/LOW | bool | Drives SPDT relay coil |
| `startBallValve(opening)` | Begins valve transition | bool | Records `_valve_action_start`, sets `_valve_target_state` |
| `checkBallValveComplete()` | Checks feedback for position confirmation | — | Advances state on confirmation or timeout |
| `checkTimeout()` | Compares elapsed time against `time_limit_seconds` | — | Transitions to `BD_STATE_TIMEOUT` |

---

## Chemical Pump Feed Modes (`chemical_pump.h` / `chemical_pump.cpp`)

### Per-Pump Processing Flow

```
processFeedMode(blowdown_active, bd_time, contacts, volume, fuzzy_rate)
       │
       ▼
  check HOA mode
  ├── HOA_OFF  → stop(), return
  ├── HOA_HAND → start(), enforce 10-min timeout, return
  └── HOA_AUTO → continue to feed mode dispatch
       │
       ▼
  switch(config.feed_mode)
  ├── DISABLED         → stop()
  ├── A: Blowdown Feed → processModeA(blowdown_active)
  ├── B: % Blowdown    → processModeB(blowdown_active, bd_time)
  ├── C: % Time        → processModeC()
  ├── D: Water Contact  → processModeD(contacts)
  ├── E: Paddlewheel    → processModeE(volume)
  └── F: Fuzzy Logic    → processModeF(volume, fuzzy_rate)
```

### Feed Mode Details

| Mode | Trigger | Duration | Key Parameters |
|------|---------|----------|----------------|
| **A** | `blowdown_active` goes true | Runs while blowdown is active, up to `lockout_seconds` | `lockout_seconds` |
| **B** | Blowdown ends | `bd_time * percent_of_blowdown / 100` | `percent_of_blowdown`, `max_time_seconds` |
| **C** | Time-based duty cycle | `cycle_time * percent_of_time / 1000` on, rest off | `percent_of_time` (0.1% units), `cycle_time_seconds` |
| **D** | Water meter contact | `time_per_contact_ms` per N contacts | `time_per_contact_ms`, `contact_divider`, `assigned_meter` |
| **E** | Accumulated volume | `time_per_volume_ms` when volume >= `volume_to_initiate` | `time_per_volume_ms`, `volume_to_initiate` |
| **F** | Makeup water + fuzzy rate | `volume * ml_per_gallon_at_100pct * (fuzzy_rate / 100)` → steps via `steps_per_ml` | `ml_per_gallon_at_100pct`, `fuzzy_meter_select` |

### Stepper Motor Control

Each `ChemicalPump` wraps an `AccelStepper` instance:

- `start(duration_ms, volume_ml)` — sets target steps from volume or time, enables driver (GPIO13 LOW), begins stepping
- `update()` — calls `_stepper.run()` each tick, checks time/step limits, updates stats
- `stop()` — calls `_stepper.stop()`, disables driver (GPIO13 HIGH)
- Shared enable pin GPIO13 (active LOW) controls all three A4988 drivers

---

## Fuzzy Logic Inference (`fuzzy_logic.h` / `fuzzy_logic.cpp`)

### Pipeline

```
fuzzy_inputs_t (6 crisp values)
       │
       ▼
   ┌───────────┐
   │ Fuzzify   │  Per input variable: evaluate each membership function
   │           │  → float[6][7] input_membership degrees
   └─────┬─────┘
         │
         ▼
   ┌───────────┐
   │ Rule Eval │  For each of 64 rules:
   │           │    firing = t-norm(antecedent memberships)  [min or product]
   │           │    aggregate consequent output sets  [s-norm max]
   └─────┬─────┘
         │
         ▼
   ┌───────────┐
   │ Defuzzify │  Per output variable:
   │           │    centroid / bisector / MOM of aggregated set
   │           │    → crisp value 0–100%
   └─────┬─────┘
         │
         ▼
fuzzy_result_t
  .blowdown_rate    (0–100%)
  .caustic_rate     (0–100%)
  .sulfite_rate     (0–100%)
  .acid_rate        (0–100%)
  .active_rules     (count)
  .max_firing_strength
```

### Input Variables

| Index | Name | Source | Range |
|-------|------|--------|-------|
| 0 | TDS / Conductivity | EZO-EC via Measurement task | 0–10 000 uS/cm |
| 1 | Alkalinity | Manual entry (web / LCD) | 0–1000 ppm |
| 2 | Sulfite | Manual entry | 0–100 ppm |
| 3 | pH | Manual entry | 0–14 |
| 4 | Temperature | MAX31865 PT1000 | -50–200 C |
| 5 | Trend | Calculated (delta cond/min) | -500–+500 uS/min |

### Output Variables

| Index | Name | Maps To |
|-------|------|---------|
| 0 | Blowdown | Blowdown recommendation % |
| 1 | Caustic | `fuzzy_rates[PUMP_NAOH]` for feed mode F |
| 2 | Sulfite | `fuzzy_rates[PUMP_AMINE]` for feed mode F |
| 3 | Acid | `fuzzy_rates[PUMP_H2SO3]` for feed mode F |

### Membership Function Types

| Type | Parameters | Shape |
|------|-----------|-------|
| Triangular | a, b, c | Peak at b, zero at a and c |
| Trapezoidal | a, b, c, d | Flat top b–c, zero at a and d |
| Gaussian | center, sigma | Bell curve |
| Sigmoid Left | center, slope | Ramp down |
| Sigmoid Right | center, slope | Ramp up |
| Singleton | value | Single point |

---

## Measurement Task Detail (`taskMeasurementLoop` — `main.cpp:295`)

Every 500 ms:

```
conductivitySensor.read()
  ├── readTemperature()           ← MAX31865 SW-SPI: CS=16, MOSI=23, MISO=39, SCK=18
  │     └── _rtd.readRTD() → resistance → Callendar–Van Dusen → °C
  ├── Send RT,<temp> to EZO       ← UART2 TX=25, RX=36
  ├── Parse response: EC, TDS, SAL, SG
  ├── applyAntiFlash()             ← optional exponential moving average
  └── applySoftwareCalibration()   ← ±50% trim from config
       │
       ▼
  systemState update:
    .conductivity_raw
    .conductivity_compensated
    .conductivity_calibrated
    .temperature_celsius
       │
       ▼
  waterMeterManager.update()
    └── per meter: poll pulse count (ISR-driven), compute flow rate
```

---

## Input Processing (`processInputs()` — `main.cpp:589`)

The rotary encoder is the sole physical input device (KY-040 on GPIO15/2/0).

```
Read encoder button (GPIO0, active LOW)
       │
       ├── Debounce (ENCODER_BTN_DEBOUNCE_MS = 50 ms)
       │
       ├── Falling edge (press) → record btn_press_start
       │
       ├── Held >= 1500 ms → long press → display.toggleMenu()
       │
       └── Rising edge (release, no long press) → short press → display.select()

Encoder rotation is handled by ISR on GPIO15 (CLK) and GPIO2 (DT).
  └── Delta count drives display.nextScreen() / display.prevScreen()
      or value increment/decrement when editing a menu field.
```

---

## Feedwater Pump Monitor (`updateFeedwaterPumpMonitor()` — `main.cpp`)

Monitors the CT-6 boiler feedwater pump contactor via a PC817 optocoupler
on GPIO35. Called at the top of each Control task tick (100 ms).

### Data Tracked

| Field | Type | Description |
|-------|------|-------------|
| `feedwater_pump_on` | bool | Current pump state |
| `fw_pump_cycle_count` | uint32 | Total pump activations (persisted to NVS) |
| `fw_pump_on_time_sec` | uint32 | Cumulative on-time in seconds (persisted to NVS) |
| `fw_pump_current_cycle_ms` | uint32 | Duration of current cycle while running |
| `fw_pump_last_cycle_sec` | uint32 | Duration of last completed cycle |

### Logic

```
Read GPIO35 (active LOW via optocoupler)
       │
       ├── Debounce (200 ms window for contactor chatter)
       │
       ├── Rising edge (pump turns ON):
       │     → increment fw_pump_cycle_count
       │     → record fw_pump_last_on_time = millis()
       │     → log FW_PUMP_ON event with cycle number
       │
       ├── Falling edge (pump turns OFF):
       │     → calculate cycle_sec = (millis - last_on_time) / 1000
       │     → accumulate fw_pump_on_time_sec += cycle_sec
       │     → store fw_pump_last_cycle_sec = cycle_sec
       │     → log FW_PUMP_OFF event with cycle duration
       │
       └── While pump ON:
             → update fw_pump_current_cycle_ms continuously

NVS persist: fw_cycles + fw_ontime saved every 5 minutes
```

### Logged Events

| Event Type | Description | Value Field |
|------------|-------------|-------------|
| `FW_PUMP_ON` | Feedwater pump started | Cycle count |
| `FW_PUMP_OFF` | Feedwater pump stopped | Cycle duration (seconds) |

---

## Alarm Processing (`checkAlarms()` — `main.cpp:486`)

Runs at the end of each Control task tick (100 ms).

### Alarm Sources

| Bitmask | Name | Trigger Condition |
|---------|------|-------------------|
| `0x0001` | `ALARM_COND_HIGH` | Conductivity > high threshold (absolute or % of setpoint) |
| `0x0002` | `ALARM_COND_LOW` | Conductivity < low threshold |
| `0x0004` | `ALARM_BLOWDOWN_TIMEOUT` | `blowdownController.isTimeout()` |
| `0x0008–0x0020` | `ALARM_FEED1/2/3_TIMEOUT` | Pump ran past `time_limit_seconds` |
| `0x0040` | *(reserved)* | Was `ALARM_NO_FLOW`; GPIO35 repurposed for feedwater pump monitor |
| `0x0080` | `ALARM_SENSOR_ERROR` | `conductivitySensor.isSensorOK()` returns false |
| `0x0100` | `ALARM_TEMP_ERROR` | `conductivitySensor.isTempSensorOK()` returns false |
| `0x0200` | `ALARM_DRUM_LEVEL_1` | AUX_INPUT1 (GPIO17) LOW |
| `0x2000` | `ALARM_VALVE_FAULT` | `blowdownController.isValveFault()` — feedback < 3 mA |

### Edge Detection

```
new_alarms = current scan bitmask
rising_alarms  = new_alarms & ~previous_alarms   ← newly activated
falling_alarms = previous_alarms & ~new_alarms   ← just cleared

For each rising alarm:
  → dataLogger.logAlarm(code, name, true, trigger_value)
  → display.showAlarm(name)

For any falling alarm:
  → display.clearAlarm()

systemState.active_alarms = new_alarms
```

---

## Logging Task Detail (`taskLoggingLoop` — `main.cpp:328`)

Every 1000 ms:

```
dataLogger.update()
  ├── handleWiFiEvents()          ← auto-reconnect if disconnected
  └── uploadBuffered()            ← drain circular buffer (100 slots)

if (millis() - lastLogTime >= log_interval_ms):
  logSensorData()
    ├── Build sensor_reading_t from systemState + subsystem getters
    └── dataLogger.logReading(&reading)
          ├── WiFi connected? → HTTP POST JSON to <host>:<port>
          └── Offline?        → bufferReading() into circular buffer
```

### Logged Fields

| Field | Source |
|-------|--------|
| `conductivity` | `systemState.conductivity_calibrated` |
| `temperature` | `systemState.temperature_celsius` |
| `water_meter1/2` | `waterMeterManager.getMeter(n)->getTotalVolume()` |
| `flow_rate` | `waterMeterManager.getCombinedFlowRate()` |
| `blowdown_active` | `blowdownController.isActive()` |
| `valve_position_mA` | `blowdownController.getFeedbackmA()` |
| `pump1/2/3_active` | `pumpManager.getPump(n)->isRunning()` |
| `feedwater_pump_on` | `systemState.feedwater_pump_on` |
| `fw_pump_cycle_count` | `systemState.fw_pump_cycle_count` |
| `fw_pump_on_time_sec` | `systemState.fw_pump_on_time_sec` |
| `active_alarms` | `systemState.active_alarms` |

---

## Configuration Persistence

### NVS Layout (namespace: `boiler_cfg`)

| Key | Type | Contents |
|-----|------|----------|
| `config` | Blob (~1 KiB) | `system_config_t` — all subsystem configs, network creds, security, display prefs |
| `wm1_total` | uint32 | Water meter 1 totalizer (gallons) |
| `wm2_total` | uint32 | Water meter 2 totalizer |
| `pump1_tot` | uint32 | Pump 1 cumulative runtime (seconds) |
| `pump2_tot` | uint32 | Pump 2 cumulative runtime |
| `pump3_tot` | uint32 | Pump 3 cumulative runtime |
| `blow_total` | uint32 | Cumulative blowdown time (seconds) |
| `fw_cycles` | uint32 | Feedwater pump activation count |
| `fw_ontime` | uint32 | Feedwater pump cumulative on-time (seconds) |
| `last_cal` | uint32 | Last calibration epoch timestamp |

### Save Triggers

| Trigger | What Is Saved |
|---------|---------------|
| User exits edit mode (LCD or web) | `config` blob |
| WiFi credentials change | `config` blob |
| Calibration performed | `config` blob + `last_cal` |
| Every 5 minutes | `wm*_total`, `pump*_tot`, `blow_total`, `fw_cycles`, `fw_ontime` |
| Graceful shutdown | All keys |

### Validation on Load (`loadConfiguration()`)

1. Check `config` blob size matches `sizeof(system_config_t)`
2. Verify `magic == 0x43543630` ("CT60")
3. On failure → `initializeDefaults()` → `saveConfiguration()`

---

## HOA (Hand-Off-Auto) Control

Each pump and the blowdown controller implement independent HOA:

```
processHOA()
  ├── HOA_OFF  → force output off, ignore auto logic
  ├── HOA_HAND → force output on
  │     └── if elapsed > HOA_HAND_TIMEOUT_SEC (600 s) → revert to HOA_AUTO
  └── HOA_AUTO → proceed to automatic control logic
```

HOA is set via:
- LCD menu (Manual Control → HOA mode per output)
- Web UI (not yet fully implemented in current firmware)
- `setHOA(hoa_mode_t mode)` API on each controller object

---

## Sequence Diagram: Blowdown Cycle (Continuous Mode)

```
Time ──►

Control Task       BlowdownController      GPIO4/Relay      ADS1115      Actuator
    │                    │                      │               │            │
    │  update(cond,ok)   │                      │               │            │
    ├───────────────────►│                      │               │            │
    │                    │  cond > setpoint      │               │            │
    │                    │  state→VALVE_OPENING  │               │            │
    │                    ├─setRelayState(HIGH)──►│               │            │
    │                    │                       │──20mA────────►│            │
    │                    │                       │               │──opens────►│
    │                    │                       │               │            │
    │   ...100ms ticks...│                       │               │            │
    │                    │  readFeedback()        │               │            │
    │                    ├──────────────────────────────────────►│            │
    │                    │◄─────────feedback_mA=20.3─────────────│            │
    │                    │  confirmed_open=true   │               │            │
    │                    │  state→BLOWING_DOWN    │               │            │
    │                    │                       │               │            │
    │   ...blowdown runs...                      │               │            │
    │                    │                       │               │            │
    │                    │  cond < (SP - DB)      │               │            │
    │                    │  state→VALVE_CLOSING   │               │            │
    │                    ├─setRelayState(LOW)───►│               │            │
    │                    │                       │──4mA─────────►│            │
    │                    │                       │               │──closes───►│
    │                    │                       │               │            │
    │                    │  readFeedback()        │               │            │
    │                    ├──────────────────────────────────────►│            │
    │                    │◄─────────feedback_mA=4.1──────────────│            │
    │                    │  confirmed_closed=true  │               │            │
    │                    │  state→IDLE             │               │            │
```

---

## Sequence Diagram: Fuzzy-Controlled Chemical Dose (Mode F)

```
Time ──►

Control Task    FuzzyController    PumpManager       ChemicalPump[i]    AccelStepper
    │                │                  │                   │                │
    │  evaluate()    │                  │                   │                │
    ├───────────────►│                  │                   │                │
    │                │─ fuzzify inputs  │                   │                │
    │                │─ evaluate rules  │                   │                │
    │                │─ defuzzify       │                   │                │
    │◄──fuzzy_result─│                  │                   │                │
    │                                   │                   │                │
    │  processFeedModes(bd, time,       │                   │                │
    │    contacts, vol, rates[])        │                   │                │
    ├──────────────────────────────────►│                   │                │
    │                                   │  processFeedMode()│                │
    │                                   ├──────────────────►│                │
    │                                   │                   │─ processModeF()│
    │                                   │                   │  dose = vol *  │
    │                                   │                   │  ml_per_gal *  │
    │                                   │                   │  (rate/100)    │
    │                                   │                   │  steps = dose  │
    │                                   │                   │  * steps_per_ml│
    │                                   │                   │                │
    │                                   │                   │─ start(steps)  │
    │                                   │                   ├───────────────►│
    │                                   │                   │                │
    │  pumpManager.update()             │                   │                │
    ├──────────────────────────────────►│                   │                │
    │                                   │  update()         │                │
    │                                   ├──────────────────►│                │
    │                                   │                   │─_stepper.run()─►│
```

---

## Error Handling Summary

| Subsystem | Error Condition | Response |
|-----------|----------------|----------|
| **EZO-EC** | No UART response within 1000 ms | `sensor_ok = false` → `ALARM_SENSOR_ERROR` |
| **MAX31865** | RTD fault register non-zero | `temp_sensor_ok = false` → `ALARM_TEMP_ERROR`; fall back to `manual_temperature` for EZO compensation |
| **Blowdown valve** | Feedback < 3 mA | `valve_fault = true` → `ALARM_VALVE_FAULT`; valve closed, state → `BD_STATE_ERROR` |
| **Blowdown valve** | Feedback doesn't confirm position within ball_valve_delay × 2 | Timeout → `BD_STATE_TIMEOUT` |
| **Pump** | Runtime exceeds `time_limit_seconds` | Pump stopped → `PUMP_STATE_LOCKED_OUT` → `ALARM_FEEDn_TIMEOUT` |
| **FW Pump Monitor** | GPIO35 via optocoupler | Logs `FW_PUMP_ON`/`FW_PUMP_OFF` events; tracks cycle count + on-time in NVS |
| **Drum level** | AUX_INPUT1 (GPIO17) LOW | `ALARM_DRUM_LEVEL_1` |
| **WiFi** | Disconnect detected | `dataLogger` auto-reconnects; readings buffered (100 slots) |
| **NVS** | Config load fails validation | Factory defaults loaded; `saveConfiguration()` called |
| **HOA HAND** | 10-minute timeout | Auto-reverts to `HOA_AUTO` |

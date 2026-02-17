# Blowdown Valve – Assured Automation E26NRXS4UV-EP420C

## Overview

The blowdown valve is an Assured Automation Series 26 stainless steel ball valve with an S4 series universal-voltage electric actuator and DPS (Digital Positioner System) configured for 4-20mA control with fail-closed action.

**Full Part Number:** E26NRXS4UV-EP420C

| Code | Meaning |
|------|---------|
| E | 1 inch NPT size |
| 26 | Series 26 – 2-piece stainless steel ball valve |
| NR | 316SS body, RPTFE seats |
| X | Full port |
| S4UV | S4 series actuator, universal voltage (24–240V AC/DC) |
| EP420C | Electronic positioner, 4-20mA, fail-to-**Closed** |

---

## Valve Specifications

| Parameter | Value |
|-----------|-------|
| **Size** | 1 inch NPT |
| **Type** | 2-way, full port ball valve |
| **Body / Ball / Stem** | 316 Stainless Steel |
| **Seats** | RPTFE (reinforced PTFE) |
| **Max Pressure** | 2,000 PSIG |
| **Temperature Range** | -30°F to 400°F |
| **End Connections** | Female NPT |

---

## Actuator Specifications (S4 Series)

| Parameter | Value |
|-----------|-------|
| **Voltage** | Universal: 24–240V AC/DC (auto-sensing) |
| **Supply Voltage (this system)** | 24 VDC |
| **Enclosure** | NEMA 4/4X, IP67 weatherproof |
| **Torque** | 170 in-lbs (smallest S4 model) |
| **Cycle Time (90°)** | ~14–30 seconds (model dependent) |
| **Duty Cycle** | 75% |
| **Wiring** | DIN connectors (field-wirable without opening actuator) |
| **Position Indication** | Visual indicator on actuator housing |

---

## DPS Positioner (4-20mA Control)

The DPS is a microprocessor-based electronic positioner integrated into the S4 actuator. It accepts a 4-20mA DC input signal and provides a 4-20mA DC position feedback output.

| Parameter | Value |
|-----------|-------|
| **Input Signal** | 4-20mA DC |
| **Output / Feedback Signal** | 4-20mA DC (proportional to position) |
| **4 mA** | Valve fully **closed** (0%) |
| **20 mA** | Valve fully **open** (100%) |
| **Fail Position** | **Closed** (on loss of control signal) |
| **Accuracy** | 2-3% full scale |
| **Calibration** | Self-calibrating on power-up |
| **Power Loss Behavior** | Holds last position (no spring return) |

---

## Design Decision: Full Open / Full Close Only

Although the EP420C positioner supports proportional (modulating) control across the full 0-100% range, **this system will operate the valve in binary mode only** — fully open or fully closed.

### Rationale

1. **Simplified programming** — The existing firmware state machine uses discrete open/close states (IDLE, VALVE_OPENING, BLOWING_DOWN, VALVE_CLOSING). Binary operation fits the current architecture with no changes to control logic.
2. **Boiler blowdown is inherently on/off** — The goal is to flush concentrated boiler water. Throttling a blowdown valve is unnecessary and can cause wire-drawing erosion on the ball and seats.
3. **Deterministic behavior** — Full open provides maximum flow for effective blowdown. Full close provides a positive shutoff with no leak path.
4. **Fail-safe simplicity** — On loss of control signal, the valve goes fully closed. No ambiguous intermediate position to worry about.

### Operating States

| Control Signal | Valve Position | Meaning |
|----------------|----------------|---------|
| 4 mA (or 0 mA / signal loss) | Fully **CLOSED** | Idle / safe state |
| 20 mA | Fully **OPEN** | Active blowdown |

No intermediate positions will be commanded.

---

## Electrical Design – Control Signal (4-20mA Output)

Since the ESP32 does not have a true current-loop output, the 4-20mA command signal is generated using a **relay-switched dual-resistor circuit** powered from the 24 VDC supply. This approach eliminates the need for DACs, voltage-to-current converters, or analog output modules.

### Circuit Concept

A 24 VDC supply drives a fixed current through a precision resistor into the actuator's 4-20mA input. A relay selects between two resistors: one sized for 4 mA (closed) and one sized for 20 mA (open).

### Resistor Calculations

The DPS positioner input appears as a low-impedance current sink. The loop voltage is 24 VDC. Accounting for the positioner's internal voltage drop (~10V typical for a 4-20mA loop), the available driving voltage is approximately 14V.

**For 20 mA (open):**

```
R_open = V_loop / I = 14V / 0.020A = 700 Ω
```

Use a **680 Ω** standard value (yields ~20.6 mA — within positioner tolerance).

**For 4 mA (closed):**

```
R_close = V_loop / I = 14V / 0.004A = 3,500 Ω
```

Use a **3.3 kΩ** standard value (yields ~4.2 mA — within positioner tolerance).

> **Note:** Exact resistor values should be verified against the actual loop voltage drop measured at the installed actuator. The positioner's self-calibration will accommodate minor current deviations, but the signal should be within the 4-20mA window. Use 1% tolerance metal film resistors rated for at least 0.5W.

### Schematic

```
    24 VDC
      │
      ├──── R_close (3.3 kΩ) ──── NC ─┐
      │                                 │
      └──── R_open  (680 Ω)  ──── NO ──┤  RELAY (SPDT)
                                        │
                                        ├───── COMMON ──→ Actuator 4-20mA INPUT (+)
                                        │
                                    Actuator 4-20mA INPUT (−) ──→ GND (24V return)


    Relay coil driven by ESP32 GPIO4 via MOSFET/driver
      │
    GPIO4 ──→ [MOSFET Driver] ──→ Relay Coil ──→ 24V / GND
```

### Relay Truth Table

| GPIO4 State | Relay Position | Resistor Selected | Current | Valve |
|-------------|----------------|-------------------|---------|-------|
| LOW (default) | NC (de-energized) | R_close (3.3 kΩ) | ~4 mA | **CLOSED** |
| HIGH | NO (energized) | R_open (680 Ω) | ~20 mA | **OPEN** |

**Fail-safe:** If the ESP32 resets, loses power, or the relay coil de-energizes for any reason, the relay returns to NC, selecting the 3.3 kΩ resistor, which sends ~4 mA to the actuator — commanding **CLOSED**. The actuator's own fail-closed behavior on signal loss provides a second layer of protection.

### Component List — Control Signal Circuit

| Ref | Component | Value / Part | Purpose |
|-----|-----------|-------------|---------|
| K1 | SPDT Relay | 5V coil, SPDT, ≥100 mA contacts | Selects control resistor |
| R_open | Resistor | 680 Ω, 1%, 0.5W metal film | Sets 20 mA (open) signal |
| R_close | Resistor | 3.3 kΩ, 1%, 0.5W metal film | Sets 4 mA (closed) signal |
| Q1 | N-channel MOSFET | 2N7000 or IRLZ44N | Drives relay coil from GPIO4 |
| D1 | Flyback diode | 1N4148 or 1N4007 | Protects MOSFET from relay coil EMF |
| R_gate | Resistor | 1 kΩ | Gate pull-down for MOSFET |

---

## Electrical Design – Position Feedback (4-20mA Input)

The actuator's 4-20mA position feedback output is read by the ESP32 to confirm the valve actually reached the commanded position. This provides closed-loop verification of valve state.

### Feedback Signal Interpretation

| Feedback Current | Valve Position |
|------------------|----------------|
| 4 mA | Fully closed (0%) |
| 20 mA | Fully open (100%) |
| ~12 mA | Mid-travel (valve in transit) |
| < 3.5 mA | Fault / wiring error |

For binary operation, the firmware only needs to distinguish three conditions:

| Condition | Feedback Range | Action |
|-----------|----------------|--------|
| Confirmed CLOSED | < 5 mA | Normal idle state |
| Confirmed OPEN | > 19 mA | Normal blowdown state |
| In transit / unknown | 5–19 mA | Waiting (ball valve delay period) |

### Feedback Reading Circuit

The 4-20mA feedback signal is converted to a voltage using a precision sense resistor, then read by the **ADS1115 external 16-bit ADC** on the shared I2C bus. A 150 Ω sense resistor converts 4-20mA to 0.6–3.0V.

> **Why ADS1115?** All ESP32 ADC1 input-only pins (GPIO34–39) are already occupied (water meter, feedwater pump monitor, EZO-EC RX, MAX31865 MISO). The ADS1115 provides 16-bit resolution with no additional GPIO — it shares the existing I2C bus (GPIO21 SDA / GPIO22 SCL) at address `0x48`.

```
4 mA × 150 Ω = 0.60V
20 mA × 150 Ω = 3.00V
```

```
    Actuator Feedback OUTPUT (+)
          │
          │
        R_sense (150 Ω, 1%, 0.25W)
          │
          ●──────────── ADS1115 Channel 0 (A0)
          │
        C_filt (0.1 µF)
          │
         GND ──── Actuator Feedback OUTPUT (−) ──── 24V GND
```

### Feedback Component List

| Ref | Component | Value / Part | Purpose |
|-----|-----------|-------------|---------|
| R_sense | Resistor | 150 Ω, 1%, 0.25W metal film | Converts 4-20mA to 0.6-3.0V |
| C_filt | Capacitor | 0.1 µF ceramic | Filters noise on ADC input |
| (optional) TVS | TVS diode | 3.3V unidirectional | Protects ADC from transients |
| U_adc | ADS1115 | I2C addr 0x48 | 16-bit external ADC (shared I2C bus) |

### ADC Configuration

The ADS1115 is on the same I2C bus as the LCD display (GPIO21 SDA, GPIO22 SCL, 400 kHz). Channel 0 is dedicated to the blowdown valve feedback. The `blowdown.h` / `blowdown.cpp` firmware reads this channel via `ads1115ReadChannel(0)` inside the `readFeedback()` method.

---

## Wiring Summary

### Power

```
24 VDC Power Supply
  (+) ──→ S4 Actuator Power (+) terminal  [L / +]
  (−) ──→ S4 Actuator Power (−) terminal  [N / −]
  (+) ──→ Control signal circuit (through R_open / R_close)
  (−) ──→ Control signal circuit GND
```

### Control Signal (ESP32 → Actuator)

```
ESP32 GPIO4 ──→ MOSFET gate (Q1)
Q1 drain    ──→ Relay coil (K1)
K1 COMMON   ──→ Actuator DPS Input (+)  [DIN connector, terminal 2]
                 Actuator DPS Input (−)  [DIN connector, terminal 1] ──→ 24V GND
```

### Position Feedback (Actuator → ESP32)

```
Actuator DPS Output (+) [terminal E2] ──→ R_sense (150 Ω) ──→ ADS1115 CH0
Actuator DPS Output (−)               ──→ 24V GND / ESP32 GND (shared)
```

### DIN Connector Reference (S4 Actuator)

The S4 actuator uses DIN connectors for field wiring. The middle small DIN connector is for the 4-20mA control/feedback:

| Terminal | Function |
|----------|----------|
| 1 | Input (−) negative |
| 2 | Input (+) positive (4-20mA command) |
| E2 | Output (+) positive (4-20mA feedback) |

> **Important:** Do not cut power to the actuator motor using the dry contact switches. The actuator must have continuously energized power to function. The control signal alone determines valve position.

---

## Firmware Integration

### Current Architecture

The existing `BlowdownController` class (`blowdown.h` / `blowdown.cpp`) drives GPIO4 as a simple digital output:

- `HIGH` = valve open (relay energized)
- `LOW` = valve closed (relay de-energized)

### No Firmware Changes Required for Control

Because the relay-switched resistor circuit translates GPIO4 HIGH/LOW into 20mA/4mA respectively, **the existing firmware open/close logic works as-is**. The `setRelayState()` method in `blowdown.cpp` already writes HIGH or LOW to GPIO4, which is exactly what this circuit needs.

### Firmware Integration: Position Feedback

The `BlowdownController` class (`blowdown.h` / `blowdown.cpp`) reads position feedback via the ADS1115 external ADC on each `update()` call:

```cpp
// Read 4-20mA feedback via ADS1115 channel 0 + 150Ω sense resistor
int16_t raw_adc = ads1115ReadChannel(BLOWDOWN_FEEDBACK_ADS_CH);
float voltage = raw_adc * 0.000125;  // ADS1115 at ±4.096V gain: 125µV/bit
float current_mA = (voltage / BLOWDOWN_FEEDBACK_R_SENSE) * 1000.0;

bool confirmed_open  = (current_mA > BLOWDOWN_MA_OPEN_MIN);      // > 19 mA
bool confirmed_closed = (current_mA < BLOWDOWN_MA_CLOSED_MAX);   // < 5 mA
bool valve_fault = (current_mA < BLOWDOWN_MA_FAULT_LOW);         // < 3 mA (wiring fault)
```

This feedback is used to:
- Confirm the valve reached the commanded position (replaces time-based delay)
- Detect a stuck valve (commanded open but feedback still reads closed)
- Trigger a `VALVE FAULT` alarm if feedback current drops below 3 mA (wiring fault)

---

## Safety Considerations

1. **Dual fail-safe:** Both the relay circuit (de-energize → 4 mA → closed) and the actuator's EP420C fail mode (loss of signal → closed) ensure the valve defaults to closed on any fault.
2. **Power loss:** The actuator holds its last position on power loss (no spring return). The relay also de-energizes, so on power restoration the control signal will be 4 mA (closed) and the actuator will drive to closed.
3. **Timeout protection:** The firmware's existing `checkTimeout()` method prevents indefinite blowdown by closing the valve after the configurable time limit.
4. **Feedback-based fault detection:** The `readFeedback()` method detects wiring faults (< 3 mA) and stuck valves, triggering `ALARM_VALVE_FAULT`.
5. **HOA override:** Hand/Off/Auto control allows manual intervention via the LCD menu or web UI.

---

## References

- Assured Automation Series 26 product page: https://assuredautomation.com/26/
- Assured Automation S4 actuator datasheet: https://assuredautomation.com/literature/S4_datasheet.pdf
- S4 wiring instructions: https://assuredautomation.com/news-and-training/multi-voltage-reversible-electric-actuator-wiring-instructions/
- Firmware source: `firmware/esp32_boiler_controller/src/blowdown.cpp`
- Pin definitions: `firmware/esp32_boiler_controller/include/pin_definitions.h`

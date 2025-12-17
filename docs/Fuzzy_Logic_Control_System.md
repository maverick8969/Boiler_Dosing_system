# Fuzzy Logic Control System for Boiler Water Chemistry

## Overview

The fuzzy logic controller provides intelligent, adaptive chemical dosing based on multiple water quality parameters. Unlike simple threshold-based control, fuzzy logic mimics expert human decision-making by using linguistic rules and gradual transitions.

## Why Fuzzy Logic for Boiler Control?

| Traditional Control | Fuzzy Logic Control |
|---------------------|---------------------|
| Binary decisions (ON/OFF) | Gradual, proportional response |
| Single parameter control | Multi-parameter reasoning |
| Abrupt setpoint transitions | Smooth control action |
| Requires precise calibration | Tolerant of sensor uncertainty |
| One rule at a time | Multiple rules fire simultaneously |

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        FUZZY INFERENCE ENGINE                           │
│  ┌───────────────┐    ┌──────────────┐    ┌───────────────────────┐    │
│  │   FUZZIFY     │───▶│  RULE BASE   │───▶│     DEFUZZIFY         │    │
│  │               │    │              │    │                       │    │
│  │ Crisp→Fuzzy   │    │ IF-THEN Rules│    │ Fuzzy→Crisp           │    │
│  └───────────────┘    └──────────────┘    └───────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
         ▲                                              │
         │                                              ▼
┌────────┴────────┐                          ┌─────────────────────┐
│     INPUTS      │                          │      OUTPUTS        │
├─────────────────┤                          ├─────────────────────┤
│ • Conductivity  │                          │ • Blowdown Rate     │
│ • Alkalinity    │                          │ • Caustic (NaOH)    │
│ • Sulfite       │                          │ • Sulfite Dosing    │
│ • pH            │                          │ • Acid (H2SO3)      │
│ • Temperature   │                          └─────────────────────┘
│ • Trend         │
└─────────────────┘
```

## Input Variables

### 1. Conductivity (TDS Proxy)
- **Source:** Sensorex CS675HTTC-P1K sensor
- **Range:** 0-5000 µS/cm
- **Purpose:** Indicates cycles of concentration
- **Membership Sets:**

```
µ(x)
1.0 ─┬─────╮                              ╭─────┬────
    │VLow  ╲    Low      Normal    High  ╱ VHigh│
    │       ╲    ╱╲        ╱╲      ╱╲   ╱       │
    │        ╲  ╱  ╲      ╱  ╲    ╱  ╲ ╱        │
0.0 ─┴────────╲╱────╲────╱────╲──╱────╳─────────┴────
    0       1000    1500  2000  2500  3000   5000 µS/cm
                          ▲
                       Setpoint
```

### 2. Alkalinity (ppm as CaCO3)
- **Source:** Manual test or inline analyzer
- **Range:** 0-1000 ppm
- **Purpose:** Corrosion protection, pH buffering
- **Target:** 200-400 ppm for low-pressure boilers

### 3. Sulfite Residual (ppm SO₃)
- **Source:** Manual test or inline analyzer
- **Range:** 0-100 ppm
- **Purpose:** Oxygen scavenging
- **Target:** 20-40 ppm (varies with pressure)

### 4. pH
- **Source:** pH sensor or manual test
- **Range:** 7.0-14.0
- **Purpose:** Corrosion control
- **Target:** 10.5-11.5 for typical boilers

### 5. Temperature (°C)
- **Source:** Pt1000 RTD
- **Range:** 0-100°C
- **Purpose:** Compensation factor

### 6. Trend (Rate of Change)
- **Source:** Calculated from conductivity history
- **Range:** -100 to +100 µS/cm per minute
- **Purpose:** Predictive control

## Output Variables

| Output | Range | Controls |
|--------|-------|----------|
| Blowdown Rate | 0-100% | Surface blowdown valve duty cycle |
| Caustic Rate | 0-100% | NaOH pump speed |
| Sulfite Rate | 0-100% | Sulfite/amine pump speed |
| Acid Rate | 0-100% | H2SO3 pump speed |

## Rule Base

The system uses **25 default rules** based on industry best practices. Rules can be customized via the LCD menu or configuration.

### Core Conductivity Rules

| # | IF Conductivity is... | THEN Blowdown is... |
|---|----------------------|---------------------|
| 1 | Very High | Very High |
| 2 | High | High |
| 3 | Normal | Zero |
| 4 | Low | Zero |
| 5 | High AND Trend Increasing | Very High |

### Core Alkalinity Rules

| # | IF Alkalinity is... | THEN Caustic is... | Blowdown is... |
|---|--------------------|--------------------|----------------|
| 6 | Very Low | Very High | - |
| 7 | Low | High | - |
| 8 | Normal | Zero | - |
| 9 | High | Zero | Medium |
| 10 | Very High | Zero | High |

### Core Sulfite Rules

| # | IF Sulfite is... | THEN Sulfite Dose is... |
|---|-----------------|------------------------|
| 11 | Very Low | Very High |
| 12 | Low | High |
| 13 | Normal | Low (maintenance) |
| 14 | High | Zero |
| 15 | Very High | Zero + Blowdown Low |

### Combined Multi-Parameter Rules

| # | IF | THEN |
|---|-----|------|
| 21 | Cond HIGH AND Alk HIGH | Blowdown VERY HIGH |
| 22 | Cond LOW AND Alk LOW | Caustic HIGH |
| 23 | Sulfite LOW AND Temp HOT | Sulfite VERY HIGH |
| 24 | Cond NORMAL AND Alk NORMAL AND Sulfite NORMAL | All outputs minimal |

## Inference Method

The controller uses **Mamdani inference** with:
- **T-norm:** MIN (AND operation)
- **S-norm:** MAX (OR operation / aggregation)
- **Implication:** MIN (clipping)
- **Defuzzification:** Centroid (Center of Gravity)

### Inference Example

```
Given:
  Conductivity = 2800 µS/cm (setpoint 2500)
  Alkalinity = 350 ppm (normal)
  Sulfite = 25 ppm (slightly low)

Fuzzification:
  Cond: Normal=0.3, High=0.7
  Alk:  Normal=0.9, High=0.1
  Sulf: Low=0.6, Normal=0.4

Rule Activation:
  Rule 2 (Cond HIGH → Blowdown HIGH): 0.7
  Rule 3 (Cond NORMAL → Blowdown ZERO): 0.3
  Rule 8 (Alk NORMAL → Caustic ZERO): 0.9
  Rule 12 (Sulf LOW → Sulfite HIGH): 0.6
  Rule 13 (Sulf NORMAL → Sulfite LOW): 0.4

Aggregation & Defuzzification:
  Blowdown: 58% (weighted between HIGH and ZERO)
  Caustic: 5% (mostly ZERO)
  Sulfite: 62% (weighted between HIGH and LOW)
```

## Manual Test Input

Since alkalinity and sulfite typically require titration tests, operators can enter manual values via the LCD menu:

```
MANUAL TEST INPUTS
├── Alkalinity
│   └── Enter ppm: [___]
├── Sulfite
│   └── Enter ppm: [___]
├── pH
│   └── Enter value: [__._]
└── Clear All Manual Values
```

Manual values remain active until:
- Cleared manually
- New value entered
- System reboot (configurable)

## Configuration Parameters

```cpp
typedef struct {
    // Setpoints (center of "Normal" membership)
    float cond_setpoint;        // Default: 2500 µS/cm
    float alk_setpoint;         // Default: 300 ppm
    float sulfite_setpoint;     // Default: 30 ppm
    float ph_setpoint;          // Default: 11.0

    // Deadbands (no action zone)
    float cond_deadband;        // Default: 200 µS/cm
    float alk_deadband;         // Default: 50 ppm
    float sulfite_deadband;     // Default: 5 ppm
    float ph_deadband;          // Default: 0.3

    // Output scaling
    float blowdown_max;         // Max seconds per cycle
    float caustic_max_ml_min;   // Max pump rate
    float sulfite_max_ml_min;
    float acid_max_ml_min;

    bool aggressive_mode;       // Faster response
} fuzzy_config_t;
```

## LCD Menu Integration

```
FUZZY CONTROL
├── Enable/Disable: [ON ]
├── View Status
│   ├── Inputs (fuzzified)
│   ├── Active Rules: 8
│   ├── Outputs (%)
│   └── Dominant Rule: #5
├── Setpoints
│   ├── Conductivity: [2500] µS/cm
│   ├── Alkalinity: [300] ppm
│   ├── Sulfite: [30] ppm
│   └── pH: [11.0]
├── Deadbands
│   └── ...
├── Manual Test Input
│   └── ...
└── Advanced
    ├── Aggressive Mode: [OFF]
    ├── Rule Editor
    └── Export/Import Rules
```

## Integration with Pump Control

The fuzzy output (0-100%) is converted to actual pump operation:

```cpp
// In chemical_pump.cpp
void applyFuzzyOutput(fuzzy_result_t& result) {
    // Caustic pump (NaOH)
    float caustic_ml_min = result.caustic_rate * config.caustic_max_ml_min / 100.0f;
    pump_naoh.setFlowRate(caustic_ml_min);

    // Sulfite pump
    float sulfite_ml_min = result.sulfite_rate * config.sulfite_max_ml_min / 100.0f;
    pump_sulfite.setFlowRate(sulfite_ml_min);

    // Blowdown valve
    blowdown.setIntensity(result.blowdown_rate);
}
```

## Data Logging

Fuzzy controller state is logged to TimescaleDB:

```sql
CREATE TABLE fuzzy_log (
    time            TIMESTAMPTZ NOT NULL,
    cond_input      REAL,
    alk_input       REAL,
    sulfite_input   REAL,
    blowdown_out    REAL,
    caustic_out     REAL,
    sulfite_out     REAL,
    acid_out        REAL,
    active_rules    INTEGER,
    dominant_rule   INTEGER
);
```

## Tuning Guidelines

### Conservative (Default)
- Wide deadbands
- Low output scaling
- Best for stable systems

### Aggressive
- Narrow deadbands
- Higher output scaling
- For systems with rapid changes

### High Makeup Water Systems
- Increase conductivity setpoint tolerance
- Higher caustic rates (makeup dilutes alkalinity)
- Higher sulfite rates (more oxygen ingress)

### Condensate Return Systems
- Lower alkalinity setpoint (condensate is pure)
- Monitor pH closely (CO₂ in condensate)

## Safety Interlocks

The fuzzy controller respects all safety limits:

1. **Maximum Blowdown Time** - Hard limit per cycle
2. **Chemical Lockout** - After blowdown to prevent waste
3. **Alarm Override** - High-high conductivity bypasses fuzzy, forces max blowdown
4. **Manual Override** - Operator can disable fuzzy and use manual rates

## References

1. ASME Guidelines for Boiler Water Quality
2. Nalco Water Handbook
3. "Fuzzy Logic with Engineering Applications" - Timothy Ross
4. ASHRAE Handbook - HVAC Systems and Equipment

## Future Enhancements

- [ ] Adaptive rule learning based on operator corrections
- [ ] Integration with inline alkalinity/sulfite analyzers
- [ ] Predictive control using trend analysis
- [ ] Remote rule tuning via web interface
- [ ] Machine learning optimization of rule weights

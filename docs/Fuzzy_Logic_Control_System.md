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
┌────────┴────────┐                          ┌─────────────────────────┐
│  INPUTS (Manual)│                          │   OUTPUTS               │
├─────────────────┤                          ├─────────────────────────┤
│ • TDS           │                          │ • Blowdown Rate (REC)   │
│ • Alkalinity    │                          │ • Caustic (NaOH)        │
│ • Sulfite       │                          │ • Sulfite Dosing        │
│ • pH            │                          │ • Acid (H2SO3)          │
├─────────────────┤                          └─────────────────────────┘
│  INPUT (Sensor) │
├─────────────────┤       Note: Blowdown output is a RECOMMENDATION
│ • Temperature   │       only. Operator controls the valve manually.
│ • Trend         │
└─────────────────┘
```

**Manual Operation Mode:** All inputs except temperature are entered manually via the Web UI or LCD menu. This allows the system to work with standard titration tests and periodic TDS measurements.

## Input Variables

### 1. TDS (Total Dissolved Solids)
- **Source:** Manual test (TDS meter or calculated from conductivity)
- **Range:** 0-5000 ppm
- **Purpose:** Indicates cycles of concentration, determines blowdown need
- **Target:** 2000-3000 ppm for typical low-pressure boilers
- **Membership Sets:**

```
µ(x)
1.0 ─┬─────╮                              ╭─────┬────
    │VLow  ╲    Low      Normal    High  ╱ VHigh│
    │       ╲    ╱╲        ╱╲      ╱╲   ╱       │
    │        ╲  ╱  ╲      ╱  ╲    ╱  ╲ ╱        │
0.0 ─┴────────╲╱────╲────╱────╲──╱────╳─────────┴────
    0       1000    1500  2000  2500  3000   5000 ppm
                          ▲
                       Setpoint
```

### 2. Alkalinity (ppm as CaCO3)
- **Source:** Manual test (titration)
- **Range:** 0-1000 ppm
- **Purpose:** Corrosion protection, pH buffering
- **Target:** 200-400 ppm for low-pressure boilers

### 3. Sulfite Residual (ppm SO₃)
- **Source:** Manual test (titration or test kit)
- **Range:** 0-100 ppm
- **Purpose:** Oxygen scavenging
- **Target:** 20-40 ppm (varies with pressure)

### 4. pH
- **Source:** Manual test (meter or strips)
- **Range:** 7.0-14.0
- **Purpose:** Corrosion control
- **Target:** 10.5-11.5 for typical boilers

### 5. Temperature (°C)
- **Source:** Pt1000 RTD (automatic sensor)
- **Range:** 0-100°C
- **Purpose:** Compensation factor, reference display

### 6. Trend (Rate of Change)
- **Source:** Calculated from TDS history
- **Range:** -100 to +100 ppm per hour
- **Purpose:** Predictive control (optional)

## Output Variables

| Output | Range | Controls |
|--------|-------|----------|
| Blowdown Rate | 0-100% | **RECOMMENDATION** - Operator controls valve manually |
| Caustic Rate | 0-100% | NaOH pump speed |
| Sulfite Rate | 0-100% | Sulfite/amine pump speed |
| Acid Rate | 0-100% | H2SO3 pump speed |

> **Note:** Blowdown is manual in this system. The fuzzy logic provides a recommendation percentage, but the operator decides when and how long to open the blowdown valve.

## Rule Base

The system uses **25 default rules** based on industry best practices. Rules can be customized via the LCD menu or configuration.

### Core TDS Rules

| # | IF TDS is... | THEN Blowdown (Rec) is... |
|---|--------------|---------------------------|
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
| 21 | TDS HIGH AND Alk HIGH | Blowdown (Rec) VERY HIGH |
| 22 | TDS LOW AND Alk LOW | Caustic HIGH |
| 23 | Sulfite LOW AND Temp HOT | Sulfite VERY HIGH |
| 24 | TDS NORMAL AND Alk NORMAL AND Sulfite NORMAL | All outputs minimal |

## Inference Method

The controller uses **Mamdani inference** with:
- **T-norm:** MIN (AND operation)
- **S-norm:** MAX (OR operation / aggregation)
- **Implication:** MIN (clipping)
- **Defuzzification:** Centroid (Center of Gravity)

### Inference Example

```
Given:
  TDS = 2800 ppm (setpoint 2500)
  Alkalinity = 350 ppm (normal)
  Sulfite = 25 ppm (slightly low)

Fuzzification:
  TDS:  Normal=0.3, High=0.7
  Alk:  Normal=0.9, High=0.1
  Sulf: Low=0.6, Normal=0.4

Rule Activation:
  Rule 2 (TDS HIGH → Blowdown HIGH): 0.7
  Rule 3 (TDS NORMAL → Blowdown ZERO): 0.3
  Rule 8 (Alk NORMAL → Caustic ZERO): 0.9
  Rule 12 (Sulf LOW → Sulfite HIGH): 0.6
  Rule 13 (Sulf NORMAL → Sulfite LOW): 0.4

Aggregation & Defuzzification:
  Blowdown (Rec): 58% (weighted between HIGH and ZERO)
  Caustic: 5% (mostly ZERO)
  Sulfite: 62% (weighted between HIGH and LOW)
```

> The operator sees "Blowdown: 58%" as a recommendation and decides how to proceed.

## Manual Test Input

All water chemistry parameters (except temperature) are entered manually via the Web UI or LCD menu:

```
MANUAL TEST INPUTS
├── TDS
│   └── Enter ppm: [____]
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
- Configured expiration time (default: 8 hours)
- System reboot (configurable)

**Web UI:** Access at `http://<controller-ip>/` for a mobile-friendly test entry interface.

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
│   ├── Outputs (%) - Note: Blowdown is REC only
│   └── Dominant Rule: #5
├── Setpoints
│   ├── TDS: [2500] ppm
│   ├── Alkalinity: [300] ppm
│   ├── Sulfite: [30] ppm
│   └── pH: [11.0]
├── Deadbands
│   └── ...
├── Manual Test Input
│   ├── TDS: [____] ppm
│   ├── Alkalinity: [___] ppm
│   ├── Sulfite: [__] ppm
│   └── pH: [__._]
└── Advanced
    ├── Aggressive Mode: [OFF]
    ├── Rule Editor
    └── Export/Import Rules
```

## Integration with Pump Control

### Feed Mode F: Fuzzy Logic Control

The system supports **Feed Mode F** which combines fuzzy logic output with makeup water volume for proportional chemical dosing. This mode is selected per-pump in the configuration.

**How Mode F Works:**

```
Chemical Dose = Water Volume × ml_per_gallon_at_100pct × (Fuzzy Rate / 100%)
```

**Example with your system (16 gal boiler, 25 gal feedwater, 1 pulse/gallon):**
- Water meter counts 1 gallon of makeup water
- Fuzzy logic output for sulfite = 50%
- `ml_per_gallon_at_100pct` = 2.0 ml
- Dose = 1.0 gal × 2.0 ml × 0.50 = **1.0 ml sulfite**

This ensures dosing is proportional to BOTH:
1. Actual makeup water consumption (from water meter)
2. Fuzzy logic assessment of chemical need

### Configuration Parameters (per pump)

```cpp
typedef struct {
    // ... other mode parameters ...

    // Mode F: Fuzzy Logic (proportional to makeup water)
    float ml_per_gallon_at_100pct;  // ml chemical per gallon at 100% fuzzy output
    uint8_t fuzzy_meter_select;     // Water meter: 0=WM1, 1=WM2, 2=Both
} pump_config_t;
```

### Feed Mode Selection

| Feed Mode | Description | Use Case |
|-----------|-------------|----------|
| A | Blowdown + Feed | Feed during blowdown cycle |
| B | % of Blowdown | Feed proportional to blowdown duration |
| C | % of Time | Continuous duty cycle |
| D | Water Contact | Feed per water meter pulse |
| E | Paddlewheel | Feed per volume measured |
| **F** | **Fuzzy Logic** | **Intelligent dosing based on water chemistry** |
| S | Scheduled | Time-of-day scheduled feed |

### Mode F Implementation

```cpp
// In chemical_pump.cpp
void ChemicalPump::processModeF(float water_volume, float fuzzy_rate) {
    // Mode F: Fuzzy logic controlled dosing proportional to makeup water

    if (water_volume <= 0 || fuzzy_rate <= 0) {
        return;  // No water or fuzzy says no dosing needed
    }

    // Calculate ml to dose based on fuzzy rate and water volume
    float ml_to_dose = water_volume * _config->ml_per_gallon_at_100pct
                       * (fuzzy_rate / 100.0f);

    // Start pump for the calculated volume
    if (!_status.running && ml_to_dose >= 0.01f) {
        start(0, ml_to_dose);
    }
}
```

### Fuzzy Output Mapping

The fuzzy controller outputs are mapped to pumps as follows:

| Pump | Fuzzy Output | Chemical |
|------|--------------|----------|
| PUMP_H2SO3 (0) | `acid_rate` | H₂SO₃ (acid) |
| PUMP_NAOH (1) | `caustic_rate` | NaOH (caustic) |
| PUMP_AMINE (2) | `sulfite_rate` | Amine/Sulfite |

> **Note:** Blowdown is still MANUAL - the `blowdown_rate` is displayed as a recommendation only. The operator controls the blowdown valve.

## Data Logging

Fuzzy controller state is logged to TimescaleDB:

```sql
CREATE TABLE fuzzy_log (
    time            TIMESTAMPTZ NOT NULL,
    tds_input       REAL,           -- Manual TDS entry (ppm)
    alk_input       REAL,           -- Manual alkalinity (ppm)
    sulfite_input   REAL,           -- Manual sulfite (ppm)
    ph_input        REAL,           -- Manual pH
    blowdown_rec    REAL,           -- Blowdown RECOMMENDATION (%)
    caustic_out     REAL,           -- Caustic dosing output (%)
    sulfite_out     REAL,           -- Sulfite dosing output (%)
    acid_out        REAL,           -- Acid dosing output (%)
    active_rules    INTEGER,
    dominant_rule   INTEGER,
    confidence      TEXT            -- HIGH, MEDIUM, LOW
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

1. **Manual Blowdown Control** - Operator always controls blowdown valve
2. **Chemical Lockout** - After blowdown to prevent waste
3. **High TDS Warning** - Visual/audible alert when TDS exceeds threshold
4. **Manual Override** - Operator can disable fuzzy and use manual pump rates
5. **Low Confidence Alert** - Warning when insufficient test data is entered

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

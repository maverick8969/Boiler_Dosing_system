# Manual Test Input Guide

## Overview

The boiler controller supports both automatic sensor readings and manual test inputs. Since alkalinity, sulfite, and pH typically require titration or colorimetric tests, these values are entered manually through either:

1. **Web Interface** - Mobile-friendly webpage at `http://<controller-ip>/`
2. **LCD Menu** - Navigate to Manual Test Input menu

## Input Sources

| Parameter | Source | Frequency |
|-----------|--------|-----------|
| Conductivity | Automatic (Sensorex sensor) | Continuous |
| Temperature | Automatic (Pt1000 RTD) | Continuous |
| Alkalinity | Manual test (titration) | Daily or as needed |
| Sulfite | Manual test (titration/test kit) | Daily or as needed |
| pH | Manual test or sensor | Daily or continuous |

## How Limited Inputs Affect Control

### Confidence Levels

The fuzzy controller calculates a **confidence level** based on available inputs:

| Valid Inputs | Confidence | Control Behavior |
|--------------|------------|------------------|
| 4 (all) | **HIGH** | Full multi-parameter optimization |
| 2-3 | **MEDIUM** | Partial optimization, some assumptions |
| 1 (conductivity only) | **LOW** | Basic blowdown control only |

### Default Assumptions for Missing Inputs

When a parameter is unknown, the fuzzy controller assumes it's at the **Normal** (target) level:

```
Missing Input → Assumed "Normal" → Membership = 1.0 for Normal set
```

**Impact by parameter:**

| Missing Input | Assumption | Effect on Control |
|---------------|------------|-------------------|
| Alkalinity | Normal (300 ppm) | No caustic adjustment, blowdown unaffected |
| Sulfite | Normal (30 ppm) | Maintenance sulfite dosing only |
| pH | Normal (11.0) | No acid/caustic pH correction |

### Rule Activation with Limited Inputs

**Example 1: Conductivity Only (LOW confidence)**

Only conductivity-based rules fire with full strength:
```
Rule 1: IF Cond=VeryHigh THEN Blowdown=VeryHigh  ✓ Active
Rule 2: IF Cond=High THEN Blowdown=High          ✓ Active
Rule 6: IF Alk=VeryLow THEN Caustic=VeryHigh     ✗ Alk assumed Normal
Rule 11: IF Sulfite=VeryLow THEN Sulfite=VeryHigh ✗ Sulfite assumed Normal
```

Result: Blowdown control works correctly, but chemical dosing stays at maintenance levels.

**Example 2: Conductivity + Alkalinity (MEDIUM confidence)**

```
Inputs: Cond=2800 (High=0.7), Alk=180 (Low=0.8), Sulfite=Unknown

Active Rules:
- Rule 2: IF Cond=High THEN Blowdown=High (0.7)
- Rule 7: IF Alk=Low THEN Caustic=High (0.8)
- Rule 13: IF Sulfite=Normal THEN Sulfite=Low (1.0) ← maintenance

Result:
- Blowdown: 58% (responding to high conductivity)
- Caustic: 72% (responding to low alkalinity)
- Sulfite: 25% (maintenance level, unknown actual need)
```

**Example 3: All Inputs (HIGH confidence)**

```
Inputs: Cond=2800 (High=0.7), Alk=180 (Low=0.8),
        Sulfite=15 (VeryLow=0.9), pH=10.8 (Normal=0.6)

Active Rules (partial list):
- Rule 2: Blowdown=High (0.7)
- Rule 7: Caustic=High (0.8)
- Rule 11: Sulfite=VeryHigh (0.9)
- Rule 22: IF Cond=Low AND Alk=Low THEN Caustic=High (0.0 - not matching)

Result:
- Blowdown: 58%
- Caustic: 72%
- Sulfite: 85% (correctly responding to very low sulfite!)
- Acid: 5% (pH is normal)
```

## Recommended Testing Schedule

### Minimum (LOW confidence operation)
- Conductivity: Automatic ✓
- Weekly: pH, Alkalinity, Sulfite checks

### Standard (MEDIUM confidence operation)
- Conductivity: Automatic ✓
- Daily: Alkalinity check
- Every 2-3 days: Sulfite, pH

### Optimal (HIGH confidence operation)
- Conductivity: Automatic ✓
- Daily: Alkalinity, Sulfite, pH
- Or: Install inline analyzers for Alk/Sulfite/pH

## Test Value Expiration

Manual test values can be configured to expire after a set time:

```cpp
fuzzy_config_t config;
config.manual_input_timeout = 480;  // Minutes (8 hours)
```

| Setting | Behavior |
|---------|----------|
| 0 | Never expire (values persist until cleared or updated) |
| 60-240 | Expire after 1-4 hours (for frequently changing systems) |
| 480-1440 | Expire after 8-24 hours (typical daily testing) |

When a value expires, the controller reverts to the "Normal" assumption and confidence drops.

## Web Interface Usage

### Accessing the Interface

1. Connect to same WiFi network as controller
2. Open browser to `http://<controller-ip>/`
3. IP address shown on LCD status screen

### Entering Test Results

```
┌─────────────────────────────────────┐
│     Boiler Water Test Entry         │
│                                     │
│  Current Readings                   │
│  ┌─────────────────────────────┐   │
│  │ Conductivity    Temperature │   │
│  │    2847            42.3     │   │
│  │   µS/cm            °C       │   │
│  └─────────────────────────────┘   │
│                                     │
│  Enter Test Results                 │
│  ┌─────────────────────────────┐   │
│  │ Alkalinity (ppm CaCO₃)      │   │
│  │ [    285    ]        2h ago │   │
│  │                              │   │
│  │ Sulfite (ppm SO₃)           │   │
│  │ [     32    ]        2h ago │   │
│  │                              │   │
│  │ pH                           │   │
│  │ [   10.9   ]         2h ago │   │
│  └─────────────────────────────┘   │
│                                     │
│  [  Submit Tests  ] [ Clear All ]   │
│                                     │
│  Control Recommendations            │
│  Confidence: [HIGH]                 │
│  ┌─────────────────────────────┐   │
│  │ Blowdown    ████████░░  32% │   │
│  │ Caustic     ██░░░░░░░░  12% │   │
│  │ Sulfite     ███████░░░  48% │   │
│  │ Acid        ░░░░░░░░░░   0% │   │
│  └─────────────────────────────┘   │
│  Active Rules: 8                    │
└─────────────────────────────────────┘
```

### API Endpoints

For integration with other systems:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Current readings and test ages |
| `/api/fuzzy` | GET | Fuzzy outputs and confidence |
| `/api/tests` | GET | Current manual test values |
| `/api/tests` | POST | Submit new test values |
| `/api/tests` | DELETE | Clear all manual values |

**Example: Submit test via curl**
```bash
curl -X POST http://192.168.1.100/api/tests \
  -H "Content-Type: application/json" \
  -d '{"alkalinity": 285, "sulfite": 32, "ph": 10.9}'
```

## Safety Considerations

### With Limited Inputs

1. **Over-treatment risk**: Unknown low sulfite → actual corrosion risk
2. **Under-treatment risk**: Unknown high alkalinity → scale formation
3. **Chemical waste**: Maintenance dosing when not needed

### Recommendations

- Never operate at LOW confidence for extended periods
- If unable to test daily, consider conservative setpoints
- Install at least one additional sensor (pH recommended) for MEDIUM confidence baseline

### Override Behavior

High-high conductivity alarm **always** triggers maximum blowdown regardless of fuzzy output or confidence level. This is a safety interlock that cannot be disabled.

## Troubleshooting

### "Confidence: LOW" persistently
- Check if test values were entered
- Check if values expired (see age indicator)
- Verify web interface can reach controller

### Chemical dosing seems wrong
- Compare test values to setpoints in "Target Ranges" section
- Check if rules are firing as expected (Active Rules count)
- With limited inputs, controller may be making incorrect assumptions

### Test values not updating
- Clear browser cache
- Check controller WiFi connection
- Verify POST request succeeds (check browser console)

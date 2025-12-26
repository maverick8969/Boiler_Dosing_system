# Manual Test Input Guide

## Overview

The boiler controller is designed for **manual operation mode** where all water chemistry parameters except temperature are entered manually via test results. This approach is common for smaller boiler systems where:

- TDS/conductivity meters are tested periodically rather than continuously
- Alkalinity, sulfite, and pH require titration or colorimetric tests
- Operators want full visibility and control over the dosing process

Test values are entered through:

1. **Web Interface** - Mobile-friendly webpage at `http://<controller-ip>/`
2. **LCD Menu** - Navigate to Manual Test Input menu

## Input Sources

| Parameter | Source | Frequency |
|-----------|--------|-----------|
| TDS | Manual test (meter/titration) | Daily or as needed |
| Temperature | Automatic (Pt1000 RTD) | Continuous |
| Alkalinity | Manual test (titration) | Daily or as needed |
| Sulfite | Manual test (titration/test kit) | Daily or as needed |
| pH | Manual test (meter/strips) | Daily or as needed |

## Manual Blowdown Control

**Important:** Blowdown output is a **RECOMMENDATION** only. The operator manually controls the blowdown valve based on the fuzzy logic recommendation. This design ensures:

- Operator oversight of all blowdown operations
- Safety verification before each blowdown cycle
- Ability to adjust based on real-world conditions
- No unattended automated water discharge

## How Limited Inputs Affect Control

### Confidence Levels

The fuzzy controller calculates a **confidence level** based on available inputs:

| Valid Inputs | Confidence | Control Behavior |
|--------------|------------|------------------|
| 4 (all: TDS, Alk, Sulfite, pH) | **HIGH** | Full multi-parameter optimization |
| 2-3 | **MEDIUM** | Partial optimization, some assumptions |
| 0-1 | **LOW** | Limited control, mostly assumptions |

### Default Assumptions for Missing Inputs

When a parameter is unknown, the fuzzy controller assumes it's at the **Normal** (target) level:

```
Missing Input → Assumed "Normal" → Membership = 1.0 for Normal set
```

**Impact by parameter:**

| Missing Input | Assumption | Effect on Control |
|---------------|------------|-------------------|
| TDS | Normal (2500 ppm) | No blowdown recommendation, dosing unaffected |
| Alkalinity | Normal (300 ppm) | No caustic adjustment, blowdown unaffected |
| Sulfite | Normal (30 ppm) | Maintenance sulfite dosing only |
| pH | Normal (11.0) | No acid/caustic pH correction |

### Rule Activation with Limited Inputs

**Example 1: TDS Only (LOW confidence)**

Only TDS-based rules fire with full strength:
```
Rule 1: IF TDS=VeryHigh THEN Blowdown=VeryHigh  ✓ Active
Rule 2: IF TDS=High THEN Blowdown=High          ✓ Active
Rule 6: IF Alk=VeryLow THEN Caustic=VeryHigh    ✗ Alk assumed Normal
Rule 11: IF Sulfite=VeryLow THEN Sulfite=VeryHigh ✗ Sulfite assumed Normal
```

Result: Blowdown recommendation works, but chemical dosing stays at maintenance levels.

**Example 2: TDS + Alkalinity (MEDIUM confidence)**

```
Inputs: TDS=2800 (High=0.7), Alk=180 (Low=0.8), Sulfite=Unknown

Active Rules:
- Rule 2: IF TDS=High THEN Blowdown=High (0.7)
- Rule 7: IF Alk=Low THEN Caustic=High (0.8)
- Rule 13: IF Sulfite=Normal THEN Sulfite=Low (1.0) ← maintenance

Result (Recommendations):
- Blowdown: 58% (responding to high TDS)
- Caustic: 72% (responding to low alkalinity)
- Sulfite: 25% (maintenance level, unknown actual need)
```

**Example 3: All Inputs (HIGH confidence)**

```
Inputs: TDS=2800 (High=0.7), Alk=180 (Low=0.8),
        Sulfite=15 (VeryLow=0.9), pH=10.8 (Normal=0.6)

Active Rules (partial list):
- Rule 2: Blowdown=High (0.7)
- Rule 7: Caustic=High (0.8)
- Rule 11: Sulfite=VeryHigh (0.9)
- Rule 22: IF TDS=Low AND Alk=Low THEN Caustic=High (0.0 - not matching)

Result (Recommendations):
- Blowdown: 58% (operator controls valve manually)
- Caustic: 72%
- Sulfite: 85% (correctly responding to very low sulfite!)
- Acid: 5% (pH is normal)
```

## Recommended Testing Schedule

### Minimum (LOW confidence operation)
- Temperature: Automatic ✓
- Weekly: TDS, pH, Alkalinity, Sulfite checks

### Standard (MEDIUM confidence operation)
- Temperature: Automatic ✓
- Daily: TDS, Alkalinity check
- Every 2-3 days: Sulfite, pH

### Optimal (HIGH confidence operation)
- Temperature: Automatic ✓
- Daily: TDS, Alkalinity, Sulfite, pH
- Or: Install inline analyzers for continuous monitoring

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
│  Sensor Reading                     │
│  ┌─────────────────────────────┐   │
│  │        Temperature          │   │
│  │           42.3              │   │
│  │            °C               │   │
│  └─────────────────────────────┘   │
│                                     │
│  Enter Test Results                 │
│  ┌─────────────────────────────┐   │
│  │ TDS (ppm)                   │   │
│  │ [   2847   ]         1h ago │   │
│  │                              │   │
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
│  │ Blowdown REC███████░░  32% │   │
│  │ Caustic     ██░░░░░░░░  12% │   │
│  │ Sulfite     ███████░░░  48% │   │
│  │ Acid        ░░░░░░░░░░   0% │   │
│  └─────────────────────────────┘   │
│  Active Rules: 8                    │
└─────────────────────────────────────┘
```

> **Note:** The "REC" badge on Blowdown indicates this is a recommendation only.
> The operator must manually control the blowdown valve.

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
  -d '{"tds": 2847, "alkalinity": 285, "sulfite": 32, "ph": 10.9}'
```

**Example: Submit TDS only**
```bash
curl -X POST http://192.168.1.100/api/tests \
  -H "Content-Type: application/json" \
  -d '{"tds": 2650}'
```

## Safety Considerations

### With Limited Inputs

1. **Over-treatment risk**: Unknown low sulfite → actual corrosion risk
2. **Under-treatment risk**: Unknown high alkalinity → scale formation
3. **Chemical waste**: Maintenance dosing when not needed
4. **TDS buildup**: Without regular TDS testing, scale can form before detection

### Recommendations

- Never operate at LOW confidence for extended periods
- If unable to test daily, consider conservative setpoints
- Test TDS at least daily to maintain proper blowdown cycles
- Keep a testing log to track trends over time

### Manual Blowdown Safety

Since blowdown is manually controlled:
- Always verify boiler level before blowdown
- Never leave blowdown valve unattended while open
- Follow the recommendation percentage as a guide
- Consider water conditions when determining blowdown duration

## Troubleshooting

### "Confidence: LOW" persistently
- Check if test values were entered (TDS, Alkalinity, Sulfite, pH)
- Check if values expired (see age indicator next to each field)
- Need at least 2 valid inputs for MEDIUM confidence
- Verify web interface can reach controller

### Chemical dosing seems wrong
- Compare test values to setpoints in "Target Ranges" section
- Check if rules are firing as expected (Active Rules count)
- With limited inputs, controller may be making incorrect assumptions
- Ensure TDS value is entered - it affects blowdown recommendations

### Blowdown recommendation doesn't match expectations
- Remember: blowdown is manual - the value is only a recommendation
- Check TDS value is current and accurate
- High TDS should show high blowdown recommendation
- Low TDS should show low/zero blowdown recommendation

### Test values not updating
- Clear browser cache
- Check controller WiFi connection
- Verify POST request succeeds (check browser console)
- Ensure values are within valid ranges (TDS: 0-5000, Alk: 0-1000, etc.)

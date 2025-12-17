# Standard Operating Procedure
## Water Testing and Controller Monitoring

**Document Number:** SOP-BDC-001
**Revision:** 1.0
**Effective Date:** December 2024
**Equipment:** Columbia CT-6 Boiler Dosing Controller

---

## 1. Purpose

This procedure establishes the standard method for:
- Collecting boiler water samples
- Performing water chemistry tests
- Entering test results into the dosing controller
- Monitoring system operation and responding to alarms

## 2. Scope

This SOP applies to all personnel responsible for:
- Boiler water testing
- Chemical treatment monitoring
- Dosing controller operation

## 3. Responsibilities

| Role | Responsibilities |
|------|------------------|
| **Boiler Operator** | Daily monitoring, sample collection, test entry |
| **Water Treatment Tech** | Weekly testing, calibration, troubleshooting |
| **Maintenance** | Equipment repair, probe replacement |
| **Supervisor** | SOP compliance verification, training |

## 4. Safety Requirements

### Personal Protective Equipment (PPE)

| Task | Required PPE |
|------|--------------|
| Sample collection | Safety glasses, heat-resistant gloves |
| Chemical testing | Safety glasses, chemical-resistant gloves, lab coat |
| Chemical handling | Safety glasses, face shield, chemical apron, gloves |

### Safety Precautions

- Never collect samples when boiler is at maximum pressure
- Allow sample line to flush for 30 seconds before collection
- Handle test chemicals according to their SDS
- Wash hands after handling samples or chemicals
- Report spills immediately

---

## 5. Required Materials

### Sample Collection
- [ ] Sample cooler (if applicable)
- [ ] Clean sample bottles (2x 250 ml minimum)
- [ ] Heat-resistant gloves
- [ ] Sample log sheet

### Water Testing
- [ ] TDS/Conductivity meter (or test kit)
- [ ] Alkalinity test kit (titration)
- [ ] Sulfite test kit (titration or colorimetric)
- [ ] pH meter or test strips
- [ ] Deionized water for rinsing
- [ ] Test log sheet

### Controller Entry
- [ ] Smartphone/tablet with web browser, OR
- [ ] Access to LCD menu buttons

---

## 6. Procedure: Daily Monitoring

### 6.1 Visual Inspection (Every Shift)

**Time Required:** 5 minutes

| Step | Action | Verify |
|------|--------|--------|
| 1 | Observe LCD main screen | No alarm messages displayed |
| 2 | Check status LED | Green = normal operation |
| 3 | Note temperature reading | Within 40-50°C operating range |
| 4 | Note conductivity reading | Within 2000-3000 µS/cm target |
| 5 | Check pump status indicators | Pumps operating as expected |
| 6 | Check chemical tank levels | Adequate supply (>25% minimum) |
| 7 | Record readings in log | Complete daily log entry |

### 6.2 Web Interface Check (Once Per Shift)

**Time Required:** 2 minutes

1. Open browser to `http://<controller-ip>/`
2. Verify connection status shows "Connected"
3. Check confidence level:
   - **HIGH** - All tests current (good)
   - **MEDIUM** - Some tests stale (acceptable)
   - **LOW** - Tests needed (action required)
4. Review control recommendations
5. Note any unusual readings

---

## 7. Procedure: Water Sample Collection

### 7.1 Sample Point Preparation

**Time Required:** 5 minutes

| Step | Action | Notes |
|------|--------|-------|
| 1 | Locate boiler sample valve | Usually on water column or blowdown line |
| 2 | Wear heat-resistant gloves | Sample water is HOT |
| 3 | Place container under valve | Use heat-resistant container |
| 4 | Open valve slowly | Allow steam/water to discharge |
| 5 | Flush for 30 seconds | Ensures fresh sample from boiler |
| 6 | Reduce flow to steady stream | Not full blast |

### 7.2 Sample Collection

| Step | Action | Notes |
|------|--------|-------|
| 1 | Rinse sample bottle 2-3 times | With boiler water |
| 2 | Fill bottle to 80% capacity | Leave headspace |
| 3 | Close valve | Securely |
| 4 | Allow sample to cool | To <50°C for safe handling |
| 5 | Label bottle | Date, time, sample point |
| 6 | Collect second sample | If backup needed |

### 7.3 Sample Handling

- Test within 4 hours of collection
- Do not refrigerate (affects chemistry)
- Keep samples away from chemicals
- Dispose of untested samples properly

---

## 8. Procedure: Water Chemistry Testing

### 8.1 TDS / Conductivity Test

**Time Required:** 2 minutes

| Step | Action | Target |
|------|--------|--------|
| 1 | Turn on TDS/conductivity meter | Wait for stabilization |
| 2 | Rinse probe with sample water | 2-3 times |
| 3 | Immerse probe in sample | Full immersion |
| 4 | Wait for reading to stabilize | Usually 10-30 seconds |
| 5 | Record TDS reading | **2000-3000 ppm** |
| 6 | Record conductivity if available | **2500-4500 µS/cm** |
| 7 | Rinse probe with DI water | Before storage |

**Troubleshooting:**
- Reading too high: May indicate TDS buildup, increase blowdown
- Reading too low: Check probe calibration, sample freshness
- Erratic reading: Clean or replace probe

### 8.2 Alkalinity Test (P & M Alkalinity)

**Time Required:** 5-10 minutes

| Step | Action | Target |
|------|--------|--------|
| 1 | Measure sample volume | Per test kit (usually 50-100 ml) |
| 2 | Add phenolphthalein indicator | Should turn pink if P-Alk present |
| 3 | Titrate with acid to colorless | Record P-Alkalinity drops |
| 4 | Add methyl orange indicator | Should turn yellow |
| 5 | Continue titrating to salmon | Record total drops |
| 6 | Calculate M-Alkalinity | Per kit instructions |
| 7 | Record total alkalinity | **200-400 ppm as CaCO₃** |

**Calculation Example (typical kit):**
```
Total Alkalinity (ppm) = Drops × 20
Example: 15 drops × 20 = 300 ppm ✓
```

### 8.3 Sulfite Residual Test

**Time Required:** 3-5 minutes

| Step | Action | Target |
|------|--------|--------|
| 1 | Measure sample volume | Per test kit (usually 25-50 ml) |
| 2 | Add reagent A | As directed |
| 3 | Add reagent B | As directed |
| 4 | Mix and wait | Per kit timing |
| 5 | Compare to color chart | Or titrate per kit |
| 6 | Record sulfite reading | **20-40 ppm SO₃** |

**Note:** Test sulfite immediately after collection - it degrades quickly in air.

### 8.4 pH Test

**Time Required:** 2 minutes

| Step | Action | Target |
|------|--------|--------|
| 1 | Turn on pH meter | Calibrate if needed |
| 2 | Rinse probe with sample | 2-3 times |
| 3 | Immerse probe in sample | Past reference junction |
| 4 | Wait for stabilization | 30-60 seconds |
| 5 | Record pH value | **10.5-11.5** |
| 6 | Rinse with DI water | Before storage |

**Alternative (test strips):**
| Step | Action |
|------|--------|
| 1 | Dip strip in sample for 1-2 seconds |
| 2 | Remove and wait (per package) |
| 3 | Compare to color chart |
| 4 | Record reading |

---

## 9. Procedure: Entering Test Results

### 9.1 Method A: Web Interface (Recommended)

**Time Required:** 2 minutes

| Step | Action | Screenshot Reference |
|------|--------|---------------------|
| 1 | Open browser on phone/tablet | - |
| 2 | Navigate to `http://<controller-ip>/` | Main screen loads |
| 3 | Scroll to "Enter Test Results" section | Below temperature display |
| 4 | Enter TDS value in ppm | e.g., `2847` |
| 5 | Enter Alkalinity value in ppm | e.g., `285` |
| 6 | Enter Sulfite value in ppm | e.g., `32` |
| 7 | Enter pH value | e.g., `10.9` |
| 8 | Press **Submit Tests** button | Green confirmation appears |
| 9 | Verify confidence shows **HIGH** | If all 4 tests entered |
| 10 | Observe updated recommendations | Bars update automatically |

**Tips:**
- You can enter partial data (not all fields required)
- Age indicator shows time since each test
- Values persist until cleared or expired (8 hours default)

### 9.2 Method B: LCD Menu

**Time Required:** 3-5 minutes

| Step | Button | Action |
|------|--------|--------|
| 1 | Press **MENU** | Enter main menu |
| 2 | Press **▼** twice | Navigate to "Manual Test Input" |
| 3 | Press **ENTER** | Open test input menu |
| 4 | Press **ENTER** on "TDS" | Edit TDS value |
| 5 | Use **▲/▼** to adjust | Or hold for fast scroll |
| 6 | Press **ENTER** | Confirm value |
| 7 | Repeat for Alkalinity, Sulfite, pH | Same process |
| 8 | Press **MENU** | Return to main screen |
| 9 | Verify readings updated | Check fuzzy output display |

### 9.3 Method C: API (Automated Systems)

For integration with LIMS or automated testing:

```bash
# Submit all test results
curl -X POST http://<controller-ip>/api/tests \
  -H "Content-Type: application/json" \
  -d '{"tds": 2847, "alkalinity": 285, "sulfite": 32, "ph": 10.9}'

# Submit single parameter
curl -X POST http://<controller-ip>/api/tests \
  -H "Content-Type: application/json" \
  -d '{"tds": 2650}'

# Clear all values
curl -X DELETE http://<controller-ip>/api/tests
```

---

## 10. Procedure: Responding to Control Recommendations

### 10.1 Interpreting Fuzzy Output

After entering test data, observe the control recommendations:

| Output | Reading | Action |
|--------|---------|--------|
| **Blowdown (REC)** | >50% | Consider manual blowdown |
| **Caustic** | >50% | Verify NaOH pump is dosing |
| **Sulfite** | >50% | Verify sulfite pump is dosing |
| **Acid** | >50% | Verify acid pump is dosing (rare) |

### 10.2 Blowdown Decision Process

**Remember: Blowdown is MANUAL - you control the valve!**

```
┌─────────────────────────────────────────┐
│ CHECK BLOWDOWN RECOMMENDATION           │
├─────────────────────────────────────────┤
│                                         │
│  Recommendation < 20%?                  │
│     └─► NO BLOWDOWN NEEDED             │
│                                         │
│  Recommendation 20-50%?                 │
│     └─► BRIEF BLOWDOWN (30-60 sec)     │
│         1. Verify water level OK        │
│         2. Open blowdown valve          │
│         3. Monitor for 30-60 seconds    │
│         4. Close valve                  │
│                                         │
│  Recommendation > 50%?                  │
│     └─► EXTENDED BLOWDOWN (1-2 min)    │
│         1. Verify water level OK        │
│         2. Open blowdown valve          │
│         3. Monitor water level          │
│         4. Close when level drops       │
│         5. Allow refill                 │
│         6. Retest in 30 minutes         │
│                                         │
│  Recommendation > 80%?                  │
│     └─► INVESTIGATE CAUSE              │
│         - Check feedwater source        │
│         - Verify chemical dosing        │
│         - Consider calling supervisor   │
└─────────────────────────────────────────┘
```

---

## 11. Procedure: Responding to Alarms

### 11.1 Alarm Response Matrix

| Alarm | Immediate Action | Follow-up |
|-------|-----------------|-----------|
| **HIGH CONDUCTIVITY** | Check if blowdown needed | Test water, adjust treatment |
| **LOW CONDUCTIVITY** | Verify probe is working | Check for dilution source |
| **BLOWDOWN TIMEOUT** | Check valve is closed | Reduce blowdown duration |
| **FEED TIMEOUT** | Check chemical supply | Verify pump calibration |
| **NO FLOW** | Check boiler operation | Inspect flow switch |
| **SENSOR ERROR** | Check probe connections | Clean or replace probe |
| **TEMP ERROR** | Check RTD connections | Verify temperature manually |
| **LOW CONFIDENCE** | Enter current test data | See Section 9 |

### 11.2 Alarm Acknowledgment

| Step | Action |
|------|--------|
| 1 | Note alarm type and time |
| 2 | Take immediate action per matrix |
| 3 | Press **ENTER** to acknowledge (if required) |
| 4 | Record alarm and action in log |
| 5 | Verify alarm clears after correction |
| 6 | Report recurring alarms to supervisor |

---

## 12. Testing Schedule

### 12.1 Recommended Testing Frequency

| Confidence Level | Testing Schedule | Test Parameters |
|------------------|------------------|-----------------|
| **LOW** (minimum) | Weekly | TDS, Alk, Sulfite, pH |
| **MEDIUM** (standard) | Daily: TDS, Alk; Every 2-3 days: Sulfite, pH |
| **HIGH** (optimal) | Daily | All four parameters |

### 12.2 Weekly Testing Schedule

| Day | Tests Required | Additional Tasks |
|-----|----------------|------------------|
| **Monday** | TDS, Alkalinity, Sulfite, pH | Full chemistry panel |
| **Tuesday** | TDS, Alkalinity | |
| **Wednesday** | TDS, Alkalinity | Check chemical levels |
| **Thursday** | TDS, Alkalinity, Sulfite, pH | Mid-week full panel |
| **Friday** | TDS, Alkalinity | |
| **Saturday** | TDS | Minimal (if weekend operation) |
| **Sunday** | TDS | Minimal (if weekend operation) |

---

## 13. Documentation

### 13.1 Required Records

| Record | Frequency | Retention |
|--------|-----------|-----------|
| Daily monitoring log | Each shift | 1 year |
| Water test results | Each test | 3 years |
| Alarm log | As they occur | 1 year |
| Calibration records | Each calibration | 3 years |
| Maintenance records | As performed | Equipment life |

### 13.2 Sample Log Entry

```
┌─────────────────────────────────────────────────────────────┐
│ BOILER WATER TEST LOG                                       │
├─────────────────────────────────────────────────────────────┤
│ Date: ____________  Time: ____________  Operator: _________ │
├─────────────────────────────────────────────────────────────┤
│ SAMPLE INFORMATION                                          │
│   Sample Point: ☐ Boiler  ☐ Feedwater  ☐ Condensate        │
│   Boiler Pressure: ________ PSI                            │
│   Boiler Temp: ________ °C                                 │
├─────────────────────────────────────────────────────────────┤
│ TEST RESULTS                      Target          Actual    │
│   TDS (ppm):                    2000-3000        _______    │
│   Alkalinity (ppm CaCO₃):       200-400          _______    │
│   Sulfite (ppm SO₃):            20-40            _______    │
│   pH:                           10.5-11.5        _______    │
├─────────────────────────────────────────────────────────────┤
│ CONTROLLER DATA (from Web UI or LCD)                        │
│   Confidence Level:  ☐ HIGH  ☐ MEDIUM  ☐ LOW               │
│   Blowdown Rec: _______%                                    │
│   Active Rules: _______                                     │
│   Any Alarms: ☐ No  ☐ Yes: _____________________________   │
├─────────────────────────────────────────────────────────────┤
│ ACTIONS TAKEN                                               │
│   ☐ Tests entered to controller                            │
│   ☐ Blowdown performed: _______ seconds                    │
│   ☐ Chemical tank refilled: _______                        │
│   ☐ Other: ____________________________________________    │
├─────────────────────────────────────────────────────────────┤
│ Notes: ____________________________________________________│
│ ___________________________________________________________│
└─────────────────────────────────────────────────────────────┘
```

---

## 14. Training Requirements

### 14.1 Initial Training

| Topic | Duration | Instructor |
|-------|----------|------------|
| SOP review | 1 hour | Supervisor |
| Water sampling technique | 1 hour | Water Treatment Tech |
| Test kit operation | 2 hours | Water Treatment Tech |
| Controller operation (LCD) | 1 hour | Maintenance |
| Controller operation (Web) | 30 min | Maintenance |
| Alarm response | 1 hour | Supervisor |

### 14.2 Refresher Training

- Annual SOP review (all operators)
- Equipment changes (as needed)
- After significant procedural errors

---

## 15. Revision History

| Rev | Date | Description | Author |
|-----|------|-------------|--------|
| 1.0 | Dec 2024 | Initial release | - |

---

## Appendix A: Quick Reference Card

```
┌─────────────────────────────────────────────────────────────┐
│                    QUICK REFERENCE                          │
├─────────────────────────────────────────────────────────────┤
│  TARGET RANGES                                              │
│    TDS:         2000 - 3000 ppm                            │
│    Alkalinity:  200 - 400 ppm CaCO₃                        │
│    Sulfite:     20 - 40 ppm SO₃                            │
│    pH:          10.5 - 11.5                                │
├─────────────────────────────────────────────────────────────┤
│  WEB INTERFACE                                              │
│    URL: http://<controller-ip>/                            │
│    Enter tests → Submit → Check confidence = HIGH          │
├─────────────────────────────────────────────────────────────┤
│  CONFIDENCE LEVELS                                          │
│    HIGH:   All 4 tests entered and current                 │
│    MEDIUM: 2-3 tests entered                               │
│    LOW:    0-1 tests (ACTION REQUIRED)                     │
├─────────────────────────────────────────────────────────────┤
│  BLOWDOWN (MANUAL VALVE!)                                   │
│    <20%:  No action needed                                 │
│    20-50%: Brief blowdown (30-60 sec)                      │
│    >50%:  Extended blowdown (1-2 min)                      │
│    >80%:  Investigate cause                                │
├─────────────────────────────────────────────────────────────┤
│  EMERGENCY CONTACTS                                         │
│    Supervisor: _______________________                      │
│    Maintenance: _______________________                     │
│    Water Treatment: _______________________                 │
└─────────────────────────────────────────────────────────────┘
```

---

## Appendix B: Troubleshooting Quick Guide

| Problem | Check First | Then Check |
|---------|-------------|------------|
| Can't connect to web UI | WiFi connection | IP address correct |
| Confidence stays LOW | Enter all 4 tests | Tests not expired |
| High TDS won't decrease | Blowdown valve open | Feedwater quality |
| Pump not dosing | HOA in AUTO | Chemical tank level |
| Erratic readings | Probe cleanliness | Probe calibration |
| Stale test warning | Re-enter current tests | Timeout setting |

---

*This SOP must be reviewed annually and updated as needed.*
*Unauthorized modifications to this procedure are prohibited.*

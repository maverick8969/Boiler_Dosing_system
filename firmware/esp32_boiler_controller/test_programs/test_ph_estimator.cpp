/**
 * @file test_ph_estimator.cpp
 * @brief Unit tests for pH-from-alkalinity estimator (per Estimating Boiler Water pH from Alkali.md)
 *
 * Test vectors from reference:
 *   P=300, M=500 -> CI=100 -> pH ~11.30
 *   P=200, M=200 -> CI=200 -> pH ~11.60
 *   P=150, M=400 -> CI=-100 -> unreliable (return false)
 */

#include <Arduino.h>
#include "ph_estimator.h"

#define ASSERT_NEAR(a, b, tol) do { \
    float _a = (a), _b = (b), _t = (tol); \
    if (fabsf(_a - _b) > _t) { \
        Serial.printf("FAIL: %.4f not near %.4f (tol %.4f)\n", _a, _b, _t); \
        failed++; \
    } else { passed++; } \
} while(0)

#define ASSERT_TRUE(x) do { if (x) passed++; else { Serial.println("FAIL: expected true"); failed++; } } while(0)
#define ASSERT_FALSE(x) do { if (!(x)) passed++; else { Serial.println("FAIL: expected false"); failed++; } } while(0)

static int passed = 0;
static int failed = 0;

void run_ph_estimator_tests() {
    Serial.println("\n=== pH Estimator Unit Tests ===\n");

    float out_pH = 0.0f;
    char status[PH_ESTIMATOR_STATUS_MAX];

    // Test 1: P=300, M=500 -> CI=100 -> pH ~11.30 (reference worked example)
    Serial.println("Test 1: P=300, M=500 (expect pH ~11.30)");
    bool ok = estimate_pH_from_alkalinity(300.0f, 500.0f, &out_pH, status, sizeof(status));
    ASSERT_TRUE(ok);
    ASSERT_NEAR(out_pH, 11.30f, 0.05f);
    Serial.printf("  pH=%.2f status=%s\n\n", out_pH, status);

    // Test 2: P=200, M=200 -> CI=200 -> pH ~11.60
    Serial.println("Test 2: P=200, M=200 (expect pH ~11.60)");
    ok = estimate_pH_from_alkalinity(200.0f, 200.0f, &out_pH, status, sizeof(status));
    ASSERT_TRUE(ok);
    ASSERT_NEAR(out_pH, 11.60f, 0.05f);
    Serial.printf("  pH=%.2f status=%s\n\n", out_pH, status);

    // Test 3: P=150, M=400 -> 2P-M=-100 -> unreliable
    Serial.println("Test 3: P=150, M=400 (expect unreliable, return false)");
    ok = estimate_pH_from_alkalinity(150.0f, 400.0f, &out_pH, status, sizeof(status));
    ASSERT_FALSE(ok);
    Serial.printf("  status=%s\n\n", status);

    // Test 4: Caustic index helper
    Serial.println("Test 4: ph_estimator_caustic_index");
    float ci1 = ph_estimator_caustic_index(300.0f, 500.0f);
    float ci2 = ph_estimator_caustic_index(150.0f, 400.0f);
    ASSERT_NEAR(ci1, 100.0f, 0.1f);
    ASSERT_NEAR(ci2, -100.0f, 0.1f);
    Serial.printf("  CI(300,500)=%.0f CI(150,400)=%.0f\n\n", ci1, ci2);

    // Test 5: Invalid inputs
    Serial.println("Test 5: Invalid inputs (negative, out of range)");
    ASSERT_FALSE(estimate_pH_from_alkalinity(-1.0f, 100.0f, &out_pH, nullptr, 0));
    ASSERT_FALSE(estimate_pH_from_alkalinity(100.0f, -1.0f, &out_pH, nullptr, 0));
    ASSERT_FALSE(estimate_pH_from_alkalinity(2500.0f, 100.0f, &out_pH, nullptr, 0));  // P > 2000
    Serial.println();

    Serial.println("----------------------------------------");
    Serial.printf("Result: %d passed, %d failed\n", passed, failed);
    Serial.println("========================================\n");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("pH Estimator Test Program");
    Serial.println("Reference: Estimating Boiler Water pH from Alkali.md");
    Serial.println();

    run_ph_estimator_tests();
}

void loop() {
    delay(10000);
}

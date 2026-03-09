/**
 * @file ph_estimator.cpp
 * @brief Estimate boiler water pH from P- and M-alkalinity per reference .md
 */

#include "ph_estimator.h"
#include <math.h>
#include <string.h>

// 1 meq/L = 50 ppm as CaCO3; [OH-] (mol/L) = CI / 50000
static const float OH_MOL_PER_50PPM = 1.0f / 50000.0f;
// Minimum [OH-] (mol/L) before log to avoid -inf
static const float OH_MIN_MOL = 1e-12f;

static bool is_valid_float(float x) {
    return (x == x) && (x >= -1e9f) && (x <= 1e9f);  // reject NaN and Inf
}

float ph_estimator_caustic_index(float P_ppm, float M_ppm) {
    if (!is_valid_float(P_ppm) || !is_valid_float(M_ppm)) return -1.0f;
    if (P_ppm < 0 || M_ppm < 0) return -1.0f;
    if (P_ppm > PH_ESTIMATOR_P_MAX || M_ppm > PH_ESTIMATOR_M_MAX) return -1.0f;
    return 2.0f * P_ppm - M_ppm;
}

bool estimate_pH_from_alkalinity(float P_ppm, float M_ppm, float* out_pH,
                                 char* out_status, size_t status_len) {
    if (out_pH) *out_pH = 0.0f;

    if (out_status && status_len > 0) out_status[0] = '\0';

    if (!is_valid_float(P_ppm) || !is_valid_float(M_ppm)) {
        if (out_status && status_len > 0) strncpy(out_status, "invalid", status_len - 1);
        return false;
    }
    if (P_ppm < 0 || M_ppm < 0) {
        if (out_status && status_len > 0) strncpy(out_status, "invalid", status_len - 1);
        return false;
    }
    if (P_ppm > PH_ESTIMATOR_P_MAX || M_ppm > PH_ESTIMATOR_M_MAX) {
        if (out_status && status_len > 0) strncpy(out_status, "out_of_range", status_len - 1);
        return false;
    }

    float ci = 2.0f * P_ppm - M_ppm;
    if (ci <= 0.0f) {
        if (out_status && status_len > 0) strncpy(out_status, "unreliable", status_len - 1);
        return false;
    }

    // [OH-] = CI / 50000 (mol/L); clamp to avoid log(0)
    float oh_mol = ci * OH_MOL_PER_50PPM;
    if (oh_mol < OH_MIN_MOL) oh_mol = OH_MIN_MOL;

    // pH = 14 + log10([OH-]); cap at 14.0
    float pH = 14.0f + log10f(oh_mol);
    if (pH > 14.0f) pH = 14.0f;
    if (pH < 0.0f) pH = 0.0f;

    if (out_pH) *out_pH = pH;
    if (out_status && status_len > 0) strncpy(out_status, "ok", status_len - 1);

    return true;
}

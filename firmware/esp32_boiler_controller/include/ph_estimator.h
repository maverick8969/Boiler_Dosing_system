/**
 * @file ph_estimator.h
 * @brief Estimate boiler water pH from P- and M-alkalinity (ppm as CaCO3)
 *
 * Implements the method from "Estimating Boiler Water pH from Alkali.md":
 *   pH ≈ 14 + log10((2*P - M) / 50000) when 2*P - M > 0.
 * Assumes cooled sample near 25°C; hydroxide-dominant alkalinity.
 */

#ifndef PH_ESTIMATOR_H
#define PH_ESTIMATOR_H

#include <Arduino.h>

// Optional max length for status string (e.g. "ok", "unreliable", "invalid")
#define PH_ESTIMATOR_STATUS_MAX 32

// Sane input range (ppm as CaCO3); inputs outside are rejected
#define PH_ESTIMATOR_P_MAX 2000.0f
#define PH_ESTIMATOR_M_MAX 2000.0f

/**
 * @brief Estimate pH from P- and M-alkalinity (ppm as CaCO3).
 *
 * @param P_ppm    P-alkalinity (phenolphthalein endpoint), ppm as CaCO3
 * @param M_ppm    M-alkalinity (methyl orange / total), ppm as CaCO3
 * @param out_pH   Output: estimated pH (only set when return is true)
 * @param out_status Optional: buffer for status string ("ok", "unreliable", "invalid"); may be nullptr
 * @param status_len Length of out_status buffer (ignored if out_status is nullptr)
 * @return true if 2*P - M > 0 and estimate was computed; false otherwise (unreliable or invalid)
 */
bool estimate_pH_from_alkalinity(float P_ppm, float M_ppm, float* out_pH,
                                 char* out_status = nullptr, size_t status_len = PH_ESTIMATOR_STATUS_MAX);

/**
 * @brief Compute caustic index only (2*P - M), ppm as CaCO3.
 * @return Caustic index, or negative if invalid inputs
 */
float ph_estimator_caustic_index(float P_ppm, float M_ppm);

#endif // PH_ESTIMATOR_H

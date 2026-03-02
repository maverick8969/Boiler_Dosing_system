/**
 * @file device_identity.h
 * @brief Stable device identifier for MQTT topics and telemetry (Modern IoT Stack)
 *
 * Provides a unique device_id per unit (e.g. MAC-derived) for device/{id}/...
 * topic namespace and payloads. Does not depend on NVS; survives factory reset.
 */

#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get stable device ID string (e.g. 12-char hex from MAC).
 * @param buf Output buffer
 * @param buf_len Size of buf (use DEVICE_ID_MAX_LEN)
 * @return buf (same pointer) for convenience
 */
char* device_id_get(char* buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_IDENTITY_H */

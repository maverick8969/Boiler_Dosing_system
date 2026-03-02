/**
 * @file device_identity.cpp
 * @brief Device ID from WiFi MAC (Modern IoT Stack)
 */

#include "device_identity.h"
#include <WiFi.h>
#include <cstdio>

char* device_id_get(char* buf, size_t buf_len) {
    if (buf == nullptr || buf_len < 2) {
        if (buf && buf_len > 0) buf[0] = '\0';
        return buf;
    }
    uint8_t mac[6];
    WiFi.macAddress(mac);
    // 12 hex chars + null
    snprintf(buf, buf_len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

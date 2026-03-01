/**
 * @file c3_pin_definitions.h
 * @brief Backward-compatibility include for panel pin definitions.
 *
 * Use panel_pin_definitions.h for new code. This file includes it so that
 * existing panel/coprocessor code (stub, test_c3_io) continues to build.
 * The "C3" name is legacy (XIAO ESP32-C3); the panel board is now ESP32 DevKit.
 */

#ifndef C3_PIN_DEFINITIONS_H
#define C3_PIN_DEFINITIONS_H

#include "panel_pin_definitions.h"

#endif // C3_PIN_DEFINITIONS_H

/**
 * @file runtime_state.h
 * @brief Persistent runtime state storage for SunSense
 */

#ifndef RUNTIME_STATE_H
#define RUNTIME_STATE_H

#include "system_types.h"

#include <stdbool.h>
#include <stdint.h>

#define RUNTIME_STATE_SCHEMA_VERSION 1U

typedef struct {
    bool initialized;
} RuntimeStateController_t;

typedef struct {
    uint8_t state_version;
    bool position_valid;
    uint8_t position_percent;
    OperatingMode_t mode;
    LightLevel_t light_level;
    bool slat_angle_valid;
    uint8_t slat_angle_deg;
} RuntimeStateSnapshot_t;

bool runtime_state_init(RuntimeStateController_t *controller);
bool runtime_state_load(RuntimeStateController_t *controller, RuntimeStateSnapshot_t *snapshot);
bool runtime_state_save(RuntimeStateController_t *controller, const RuntimeStateSnapshot_t *snapshot);

#endif /* RUNTIME_STATE_H */

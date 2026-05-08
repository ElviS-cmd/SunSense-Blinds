/**
 * @file app_types.h
 * @brief App-level state snapshots shared by main, tasks, and MQTT.
 */
#pragma once

#include <stdint.h>
#include "system_types.h"

extern "C" {
    #include "ldr_controller.h"
    #include "motor_controller.h"
}

typedef struct {
    uint32_t uptime_ms;
    OperatingMode_t current_mode;
    MotorState_t motor_state;
    LightLevel_t light_level;
    bool system_healthy;
} SystemState_t;

typedef struct {
    SystemState_t system;
    uint16_t ldr_raw;
    uint16_t ldr_filtered;
    float encoder_percent;
    float servo_angle;
} PublishSnapshot_t;

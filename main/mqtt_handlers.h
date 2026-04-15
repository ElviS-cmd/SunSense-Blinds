/**
 * @file mqtt_handlers.h
 * @brief MQTT command handlers and state publishing declarations
 */
#pragma once

#include <stdint.h>
#include "esp_event.h"
#include "system_types.h"

extern "C" {
    #include "motor_controller.h"
    #include "servo_controller.h"
    #include "mode_controller.h"
    #include "ldr_controller.h"
    #include "led_controller.h"
}

/* ============================================================================
 * TYPES
 * ========================================================================== */

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

/* ============================================================================
 * SHARED STATE (defined in main.cpp)
 * ========================================================================== */

extern MotorController_t   motor;
extern ServoController_t   servo;
extern ModeController_t    mode;
extern LDRController_t     ldr;
extern SystemState_t       system_state;
extern SystemHealth_t      system_health;
extern SystemConfig_t      system_config;
extern bool                auto_command_pending;
extern bool                manual_next_open;

/* ============================================================================
 * FUNCTIONS DEFINED IN main.cpp
 * ========================================================================== */

bool lock_state(void);
void unlock_state(void);
void apply_manual_mode_locked(uint32_t current_time);
void collect_publish_snapshot_locked(PublishSnapshot_t *snapshot);
void stop_motor_locked(uint32_t current_time);
bool command_slat_locked(float angle,
                         uint32_t current_time,
                         bool force_reapply,
                         const char *reason);
void begin_open_sequence_locked(uint32_t current_time);
void begin_close_sequence_locked(uint32_t current_time);
void set_position_locked(uint8_t percent);
void request_led_status_event(LEDStatusPattern_t pattern);
void request_led_network_status_event(void);

/* ============================================================================
 * FUNCTIONS DEFINED IN mqtt_handlers.cpp
 * ========================================================================== */

void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                        int32_t event_id, void *event_data);
void publish_state_if_ready(void);

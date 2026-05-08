/**
 * @file app_state.h
 * @brief Shared controller instances, mutex helpers, runtime state, and events.
 */
#pragma once

#include <stdint.h>
#include "app_types.h"

extern "C" {
    #include "button_controller.h"
    #include "led_controller.h"
    #include "ldr_controller.h"
    #include "microphone_controller.h"
    #include "mode_controller.h"
    #include "motor_controller.h"
    #include "servo_controller.h"
    #include "voice_command_controller.h"
}

extern ButtonController_t button;
extern ModeController_t mode;
extern MotorController_t motor;
extern LEDController_t led;
extern LDRController_t ldr;
extern ServoController_t servo;
extern MicrophoneController_t microphone;
extern VoiceCommandController_t voice;

extern SystemState_t system_state;
extern SystemHealth_t system_health;
extern SystemConfig_t system_config;
extern bool auto_command_pending;
extern bool manual_next_open;

bool sunsense_local_only_enabled(void);

bool initialize_state_sync(void);
bool lock_state(void);
void unlock_state(void);

const char *event_type_to_string(SystemEvent_t event_type);
bool queue_system_event(SystemEvent_t event_type,
                        uint32_t current_time,
                        uint16_t value_u16);
bool system_event_ready(void);
bool receive_system_event(SystemEvent_Queue_t *event);

void update_time_based_position_locked(uint32_t current_time);
bool get_position_snapshot_locked(uint8_t *position_percent);
void capture_current_position_as_motor_baseline_locked(void);
void request_runtime_save_locked(void);
void maybe_persist_runtime_state_locked(uint32_t current_time);
void restore_runtime_state(void);
void set_position_locked(uint8_t percent);
void mark_motor_travel_complete_locked(MotorState_t motor_state);

void set_auto_actions_enabled_after_ms(uint32_t current_time);
bool auto_actions_enabled(uint32_t current_time);

void request_led_status_event(LEDStatusPattern_t pattern);
void request_led_network_status_event(void);
LEDStatusPattern_t get_requested_led_pattern(void);

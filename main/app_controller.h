/**
 * @file app_controller.h
 * @brief Shared locked operations that coordinate mode, motor, servo, and light.
 */
#pragma once

#include <stdint.h>
#include "app_types.h"

bool command_slat_locked(float angle,
                         uint32_t current_time,
                         bool force_reapply,
                         const char *reason);
void update_servo_for_light_locked(uint32_t current_time);
void clear_control_sequence_locked(void);

void apply_manual_mode_locked(uint32_t current_time);
void collect_publish_snapshot_locked(PublishSnapshot_t *snapshot);
void stop_motor_locked(uint32_t current_time);
void begin_open_sequence_locked(uint32_t current_time);
void begin_close_sequence_locked(uint32_t current_time);
void process_system_event_locked(const SystemEvent_Queue_t *event);

/**
 * @file app_controller.cpp
 * @brief SunSense app behavior for button, light, voice, motor, and servo commands.
 */

#include "app_controller.h"

#include "esp_log.h"
#include "gpio_config.h"
#include "app_state.h"

static const char *TAG = "SunSense";

static bool servo_tilt_allowed_locked(const char *reason) {
    uint8_t position_percent = 0U;
    if (!get_position_snapshot_locked(&position_percent)) {
        ESP_LOGW(TAG, "Slat command allowed (%s): cover position unknown", reason);
        return true;
    }

    if (position_percent >= SERVO_TILT_BLOCKED_POSITION_MIN_PERCENT) {
        ESP_LOGW(TAG,
                 "Slat command blocked (%s): blinds are fully rolled up position=%u%%",
                 reason,
                 position_percent);
        return false;
    }

    return true;
}

static float clamp_slat_angle(float angle) {
    float min_angle = SERVO_SLAT_CLOSED_ANGLE;
    float max_angle = SERVO_SLAT_OPEN_ANGLE;

    if (min_angle > max_angle) {
        float tmp = min_angle;
        min_angle = max_angle;
        max_angle = tmp;
    }

    if (angle < min_angle) {
        return min_angle;
    }
    if (angle > max_angle) {
        return max_angle;
    }
    return angle;
}

void clear_control_sequence_locked(void) {
    /* Servo commands are immediate now; no deferred servo/motor sequence remains. */
}

bool command_slat_locked(float angle,
                         uint32_t current_time,
                         bool force_reapply,
                         const char *reason) {
    if (!system_health.servo_ok) {
        ESP_LOGW(TAG, "Slat command skipped (%s): servo unavailable", reason);
        return false;
    }

    if (motor_is_running(&motor)) {
        ESP_LOGW(TAG, "Slat command skipped (%s): motor is running", reason);
        return false;
    }

    if (!servo_tilt_allowed_locked(reason)) {
        return false;
    }

    angle = clamp_slat_angle(angle);
    if (force_reapply) {
        servo_move_to(&servo, angle, current_time);
    } else {
        servo_move_to_if_changed(&servo, angle, current_time);
    }
    request_runtime_save_locked();
    ESP_LOGI(TAG, "Slat command applied (%s): angle=%.1f", reason, angle);
    return true;
}

void update_servo_for_light_locked(uint32_t current_time) {
    if (mode_get_current(&mode) != MODE_AUTO) {
        return;
    }

    if (ldr_is_bright(&ldr)) {
        command_slat_locked(SERVO_SLAT_OPEN_ANGLE, current_time, false, "auto bright");
    } else if (ldr_is_dark(&ldr)) {
        command_slat_locked(SERVO_SLAT_CLOSED_ANGLE, current_time, false, "auto dark");
        request_runtime_save_locked();
    }
}

void apply_manual_mode_locked(uint32_t current_time) {
    mode_set_manual(&mode, current_time);
    system_state.current_mode = mode_get_current(&mode);
    request_runtime_save_locked();
}

void collect_publish_snapshot_locked(PublishSnapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    snapshot->system = system_state;
    snapshot->ldr_raw = system_health.ldr_ok ? ldr_get_raw(&ldr) : 0;
    snapshot->ldr_filtered = system_health.ldr_ok ? ldr_get_filtered(&ldr) : 0;
    uint8_t position_percent = 0U;
    if (get_position_snapshot_locked(&position_percent)) {
        snapshot->encoder_percent = position_percent;
    } else {
        snapshot->encoder_percent = 0.0f;
    }
    snapshot->servo_angle = system_health.servo_ok ? servo_get_angle(&servo) : 0.0f;
}

void stop_motor_locked(uint32_t current_time) {
    if (motor_is_running(&motor)) {
        update_time_based_position_locked(current_time);
        motor_stop(&motor, current_time);
        system_state.motor_state = motor_get_state(&motor);
        request_runtime_save_locked();
        request_led_network_status_event();
    }
}

void begin_open_sequence_locked(uint32_t current_time) {
    command_slat_locked(SERVO_SLAT_OPEN_ANGLE, current_time, true, "cover open");

    if (system_health.motor_ok) {
        update_time_based_position_locked(current_time);
        capture_current_position_as_motor_baseline_locked();
        motor_set_opening(&motor, current_time);
        system_state.motor_state = motor_get_state(&motor);
        request_led_status_event(LED_STATUS_OPENING);
        request_runtime_save_locked();
        ESP_LOGI(TAG, "Open command applied: servo_target=%.1f motor=%s",
                 system_health.servo_ok ? servo_get_target(&servo) : -1.0f,
                 motor_state_to_string(system_state.motor_state));
    }
}

void begin_close_sequence_locked(uint32_t current_time) {
    command_slat_locked(SERVO_SLAT_CLOSED_ANGLE, current_time, true, "cover close");

    if (system_health.motor_ok) {
        update_time_based_position_locked(current_time);
        capture_current_position_as_motor_baseline_locked();
        motor_set_closing(&motor, current_time);
        system_state.motor_state = motor_get_state(&motor);
        request_led_status_event(LED_STATUS_CLOSING);
        request_runtime_save_locked();
        ESP_LOGI(TAG, "Close command applied: servo_target=%.1f motor=%s",
                 system_health.servo_ok ? servo_get_target(&servo) : -1.0f,
                 motor_state_to_string(system_state.motor_state));
    }
}

static void handle_manual_button_locked(uint32_t current_time) {
    MotorState_t current_motor_state = motor_get_state(&motor);

    if (current_motor_state == MOTOR_OPENING || current_motor_state == MOTOR_CLOSING) {
        clear_control_sequence_locked();
        stop_motor_locked(current_time);
        manual_next_open = (current_motor_state == MOTOR_CLOSING);
    } else if (manual_next_open) {
        begin_open_sequence_locked(current_time);
        manual_next_open = false;
    } else {
        begin_close_sequence_locked(current_time);
        manual_next_open = true;
    }

    system_state.motor_state = motor_get_state(&motor);
}

static void apply_voice_command_locked(SystemCommand_t command, uint32_t current_time) {
    switch (command) {
        case COMMAND_OPEN:
            apply_manual_mode_locked(current_time);
            begin_open_sequence_locked(current_time);
            manual_next_open = false;
            break;
        case COMMAND_CLOSE:
            apply_manual_mode_locked(current_time);
            begin_close_sequence_locked(current_time);
            manual_next_open = true;
            break;
        case COMMAND_STOP:
            clear_control_sequence_locked();
            stop_motor_locked(current_time);
            manual_next_open = true;
            break;
        case COMMAND_RETURN_TO_AUTO:
            mode_return_to_auto(&mode, current_time);
            clear_control_sequence_locked();
            stop_motor_locked(current_time);
            auto_command_pending = system_health.ldr_ok;
            manual_next_open = true;
            update_servo_for_light_locked(current_time);
            request_runtime_save_locked();
            break;
        default:
            return;
    }

    system_state.current_mode = mode_get_current(&mode);
    system_state.motor_state = motor_get_state(&motor);
}

void process_system_event_locked(const SystemEvent_Queue_t *event) {
    if (event == NULL) {
        return;
    }

    switch (event->event_type) {
        case EVENT_BUTTON_PRESSED: {
            ButtonAction_t action = static_cast<ButtonAction_t>(event->data.value_u16);
            OperatingMode_t current_mode = mode_get_current(&mode);

            ESP_LOGI(TAG, "Queue event: %s (%s)",
                     event_type_to_string(event->event_type),
                     button_action_to_string(action));

            if (action == BUTTON_ACTION_LONG) {
                mode_handle_button(&mode, action, event->timestamp);
                clear_control_sequence_locked();
                stop_motor_locked(event->timestamp);
                auto_command_pending = system_health.ldr_ok;
                manual_next_open = true;
                update_servo_for_light_locked(event->timestamp);
            } else if (current_mode == MODE_AUTO) {
                mode_handle_button(&mode, action, event->timestamp);
                clear_control_sequence_locked();
                stop_motor_locked(event->timestamp);
                manual_next_open = true;
            } else {
                handle_manual_button_locked(event->timestamp);
            }
            break;
        }

        case EVENT_LIGHT_CHANGED: {
            LightLevel_t light_level = static_cast<LightLevel_t>(event->data.value_u16);
            OperatingMode_t current_mode = mode_get_current(&mode);

            ESP_LOGI(TAG, "Queue event: %s (%s)",
                     event_type_to_string(event->event_type),
                     light_level_to_string(light_level));

            if (light_level == LIGHT_DARK) {
                if (current_mode == MODE_MANUAL) {
                    ESP_LOGI(TAG, "Darkness overrides manual mode, returning to AUTO");
                    mode_return_to_auto(&mode, event->timestamp);
                    current_mode = mode_get_current(&mode);
                    system_state.current_mode = current_mode;
                    request_runtime_save_locked();
                }
                auto_command_pending = true;
            } else {
                auto_command_pending = (current_mode == MODE_AUTO);
            }

            update_servo_for_light_locked(event->timestamp);
            break;
        }

        case EVENT_MICROPHONE_READY: {
            SystemCommand_t command = static_cast<SystemCommand_t>(event->data.value_u16);
            ESP_LOGI(TAG, "Queue event: %s (%s)",
                     event_type_to_string(event->event_type),
                     system_command_to_string(command));
            apply_voice_command_locked(command, event->timestamp);
            break;
        }

        default:
            ESP_LOGI(TAG, "Queue event: %s", event_type_to_string(event->event_type));
            break;
    }

    system_state.current_mode = mode_get_current(&mode);
    system_state.motor_state = motor_get_state(&motor);
}

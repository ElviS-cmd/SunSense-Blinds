/**
 * @file main.cpp
 * @brief SunSense V2 — controller init, FreeRTOS tasks, and app_main
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "gpio_config.h"
#include "mqtt_handlers.h"
#include "network.h"

extern "C" {
    #include "button_controller.h"
    #include "led_controller.h"
    #include "microphone_controller.h"
    #include "runtime_state.h"
    #include "voice_command_controller.h"
}

static const char *TAG = "SunSense";

/* ============================================================================
 * CONTROLLER INSTANCES (extern-visible via mqtt_handlers.h)
 * ========================================================================== */

ButtonController_t   button    = {};
ModeController_t     mode      = {};
MotorController_t    motor     = {};
LEDController_t      led       = {};
LDRController_t      ldr       = {};
ServoController_t    servo     = {};
MicrophoneController_t microphone = {};
VoiceCommandController_t voice = {};

/* ============================================================================
 * SYSTEM STATE (extern-visible via mqtt_handlers.h)
 * ========================================================================== */

SystemState_t  system_state  = {};
SystemHealth_t system_health = {};
SystemConfig_t system_config = {};
bool           auto_command_pending = false;
bool           manual_next_open     = true;

/* ============================================================================
 * TASK HANDLES AND SYNCHRONISATION
 * ========================================================================== */

static TaskHandle_t     task_button     = NULL;
static TaskHandle_t     task_mode       = NULL;
static TaskHandle_t     task_motor      = NULL;
static TaskHandle_t     task_led        = NULL;
static TaskHandle_t     task_ldr        = NULL;
static TaskHandle_t     task_microphone = NULL;
static SemaphoreHandle_t state_mutex    = NULL;
static RuntimeStateController_t runtime_state = {};
static bool                     runtime_position_valid = false;
static uint8_t                  runtime_position_percent = 0U;
static uint8_t                  last_saved_position_percent = 0U;
static bool                     runtime_save_pending = false;
static uint32_t                 last_runtime_save_time = 0U;
static uint32_t                 auto_actions_enabled_after_ms = 0U;
static volatile LEDStatusPattern_t led_requested_pattern = LED_STATUS_OFFLINE;

/* ============================================================================
 * STATE MUTEX HELPERS
 * ========================================================================== */

static uint8_t clamp_percent_to_u8(float percent) {
    if (percent <= 0.0f) {
        return 0U;
    }
    if (percent >= 100.0f) {
        return 100U;
    }
    return static_cast<uint8_t>(percent + 0.5f);
}

static void update_time_based_position_locked(uint32_t current_time) {
    MotorState_t motor_state = motor_get_state(&motor);
    if (motor_state == MOTOR_STOP || !runtime_position_valid) {
        return;
    }

    uint32_t elapsed = motor_get_elapsed_time(&motor, current_time);
    uint32_t travel_time = system_config.motor_timeout_ms > 0U
        ? system_config.motor_timeout_ms
        : MOTOR_TRAVEL_TIME_MS;

    if (elapsed >= travel_time) {
        runtime_position_percent = (motor_state == MOTOR_OPENING) ? 100U : 0U;
        return;
    }

    float travel_delta = (float)elapsed * 100.0f / (float)travel_time;
    float position = (float)last_saved_position_percent;
    if (motor_state == MOTOR_OPENING) {
        position += travel_delta;
    } else if (motor_state == MOTOR_CLOSING) {
        position -= travel_delta;
    }

    runtime_position_percent = clamp_percent_to_u8(position);
}

static bool get_position_snapshot_locked(uint8_t *position_percent) {
    if (position_percent == NULL) {
        return false;
    }

    if (runtime_position_valid) {
        *position_percent = runtime_position_percent;
        return true;
    }

    return false;
}

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

static RuntimeStateSnapshot_t build_runtime_snapshot_locked(void) {
    RuntimeStateSnapshot_t snapshot = {};
    uint8_t position_percent = 0U;

    snapshot.state_version = RUNTIME_STATE_SCHEMA_VERSION;
    snapshot.position_valid = get_position_snapshot_locked(&position_percent);
    snapshot.position_percent = position_percent;
    snapshot.mode = mode_get_current(&mode);
    snapshot.light_level = ldr_get_level(&ldr);
    snapshot.slat_angle_valid = system_health.servo_ok;
    snapshot.slat_angle_deg = system_health.servo_ok
        ? static_cast<uint8_t>(servo_get_target(&servo) + 0.5f)
        : 0U;

    return snapshot;
}

static void request_runtime_save_locked(void) {
    runtime_save_pending = true;
}

static void clear_control_sequence_locked(void) {
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

static void update_servo_for_light_locked(uint32_t current_time) {
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

bool lock_state(void) {
    return (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE);
}

void unlock_state(void) {
    xSemaphoreGive(state_mutex);
}

static bool led_pattern_is_motion(LEDStatusPattern_t pattern) {
    return (pattern == LED_STATUS_OPENING) || (pattern == LED_STATUS_CLOSING);
}

static bool led_pattern_is_network(LEDStatusPattern_t pattern) {
    return (pattern == LED_STATUS_NORMAL) ||
           (pattern == LED_STATUS_OFFLINE) ||
           (pattern == LED_STATUS_RECONNECTING);
}

void request_led_status_event(LEDStatusPattern_t pattern) {
    /* Motion patterns take priority over non-critical network patterns (NORMAL,
     * RECONNECTING), but OFFLINE always overrides so the user can see the
     * device has lost connectivity even while the blind is moving. */
    if (led_pattern_is_motion(led_requested_pattern) &&
        led_pattern_is_network(pattern) &&
        pattern != LED_STATUS_OFFLINE) {
        return;
    }

    led_requested_pattern = pattern;
}

void request_led_network_status_event(void) {
    if (network_is_provisioning_active()) {
        request_led_status_event(LED_STATUS_PAIRING);
    } else if (network_is_reconnecting()) {
        request_led_status_event(LED_STATUS_RECONNECTING);
    } else if (wifi_connected && mqtt_connected) {
        request_led_status_event(LED_STATUS_NORMAL);
    } else {
        request_led_status_event(LED_STATUS_OFFLINE);
    }
}

/* ============================================================================
 * SHARED LOCKED OPERATIONS (called from mqtt_handlers.cpp and tasks)
 * ========================================================================== */

void apply_manual_mode_locked(uint32_t current_time) {
    mode_set_manual(&mode, current_time);
    system_state.current_mode = mode_get_current(&mode);
    request_runtime_save_locked();
}

void collect_publish_snapshot_locked(PublishSnapshot_t *snapshot) {
    uint8_t position_percent = 0U;
    snapshot->system        = system_state;
    snapshot->ldr_raw       = system_health.ldr_ok ? ldr_get_raw(&ldr)       : 0;
    snapshot->ldr_filtered  = system_health.ldr_ok ? ldr_get_filtered(&ldr)  : 0;
    if (get_position_snapshot_locked(&position_percent)) {
        snapshot->encoder_percent = position_percent;
    } else {
        snapshot->encoder_percent = 0.0f;
    }
    snapshot->servo_angle   = system_health.servo_ok ? servo_get_angle(&servo) : 0.0f;
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
        last_saved_position_percent = runtime_position_percent;
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
        last_saved_position_percent = runtime_position_percent;
        motor_set_closing(&motor, current_time);
        system_state.motor_state = motor_get_state(&motor);
        request_led_status_event(LED_STATUS_CLOSING);
        request_runtime_save_locked();
        ESP_LOGI(TAG, "Close command applied: servo_target=%.1f motor=%s",
                 system_health.servo_ok ? servo_get_target(&servo) : -1.0f,
                 motor_state_to_string(system_state.motor_state));
    }
}

void set_position_locked(uint8_t percent) {
    runtime_position_percent      = percent;
    runtime_position_valid        = true;
    last_saved_position_percent   = percent;
    request_runtime_save_locked();
    ESP_LOGI(TAG, "Position manually set to %u%%", percent);
}

static void maybe_persist_runtime_state_locked(uint32_t current_time) {
    if (!runtime_state.initialized) {
        return;
    }

    update_time_based_position_locked(current_time);

    uint8_t position_percent = 0U;
    bool position_valid = get_position_snapshot_locked(&position_percent);
    bool motor_running = motor_is_running(&motor);
    bool should_save = runtime_save_pending;

    if (position_valid) {
        runtime_position_valid = true;
        runtime_position_percent = position_percent;
    }

    if (!should_save && motor_running && position_valid) {
        uint8_t delta = (position_percent > last_saved_position_percent)
            ? static_cast<uint8_t>(position_percent - last_saved_position_percent)
            : static_cast<uint8_t>(last_saved_position_percent - position_percent);

        should_save =
            (delta >= RUNTIME_POSITION_SAVE_DELTA) &&
            ((current_time - last_runtime_save_time) >= RUNTIME_POSITION_SAVE_INTERVAL_MS);
    }

    if (!should_save) {
        return;
    }

    RuntimeStateSnapshot_t snapshot = build_runtime_snapshot_locked();
    if (runtime_state_save(&runtime_state, &snapshot)) {
        runtime_save_pending = false;
        last_runtime_save_time = current_time;
        if (snapshot.position_valid) {
            last_saved_position_percent = snapshot.position_percent;
        }
    }
}

static void restore_runtime_state(void) {
    if (!runtime_state_init(&runtime_state)) {
        ESP_LOGW(TAG, "Runtime state init failed; assuming position 0%% (fully closed)");
        runtime_position_valid = true;
        runtime_position_percent = 0U;
        last_saved_position_percent = 0U;
        return;
    }

    RuntimeStateSnapshot_t snapshot = {};
    if (!runtime_state_load(&runtime_state, &snapshot)) {
        ESP_LOGI(TAG, "No saved runtime state found");
        runtime_position_valid = true;
        runtime_position_percent = 0U;
        last_saved_position_percent = 0U;
        return;
    }

    runtime_position_valid = snapshot.position_valid;
    runtime_position_percent = snapshot.position_percent;
    last_saved_position_percent = snapshot.position_percent;

    if (snapshot.mode == MODE_MANUAL) {
        mode_set_manual(&mode, 0U);
    } else {
        mode_return_to_auto(&mode, 0U);
    }

    if (system_health.servo_ok && snapshot.slat_angle_valid) {
        command_slat_locked(clamp_slat_angle(snapshot.slat_angle_deg), 0U, false, "runtime restore");
    }

    system_state.current_mode = mode_get_current(&mode);
    system_state.light_level = snapshot.light_level;
    ESP_LOGI(TAG, "Restored runtime state: mode=%s position_valid=%s position=%u%% slats=%u deg",
             mode_to_string(system_state.current_mode),
             snapshot.position_valid ? "yes" : "no",
             snapshot.position_percent,
             snapshot.slat_angle_deg);
}

/* ============================================================================
 * BUTTON LOGIC
 * ========================================================================== */

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

/* ============================================================================
 * FREERTOS TASKS
 * ========================================================================== */

static void task_button_handler(void *pvParameters) {
    (void) pvParameters;
    ESP_LOGI(TAG, "Button task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period   = pdMS_TO_TICKS(TASK_PERIOD_BUTTON);

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        button_update(&button, current_time);

        ButtonAction_t action = button_get_action(&button);
        if (action != BUTTON_ACTION_NONE && lock_state()) {
            OperatingMode_t current_mode = mode_get_current(&mode);

            if (action == BUTTON_ACTION_LONG) {
                mode_handle_button(&mode, action, current_time);
                clear_control_sequence_locked();
                stop_motor_locked(current_time);
                auto_command_pending = system_health.ldr_ok;
                manual_next_open = true;
                update_servo_for_light_locked(current_time);
            } else if (current_mode == MODE_AUTO) {
                mode_handle_button(&mode, action, current_time);
                clear_control_sequence_locked();
                stop_motor_locked(current_time);
                manual_next_open = true;
            } else {
                handle_manual_button_locked(current_time);
            }

            system_state.current_mode = mode_get_current(&mode);
            button_clear_action(&button);
            unlock_state();
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_mode_handler(void *pvParameters) {
    (void) pvParameters;
    ESP_LOGI(TAG, "Mode task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period   = pdMS_TO_TICKS(TASK_PERIOD_MODE);

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool changed          = false;
        OperatingMode_t current_mode = MODE_AUTO;

        if (lock_state()) {
            mode_update_idle(&mode, current_time);
            changed      = mode_changed(&mode);
            current_mode = mode_get_current(&mode);
            system_state.current_mode = current_mode;

            if (changed && current_mode == MODE_AUTO) {
                clear_control_sequence_locked();
                stop_motor_locked(current_time);
                auto_command_pending = system_health.ldr_ok;
                manual_next_open = true;
                update_servo_for_light_locked(current_time);
            }

            unlock_state();
        }

        if (changed) {
            ESP_LOGI(TAG, "Mode changed to: %s", mode_to_string(current_mode));
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_motor_handler(void *pvParameters) {
    (void) pvParameters;
    ESP_LOGI(TAG, "Motor task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period   = pdMS_TO_TICKS(TASK_PERIOD_MOTOR);

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (lock_state()) {
            OperatingMode_t current_mode = mode_get_current(&mode);
            MotorState_t motor_state     = motor_get_state(&motor);

            if (motor_state != MOTOR_STOP) {
                update_time_based_position_locked(current_time);
                bool timeout_reached = (system_config.motor_timeout_ms > 0U) &&
                                       (motor_get_elapsed_time(&motor, current_time) >= system_config.motor_timeout_ms);

                if (timeout_reached) {
                    uint32_t elapsed = motor_get_elapsed_time(&motor, current_time);
                    runtime_position_percent = (motor_state == MOTOR_OPENING) ? 100U : 0U;
                    runtime_position_valid = true;
                    stop_motor_locked(current_time);
                    request_runtime_save_locked();
                    ESP_LOGI(TAG,
                             "Time-based motor travel complete: direction=%s position=%u%% elapsed=%lums",
                             motor_state_to_string(motor_state),
                             runtime_position_percent,
                             (unsigned long)elapsed);
                }
            } else {
                if ((current_mode == MODE_AUTO) &&
                    (current_time >= auto_actions_enabled_after_ms) &&
                    auto_command_pending &&
                    system_health.ldr_ok) {
                    if (ldr_is_bright(&ldr)) {
                        begin_open_sequence_locked(current_time);
                    } else if (ldr_is_dark(&ldr)) {
                        begin_close_sequence_locked(current_time);
                    }
                    auto_command_pending = false;
                }
            }

            maybe_persist_runtime_state_locked(current_time);
            system_state.motor_state = motor_get_state(&motor);
            unlock_state();
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_led_handler(void *pvParameters) {
    (void) pvParameters;
    ESP_LOGI(TAG, "LED task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period   = pdMS_TO_TICKS(TASK_PERIOD_LED);

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool healthy          = false;

        if (lock_state()) {
            healthy      = system_state.system_healthy;
            unlock_state();
        }

        if (system_health.led_ok) {
            LEDStatusPattern_t pattern = healthy
                ? led_requested_pattern
                : LED_STATUS_FAULT;

            led_set_status_pattern(&led, pattern, current_time);
            led_update(&led, current_time);
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_ldr_handler(void *pvParameters) {
    (void) pvParameters;
    ESP_LOGI(TAG, "LDR task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period   = pdMS_TO_TICKS(TASK_PERIOD_LDR);

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool level_changed    = false;
        LightLevel_t light_level = LIGHT_DARK;
        uint16_t raw_level    = 0;
        uint16_t filtered_level = 0;

        if (system_health.ldr_ok && lock_state()) {
            OperatingMode_t current_mode = mode_get_current(&mode);
            ldr_update(&ldr, current_time);
            level_changed = ldr_level_changed(&ldr);
            light_level   = ldr_get_level(&ldr);
            raw_level     = ldr_get_raw(&ldr);
            filtered_level = ldr_get_filtered(&ldr);
            system_state.light_level = light_level;

            if (level_changed) {
                if (ldr_is_dark(&ldr)) {
                    if (current_mode == MODE_MANUAL) {
                        ESP_LOGI(TAG, "Darkness overrides manual mode, returning to AUTO");
                        mode_return_to_auto(&mode, current_time);
                        system_state.current_mode = mode_get_current(&mode);
                        request_runtime_save_locked();
                    }
                    auto_command_pending = true;
                } else {
                    auto_command_pending = (current_mode == MODE_AUTO);
                }

                update_servo_for_light_locked(current_time);
            }

            unlock_state();
        }

        ESP_LOGI(TAG, "LDR raw: %u filtered: %u level: %s",
                 raw_level,
                 filtered_level,
                 light_level_to_string(light_level));

        if (level_changed) {
            ESP_LOGI(TAG, "Light level changed to: %s", light_level_to_string(light_level));
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_microphone_handler(void *pvParameters) {
    (void) pvParameters;
    ESP_LOGI(TAG, "Microphone task started");
    ESP_LOGI(TAG, "Voice commands: 1 utterance=open, 2=close, 3=stop, 4=auto");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period   = pdMS_TO_TICKS(TASK_PERIOD_MICROPHONE);
    uint32_t last_log         = 0;

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (system_health.microphone_ok) {
            microphone_update(&microphone, current_time);

            if (microphone_is_buffer_ready(&microphone)) {
                uint16_t level = microphone_get_level(&microphone);
                SystemCommand_t voice_command = COMMAND_NONE;

                if (system_config.enable_voice_commands) {
                    voice_command = voice_command_update(&voice,
                                                         level,
                                                         system_config.audio_threshold,
                                                         current_time);
                }

                if (current_time - last_log > 2000U) {
                    ESP_LOGI(TAG, "Audio level: %u", level);
                    last_log = current_time;
                }

                microphone_clear_buffer(&microphone);

                if (voice_command != COMMAND_NONE) {
                    ESP_LOGI(TAG, "Applying voice command: %s", voice_command_to_string(voice_command));
                    if (lock_state()) {
                        apply_voice_command_locked(voice_command, current_time);
                        unlock_state();
                    }
                }
            }
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

/* ============================================================================
 * INITIALISATION
 * ========================================================================== */

static bool create_task_checked(TaskFunction_t task_fn,
                                const char *name,
                                uint32_t stack_size,
                                UBaseType_t priority,
                                TaskHandle_t *handle,
                                BaseType_t core_id) {
    BaseType_t result = xTaskCreatePinnedToCore(task_fn, name, stack_size, NULL, priority, handle, core_id);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: %s", name);
        return false;
    }
    return true;
}

static void run_servo_boot_exercise(void) {
#if SUNSENSE_SERVO_BOOT_EXERCISE
    if (!system_health.servo_ok) {
        ESP_LOGW(TAG, "Skipping servo boot exercise: servo unavailable");
        return;
    }

    ESP_LOGW(TAG, "Running servo boot exercise before mode/LDR/motor integration");
    const float exercise_angles[] = {
        90.0f,
        SERVO_SLAT_OPEN_ANGLE,
        90.0f,
        SERVO_SLAT_CLOSED_ANGLE,
        90.0f,
    };
    const size_t exercise_count = sizeof(exercise_angles) / sizeof(exercise_angles[0]);

    for (size_t i = 0; i < exercise_count; ++i) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        float angle = exercise_angles[i];

        ESP_LOGI(TAG, "Servo boot exercise command angle=%.1f duty=%lu",
                 angle,
                 (unsigned long)servo_angle_to_duty(angle));
        servo_move_to(&servo, angle, current_time);

        while (servo_is_moving(&servo)) {
            vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MOTOR));
            current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            servo_update(&servo, current_time);
        }

        vTaskDelay(pdMS_TO_TICKS(SERVO_TEST_HOLD_MS));
    }

    ESP_LOGW(TAG, "Servo boot exercise complete");
#endif
}

static bool initialize_all_controllers(void) {
    ESP_LOGI(TAG, "Initializing controllers...");

    system_config = get_default_config();

    system_health.button_ok    = button_init(&button);
    mode_init(&mode);
    system_health.motor_ok     = motor_init(&motor);
    system_health.led_ok       = led_init(&led);
    system_health.ldr_ok       = ldr_init(&ldr);
    system_health.encoder_ok   = false;
    system_health.servo_ok     = servo_init(&servo);
    system_health.microphone_ok = microphone_init(&microphone);
    voice_command_init(&voice);

    run_servo_boot_exercise();
    restore_runtime_state();

    system_state.current_mode  = mode_get_current(&mode);
    system_state.motor_state   = motor_get_state(&motor);
    system_state.light_level   = ldr_get_level(&ldr);
    system_state.system_healthy =
        system_health.button_ok &&
        system_health.motor_ok  &&
        system_health.led_ok    &&
        system_health.ldr_ok    &&
        system_health.servo_ok  &&
        system_health.microphone_ok;

    auto_command_pending = system_health.ldr_ok && (mode_get_current(&mode) == MODE_AUTO);
    manual_next_open     = true;

    ESP_LOGI(TAG, "Button:     %s", system_health.button_ok     ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Motor:      %s", system_health.motor_ok      ? "OK" : "FAILED");
    ESP_LOGI(TAG, "LED:        %s", system_health.led_ok        ? "OK" : "FAILED");
    ESP_LOGI(TAG, "LDR:        %s", system_health.ldr_ok        ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Encoder:    DISABLED (time-based travel)");
    ESP_LOGI(TAG, "Servo:      %s", system_health.servo_ok      ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Microphone: %s", system_health.microphone_ok ? "OK" : "FAILED");

    return system_state.system_healthy;
}

static bool create_all_tasks(void) {
    ESP_LOGI(TAG, "Creating FreeRTOS tasks...");

    return
        create_task_checked(task_button_handler,     "button_task",     TASK_STACK_BUTTON,     TASK_PRIORITY_BUTTON,     &task_button,     0) &&
        create_task_checked(task_mode_handler,       "mode_task",       TASK_STACK_MODE,       TASK_PRIORITY_MODE,       &task_mode,       0) &&
        create_task_checked(task_motor_handler,      "motor_task",      TASK_STACK_MOTOR,      TASK_PRIORITY_MOTOR,      &task_motor,      1) &&
        create_task_checked(task_led_handler,        "led_task",        TASK_STACK_LED,        TASK_PRIORITY_LED,        &task_led,        0) &&
        create_task_checked(task_ldr_handler,        "ldr_task",        TASK_STACK_LDR,        TASK_PRIORITY_LDR,        &task_ldr,        0) &&
        create_task_checked(task_microphone_handler, "microphone_task", TASK_STACK_MICROPHONE, TASK_PRIORITY_MICROPHONE, &task_microphone, 1);
}

#if SUNSENSE_SERVO_TEST_ONLY
static void run_servo_only_test(void) {
    ESP_LOGW(TAG, "SUNSENSE_SERVO_TEST_ONLY=1; starting isolated servo test");
    ESP_LOGW(TAG, "LDR, motor, encoder, MQTT, Wi-Fi, mode, button, LEDs, and microphone are bypassed");

    ServoController_t test_servo = {};
    if (!servo_init(&test_servo)) {
        ESP_LOGE(TAG, "Servo-only test failed: servo_init() failed");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

#if SUNSENSE_SERVO_TEST_FULL_RANGE
    const float test_angles[] = {0.0f, 90.0f, 180.0f, 90.0f};
#else
    const float test_angles[] = {90.0f, 60.0f, 90.0f, 120.0f};
#endif
    const size_t test_angle_count = sizeof(test_angles) / sizeof(test_angles[0]);
    size_t index = 0U;

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        float angle = test_angles[index];

        ESP_LOGI(TAG, "Servo-only test command angle=%.1f expected_duty=%lu",
                 angle,
                 (unsigned long)servo_angle_to_duty(angle));
        servo_move_to(&test_servo, angle, current_time);

        while (servo_is_moving(&test_servo)) {
            vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MOTOR));
            current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            servo_update(&test_servo, current_time);
        }

        ESP_LOGI(TAG, "Servo-only test settled current=%.1f target=%.1f duty=%lu state=%s",
                 servo_get_angle(&test_servo),
                 servo_get_target(&test_servo),
                 (unsigned long)servo_get_duty(&test_servo),
                 servo_is_moving(&test_servo) ? "moving" : "idle");

        vTaskDelay(pdMS_TO_TICKS(SERVO_TEST_HOLD_MS));
        index = (index + 1U) % test_angle_count;
    }
}
#endif

/* ============================================================================
 * APP MAIN
 * ========================================================================== */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting SunSense V2");

#if SUNSENSE_SERVO_TEST_ONLY
    run_servo_only_test();
    return;
#endif

    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return;
    }

    if (!initialize_all_controllers()) {
        ESP_LOGE(TAG, "Controller initialization failed, refusing to start tasks");
        return;
    }

    /* Set settle deadline BEFORE tasks start so no task ever reads the
     * initial value of 0 and bypasses the startup settle window. */
    auto_actions_enabled_after_ms =
        (xTaskGetTickCount() * portTICK_PERIOD_MS) + AUTO_STARTUP_SETTLE_MS;

    if (!create_all_tasks()) {
        ESP_LOGE(TAG, "Task creation failed, refusing to continue");
        return;
    }

    initialize_network();

    uint32_t last_log_time     = 0;
    uint32_t last_publish_time = 0;

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        SystemState_t snapshot = {};

        if (lock_state()) {
            servo_update(&servo, current_time);
            system_state.uptime_ms = current_time;
            maybe_persist_runtime_state_locked(current_time);
            snapshot = system_state;
            unlock_state();
        }

        if (current_time - last_log_time > 10000U) {
            ESP_LOGI(TAG, "=== System Status ===");
            ESP_LOGI(TAG, "Uptime: %lu ms", static_cast<unsigned long>(snapshot.uptime_ms));
            ESP_LOGI(TAG, "Mode:   %s", mode_to_string(snapshot.current_mode));
            ESP_LOGI(TAG, "Motor:  %s", motor_state_to_string(snapshot.motor_state));
            ESP_LOGI(TAG, "Light:  %s", light_level_to_string(snapshot.light_level));
            ESP_LOGI(TAG, "Health: %s", snapshot.system_healthy ? "OK" : "FAULT");
            last_log_time = current_time;
        }

        if (current_time - last_publish_time > 5000U) {
            publish_state_if_ready();
            last_publish_time = current_time;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

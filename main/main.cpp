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
EncoderController_t  encoder   = {};
ServoController_t    servo     = {};
MicrophoneController_t microphone = {};

/* ============================================================================
 * SYSTEM STATE (extern-visible via mqtt_handlers.h)
 * ========================================================================== */

SystemState_t  system_state  = {};
SystemHealth_t system_health = {};
SystemConfig_t system_config = {};
bool           auto_command_pending = false;
bool           manual_next_open     = true;

typedef enum {
    CONTROL_SEQUENCE_NONE = 0,
    CONTROL_SEQUENCE_WAIT_SERVO_OPEN,
    CONTROL_SEQUENCE_WAIT_SERVO_CLOSE,
    CONTROL_SEQUENCE_MOVE_OPEN,
    CONTROL_SEQUENCE_MOVE_CLOSE,
} ControlSequenceState_t;

/* ============================================================================
 * TASK HANDLES AND SYNCHRONISATION
 * ========================================================================== */

static TaskHandle_t     task_button     = NULL;
static TaskHandle_t     task_mode       = NULL;
static TaskHandle_t     task_motor      = NULL;
static TaskHandle_t     task_led        = NULL;
static TaskHandle_t     task_ldr        = NULL;
static TaskHandle_t     task_encoder    = NULL;
static TaskHandle_t     task_microphone = NULL;
static SemaphoreHandle_t state_mutex    = NULL;
static RuntimeStateController_t runtime_state = {};
static ControlSequenceState_t   control_sequence = CONTROL_SEQUENCE_NONE;
static bool                     runtime_position_valid = false;
static uint8_t                  runtime_position_percent = 0U;
static uint8_t                  last_saved_position_percent = 0U;
static bool                     runtime_save_pending = false;
static uint32_t                 last_runtime_save_time = 0U;
static uint32_t                 auto_actions_enabled_after_ms = 0U;

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

static bool get_position_snapshot_locked(uint8_t *position_percent) {
    if (position_percent == NULL) {
        return false;
    }

    if (system_health.encoder_ok && encoder_is_healthy(&encoder)) {
        *position_percent = clamp_percent_to_u8(encoder_get_percent(&encoder));
        return true;
    }

    if (runtime_position_valid) {
        *position_percent = runtime_position_percent;
        return true;
    }

    return false;
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
    control_sequence = CONTROL_SEQUENCE_NONE;
}

static void update_servo_for_light_locked(uint32_t current_time) {
    if (!system_health.servo_ok || mode_get_current(&mode) != MODE_AUTO) {
        return;
    }

    if (ldr_is_bright(&ldr)) {
        if (servo_get_target(&servo) != SERVO_SLAT_OPEN_ANGLE) {
            servo_open(&servo, current_time);
            request_runtime_save_locked();
        }
    } else if (ldr_is_dark(&ldr)) {
        if (servo_get_target(&servo) != SERVO_SLAT_CLOSED_ANGLE) {
            servo_close(&servo, current_time);
            request_runtime_save_locked();
        }
    }
}

bool lock_state(void) {
    return (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE);
}

void unlock_state(void) {
    xSemaphoreGive(state_mutex);
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
    if (system_health.encoder_ok) {
        snapshot->encoder_percent = encoder_get_percent(&encoder);
    } else if (get_position_snapshot_locked(&position_percent)) {
        snapshot->encoder_percent = position_percent;
    } else {
        snapshot->encoder_percent = 0.0f;
    }
    snapshot->servo_angle   = system_health.servo_ok ? servo_get_angle(&servo) : 0.0f;
}

void stop_motor_locked(uint32_t current_time) {
    if (motor_is_running(&motor)) {
        motor_stop(&motor, current_time);
        system_state.motor_state = motor_get_state(&motor);
        request_runtime_save_locked();
    }
}

void begin_open_sequence_locked(uint32_t current_time) {
    if (system_health.servo_ok) {
        if (servo_get_target(&servo) != SERVO_SLAT_OPEN_ANGLE) {
            servo_open(&servo, current_time);
        }
        if (!servo_at_target(&servo)) {
            control_sequence = CONTROL_SEQUENCE_WAIT_SERVO_OPEN;
            return;
        }
    }

    control_sequence = CONTROL_SEQUENCE_MOVE_OPEN;
}

void begin_close_sequence_locked(uint32_t current_time) {
    if (system_health.servo_ok) {
        if (servo_get_target(&servo) != SERVO_SLAT_CLOSED_ANGLE) {
            servo_close(&servo, current_time);
        }
        if (!servo_at_target(&servo)) {
            control_sequence = CONTROL_SEQUENCE_WAIT_SERVO_CLOSE;
            return;
        }
    }

    control_sequence = CONTROL_SEQUENCE_MOVE_CLOSE;
}

static void advance_control_sequence_locked(uint32_t current_time) {
    switch (control_sequence) {
        case CONTROL_SEQUENCE_WAIT_SERVO_OPEN:
            if (servo_at_target(&servo)) {
                control_sequence = CONTROL_SEQUENCE_MOVE_OPEN;
            }
            break;
        case CONTROL_SEQUENCE_WAIT_SERVO_CLOSE:
            if (servo_at_target(&servo)) {
                control_sequence = CONTROL_SEQUENCE_MOVE_CLOSE;
            }
            break;
        case CONTROL_SEQUENCE_MOVE_OPEN:
            if (!motor_is_running(&motor)) {
                motor_set_opening(&motor, current_time);
                system_state.motor_state = motor_get_state(&motor);
                control_sequence = CONTROL_SEQUENCE_NONE;
            }
            break;
        case CONTROL_SEQUENCE_MOVE_CLOSE:
            if (!motor_is_running(&motor)) {
                motor_set_closing(&motor, current_time);
                system_state.motor_state = motor_get_state(&motor);
                control_sequence = CONTROL_SEQUENCE_NONE;
            }
            break;
        case CONTROL_SEQUENCE_NONE:
        default:
            break;
    }
}

static void maybe_persist_runtime_state_locked(uint32_t current_time) {
    if (!runtime_state.initialized) {
        return;
    }

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
        ESP_LOGW(TAG, "Runtime state init failed");
        return;
    }

    RuntimeStateSnapshot_t snapshot = {};
    if (!runtime_state_load(&runtime_state, &snapshot)) {
        ESP_LOGI(TAG, "No saved runtime state found");
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
        servo_move_to(&servo, snapshot.slat_angle_deg, 0U);
        servo_update(&servo, 0U);
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
                bool timeout_reached  = (motor_get_elapsed_time(&motor, current_time) >= system_config.motor_timeout_ms);
                bool endpoint_reached = false;

                if (system_health.encoder_ok && encoder_is_healthy(&encoder)) {
                    float position_percent = encoder_get_percent(&encoder);
                    endpoint_reached =
                        ((motor_state == MOTOR_OPENING) && (position_percent >= 99.0f)) ||
                        ((motor_state == MOTOR_CLOSING) && (position_percent <= 1.0f));
                }

                if (timeout_reached || endpoint_reached) {
                    stop_motor_locked(current_time);
                }
            } else {
                advance_control_sequence_locked(current_time);

                if ((control_sequence == CONTROL_SEQUENCE_NONE) &&
                    (current_mode == MODE_AUTO) &&
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
        OperatingMode_t current_mode = MODE_AUTO;

        if (lock_state()) {
            healthy      = system_state.system_healthy;
            current_mode = system_state.current_mode;
            unlock_state();
        }

        if (system_health.led_ok) {
            led_update(&led, current_time);
            led_set_green(&led, healthy ? LED_BLINK_SLOW : LED_BLINK_FAST);
            led_set_blue(&led, (current_mode == MODE_AUTO) ? LED_ON : LED_BLINK_SLOW);
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

static void task_encoder_handler(void *pvParameters) {
    (void) pvParameters;
    ESP_LOGI(TAG, "Encoder task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period   = pdMS_TO_TICKS(TASK_PERIOD_ENCODER);
    uint32_t last_log         = 0;

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool updated          = false;
        float angle           = 0.0f;

        if (system_health.encoder_ok && lock_state()) {
            updated = encoder_update(&encoder, current_time);
            angle   = encoder_get_degrees(&encoder);
            unlock_state();
        }

        if (updated && (current_time - last_log > 1000U)) {
            ESP_LOGI(TAG, "Encoder: %.1f°", angle);
            last_log = current_time;
        }

        vTaskDelayUntil(&last_wake_time, period);
    }
}

static void task_microphone_handler(void *pvParameters) {
    (void) pvParameters;
    ESP_LOGI(TAG, "Microphone task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period   = pdMS_TO_TICKS(TASK_PERIOD_MICROPHONE);
    uint32_t last_log         = 0;

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (system_health.microphone_ok) {
            microphone_update(&microphone, current_time);

            if (microphone_is_buffer_ready(&microphone)) {
                uint16_t level = microphone_get_level(&microphone);

                if (current_time - last_log > 2000U) {
                    ESP_LOGI(TAG, "Audio level: %u", level);
                    last_log = current_time;
                }

                microphone_clear_buffer(&microphone);
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

static bool initialize_all_controllers(void) {
    ESP_LOGI(TAG, "Initializing controllers...");

    system_config = get_default_config();

    system_health.button_ok    = button_init(&button);
    mode_init(&mode);
    system_health.motor_ok     = motor_init(&motor);
    system_health.led_ok       = led_init(&led);
    system_health.ldr_ok       = ldr_init(&ldr);
    system_health.encoder_ok   = encoder_init(&encoder);
    system_health.servo_ok     = servo_init(&servo);
    system_health.microphone_ok = microphone_init(&microphone);
    restore_runtime_state();

    system_state.current_mode  = mode_get_current(&mode);
    system_state.motor_state   = motor_get_state(&motor);
    system_state.light_level   = ldr_get_level(&ldr);
    system_state.system_healthy =
        system_health.button_ok &&
        system_health.motor_ok  &&
        system_health.led_ok    &&
        system_health.ldr_ok    &&
        system_health.encoder_ok &&
        system_health.servo_ok  &&
        system_health.microphone_ok;

    auto_command_pending = system_health.ldr_ok && (mode_get_current(&mode) == MODE_AUTO);
    manual_next_open     = true;

    ESP_LOGI(TAG, "Button:     %s", system_health.button_ok     ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Motor:      %s", system_health.motor_ok      ? "OK" : "FAILED");
    ESP_LOGI(TAG, "LED:        %s", system_health.led_ok        ? "OK" : "FAILED");
    ESP_LOGI(TAG, "LDR:        %s", system_health.ldr_ok        ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Encoder:    %s", system_health.encoder_ok    ? "OK" : "FAILED");
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
        create_task_checked(task_encoder_handler,    "encoder_task",    TASK_STACK_ENCODER,    TASK_PRIORITY_ENCODER,    &task_encoder,    0) &&
        create_task_checked(task_microphone_handler, "microphone_task", TASK_STACK_MICROPHONE, TASK_PRIORITY_MICROPHONE, &task_microphone, 1);
}

/* ============================================================================
 * APP MAIN
 * ========================================================================== */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting SunSense V2");

    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return;
    }

    if (!initialize_all_controllers()) {
        ESP_LOGE(TAG, "Controller initialization failed, refusing to start tasks");
        return;
    }

    if (!create_all_tasks()) {
        ESP_LOGE(TAG, "Task creation failed, refusing to continue");
        return;
    }

    initialize_network();
    auto_actions_enabled_after_ms = AUTO_STARTUP_SETTLE_MS;

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

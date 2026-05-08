/**
 * @file app_state.cpp
 * @brief Shared app state, runtime persistence, LED requests, and event queue.
 */

#include "app_state.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "gpio_config.h"
#include "app_controller.h"
#include "network.h"

extern "C" {
    #include "runtime_state.h"
}

static const char *TAG = "SunSense";

ButtonController_t button = {};
ModeController_t mode = {};
MotorController_t motor = {};
LEDController_t led = {};
LDRController_t ldr = {};
ServoController_t servo = {};
MicrophoneController_t microphone = {};
VoiceCommandController_t voice = {};

SystemState_t system_state = {};
SystemHealth_t system_health = {};
SystemConfig_t system_config = {};
bool auto_command_pending = false;
bool manual_next_open = true;

static SemaphoreHandle_t state_mutex = NULL;
static SemaphoreHandle_t event_ready_semaphore = NULL;
static QueueHandle_t system_event_queue = NULL;
static RuntimeStateController_t runtime_state = {};
static bool runtime_position_valid = false;
static uint8_t runtime_position_percent = 0U;
static uint8_t last_saved_position_percent = 0U;
static bool runtime_save_pending = false;
static uint32_t last_runtime_save_time = 0U;
static uint32_t auto_actions_enabled_after_ms = 0U;
static volatile LEDStatusPattern_t led_requested_pattern =
#if SUNSENSE_LOCAL_ONLY
    LED_STATUS_NORMAL;
#else
    LED_STATUS_OFFLINE;
#endif

static constexpr UBaseType_t SYSTEM_EVENT_QUEUE_LENGTH = 12U;

bool sunsense_local_only_enabled(void) {
#if SUNSENSE_LOCAL_ONLY
    return true;
#else
    return false;
#endif
}

static uint8_t clamp_percent_to_u8(float percent) {
    if (percent <= 0.0f) {
        return 0U;
    }
    if (percent >= 100.0f) {
        return 100U;
    }
    return static_cast<uint8_t>(percent + 0.5f);
}

bool initialize_state_sync(void) {
    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return false;
    }

    event_ready_semaphore = xSemaphoreCreateBinary();
    if (event_ready_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create event ready semaphore");
        return false;
    }

    system_event_queue = xQueueCreate(SYSTEM_EVENT_QUEUE_LENGTH, sizeof(SystemEvent_Queue_t));
    if (system_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create system event queue");
        return false;
    }

    return true;
}

bool lock_state(void) {
    return (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE);
}

void unlock_state(void) {
    xSemaphoreGive(state_mutex);
}

const char *event_type_to_string(SystemEvent_t event_type) {
    switch (event_type) {
        case EVENT_NONE:
            return "NONE";
        case EVENT_BUTTON_PRESSED:
            return "BUTTON_PRESSED";
        case EVENT_MODE_CHANGED:
            return "MODE_CHANGED";
        case EVENT_LIGHT_CHANGED:
            return "LIGHT_CHANGED";
        case EVENT_MOTOR_STARTED:
            return "MOTOR_STARTED";
        case EVENT_MOTOR_STOPPED:
            return "MOTOR_STOPPED";
        case EVENT_ENCODER_ERROR:
            return "ENCODER_ERROR";
        case EVENT_MICROPHONE_READY:
            return "MICROPHONE_READY";
        default:
            return "UNKNOWN";
    }
}

bool queue_system_event(SystemEvent_t event_type,
                        uint32_t current_time,
                        uint16_t value_u16) {
    if (system_event_queue == NULL) {
        return false;
    }

    SystemEvent_Queue_t event = {};
    event.event_type = event_type;
    event.timestamp = current_time;
    event.data.value_u16 = value_u16;

    if (xQueueSend(system_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "System event queue full, dropping %s", event_type_to_string(event_type));
        return false;
    }

    if (event_ready_semaphore != NULL) {
        xSemaphoreGive(event_ready_semaphore);
    }
    return true;
}

bool system_event_ready(void) {
    return (event_ready_semaphore != NULL) &&
           (xSemaphoreTake(event_ready_semaphore, 0) == pdTRUE);
}

bool receive_system_event(SystemEvent_Queue_t *event) {
    if (system_event_queue == NULL || event == NULL) {
        return false;
    }

    return xQueueReceive(system_event_queue, event, 0) == pdTRUE;
}

void update_time_based_position_locked(uint32_t current_time) {
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

bool get_position_snapshot_locked(uint8_t *position_percent) {
    if (position_percent == NULL) {
        return false;
    }

    if (runtime_position_valid) {
        *position_percent = runtime_position_percent;
        return true;
    }

    return false;
}

void capture_current_position_as_motor_baseline_locked(void) {
    last_saved_position_percent = runtime_position_percent;
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

void request_runtime_save_locked(void) {
    runtime_save_pending = true;
}

void maybe_persist_runtime_state_locked(uint32_t current_time) {
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

void restore_runtime_state(void) {
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

void set_position_locked(uint8_t percent) {
    runtime_position_percent = percent;
    runtime_position_valid = true;
    last_saved_position_percent = percent;
    request_runtime_save_locked();
    ESP_LOGI(TAG, "Position manually set to %u%%", percent);
}

void mark_motor_travel_complete_locked(MotorState_t motor_state) {
    runtime_position_percent = (motor_state == MOTOR_OPENING) ? 100U : 0U;
    runtime_position_valid = true;
}

void set_auto_actions_enabled_after_ms(uint32_t current_time) {
    auto_actions_enabled_after_ms = current_time + AUTO_STARTUP_SETTLE_MS;
}

bool auto_actions_enabled(uint32_t current_time) {
    return current_time >= auto_actions_enabled_after_ms;
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
    if (led_pattern_is_motion(led_requested_pattern) &&
        led_pattern_is_network(pattern) &&
        pattern != LED_STATUS_OFFLINE) {
        return;
    }

    led_requested_pattern = pattern;
}

void request_led_network_status_event(void) {
    if (sunsense_local_only_enabled()) {
        request_led_status_event(LED_STATUS_NORMAL);
        return;
    }

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

LEDStatusPattern_t get_requested_led_pattern(void) {
    return led_requested_pattern;
}

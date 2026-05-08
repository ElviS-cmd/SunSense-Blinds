/**
 * @file system_init.cpp
 * @brief Controller startup and optional isolated servo test path.
 */

#include "system_init.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "gpio_config.h"
#include "app_state.h"

static const char *TAG = "SunSense";

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

bool initialize_all_controllers(void) {
    ESP_LOGI(TAG, "Initializing controllers...");

    system_config = get_default_config();
    if (sunsense_local_only_enabled()) {
        system_config.enable_voice_commands = false;
    }

    system_health.button_ok = button_init(&button);
    mode_init(&mode);
    system_health.motor_ok = motor_init(&motor);
    system_health.led_ok = led_init(&led);
    system_health.ldr_ok = ldr_init(&ldr);
    system_health.encoder_ok = false;
    system_health.servo_ok = servo_init(&servo);
    if (sunsense_local_only_enabled()) {
        system_health.microphone_ok = true;
        ESP_LOGI(TAG, "Microphone init bypassed in local-only mode");
    } else {
        system_health.microphone_ok = microphone_init(&microphone);
        voice_command_init(&voice);
    }

    run_servo_boot_exercise();
    restore_runtime_state();

    system_state.current_mode = mode_get_current(&mode);
    system_state.motor_state = motor_get_state(&motor);
    system_state.light_level = ldr_get_level(&ldr);
    system_state.system_healthy =
        system_health.button_ok &&
        system_health.motor_ok &&
        system_health.led_ok &&
        system_health.ldr_ok &&
        system_health.servo_ok &&
        system_health.microphone_ok;

    auto_command_pending = system_health.ldr_ok && (mode_get_current(&mode) == MODE_AUTO);
    manual_next_open = true;

    ESP_LOGI(TAG, "Button:     %s", system_health.button_ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Motor:      %s", system_health.motor_ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "LED:        %s", system_health.led_ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "LDR:        %s", system_health.ldr_ok ? "OK" : "FAILED");
    ESP_LOGI(TAG, "Encoder:    DISABLED (time-based travel)");
    ESP_LOGI(TAG, "Servo:      %s", system_health.servo_ok ? "OK" : "FAILED");
    if (sunsense_local_only_enabled()) {
        ESP_LOGI(TAG, "Microphone: BYPASSED (local-only mode)");
    } else {
        ESP_LOGI(TAG, "Microphone: %s", system_health.microphone_ok ? "OK" : "FAILED");
    }

    return system_state.system_healthy;
}

void run_servo_only_test(void) {
#if SUNSENSE_SERVO_TEST_ONLY
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
#endif
}

/**
 * @file main.cpp
 * @brief SunSense V2 app entry point and main status/publish loop.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "gpio_config.h"
#include "app_state.h"
#include "mqtt_handlers.h"
#include "network.h"
#include "system_init.h"
#include "tasks.h"

static const char *TAG = "SunSense";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting SunSense V2");
    if (sunsense_local_only_enabled()) {
        ESP_LOGW(TAG, "Running in local-only mode: LDR + physical button control, network disabled");
    }

#if SUNSENSE_SERVO_TEST_ONLY
    run_servo_only_test();
    return;
#endif

    if (!initialize_state_sync()) {
        return;
    }

    if (!initialize_all_controllers()) {
        ESP_LOGE(TAG, "Controller initialization failed, refusing to start tasks");
        return;
    }

    set_auto_actions_enabled_after_ms(xTaskGetTickCount() * portTICK_PERIOD_MS);

    if (!create_all_tasks()) {
        ESP_LOGE(TAG, "Task creation failed, refusing to continue");
        return;
    }

    if (!sunsense_local_only_enabled()) {
        initialize_network();
    }

    uint32_t last_log_time = 0;
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

        if (!sunsense_local_only_enabled() &&
            (current_time - last_publish_time > 5000U)) {
            publish_state_if_ready();
            last_publish_time = current_time;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

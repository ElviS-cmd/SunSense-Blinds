#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"

/* Component Headers */
extern "C" {
    #include "ldr_controller.h"
    #include "mode_controller.h"
    #include "motor_controller.h"
    #include "button_controller.h"
    #include "oled_display.h"
    #include "system_types.h"
}

/* ==================== Logging ==================== */

static const char *TAG = "SUNSENSE_MAIN";

/* ==================== Task Handles ==================== */

static TaskHandle_t g_button_task_handle = NULL;
static TaskHandle_t g_mode_task_handle = NULL;
static TaskHandle_t g_motor_task_handle = NULL;
static TaskHandle_t g_display_task_handle = NULL;

/* ==================== Global State ==================== */

static ModeController g_mode_ctrl = {MODE_MENU, 0};
static MotorController g_motor_ctrl = {MOTOR_STOPPED, MOTOR_STOPPED, false};
static ButtonController g_button_ctrl = {{0, 0, false, BUTTON_ACTION_NONE}, 
                                          {0, 0, false, BUTTON_ACTION_NONE}, 
                                          {0, 0, false, BUTTON_ACTION_NONE}};

/* ==================== GPIO Configuration ==================== */

/**
 * @brief Configure GPIO pins for buttons and motor control
 */
static esp_err_t gpio_init(void)
{
    ESP_LOGI(TAG, "Initializing GPIO pins");

    /* Configure button input pins */
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << GPIO_BTN_UP) | (1ULL << GPIO_BTN_DOWN) | (1ULL << GPIO_BTN_ENTER),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(
        gpio_config(&btn_config),
        TAG,
        "Failed to configure button GPIO pins"
    );

    /* TODO: Configure motor control pins (L298N inputs) */
    /* Example:
     * #define GPIO_MOTOR_IN1 GPIO_NUM_25
     * #define GPIO_MOTOR_IN2 GPIO_NUM_26
     * gpio_config_t motor_config = {
     *     .pin_bit_mask = (1ULL << GPIO_MOTOR_IN1) | (1ULL << GPIO_MOTOR_IN2),
     *     .mode = GPIO_MODE_OUTPUT,
     *     .pull_up_en = GPIO_PULLUP_DISABLE,
     *     .pull_down_en = GPIO_PULLDOWN_DISABLE,
     *     .intr_type = GPIO_INTR_DISABLE,
     * };
     * gpio_config(&motor_config);
     */

    ESP_LOGI(TAG, "GPIO initialization complete");
    return ESP_OK;
}

/* ==================== Button Task (50ms polling) ==================== */

/**
 * @brief Task for polling button states and updating button controller
 * Runs every 50ms to detect button presses with debouncing
 */
static void button_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Button task started");

    button_init(&g_button_ctrl);
    uint32_t last_tick_ms = 0;

    while (1) {
        uint32_t current_tick_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* Read GPIO button states (active low with pull-ups) */
        bool btn_up_pressed = !gpio_get_level(GPIO_BTN_UP);
        bool btn_down_pressed = !gpio_get_level(GPIO_BTN_DOWN);
        bool btn_enter_pressed = !gpio_get_level(GPIO_BTN_ENTER);

        /* Update button state machine */
        button_update_up(&g_button_ctrl, btn_up_pressed, current_tick_ms);
        button_update_down(&g_button_ctrl, btn_down_pressed, current_tick_ms);
        button_update_enter(&g_button_ctrl, btn_enter_pressed, current_tick_ms);

        /* Log detected button actions */
        ButtonAction up_action = button_get_up_action(&g_button_ctrl);
        ButtonAction down_action = button_get_down_action(&g_button_ctrl);
        ButtonAction enter_action = button_get_enter_action(&g_button_ctrl);

        if (up_action != BUTTON_ACTION_NONE) {
            ESP_LOGD(TAG, "Button UP pressed");
        }
        if (down_action != BUTTON_ACTION_NONE) {
            ESP_LOGD(TAG, "Button DOWN pressed");
        }
        if (enter_action != BUTTON_ACTION_NONE) {
            ESP_LOGD(TAG, "Button ENTER pressed");
        }

        last_tick_ms = current_tick_ms;
        vTaskDelay(pdMS_TO_TICKS(50));  /* Poll every 50ms */
    }

    vTaskDelete(NULL);
}

/* ==================== Mode Task (100ms update) ==================== */

/**
 * @brief Task for mode state machine and menu navigation
 * Processes button actions and transitions between modes
 */
static void mode_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Mode task started");

    mode_init(&g_mode_ctrl);

    while (1) {
        /* Read button actions from button controller */
        ButtonAction up_action = button_get_up_action(&g_button_ctrl);
        ButtonAction down_action = button_get_down_action(&g_button_ctrl);
        ButtonAction enter_action = button_get_enter_action(&g_button_ctrl);

        /* Process button actions */
        if (up_action == BUTTON_ACTION_UP) {
            mode_handle_up(&g_mode_ctrl);
            ESP_LOGD(TAG, "Mode: UP pressed, menu index=%u", mode_get_selected_index(&g_mode_ctrl));
        }

        if (down_action == BUTTON_ACTION_DOWN) {
            mode_handle_down(&g_mode_ctrl);
            ESP_LOGD(TAG, "Mode: DOWN pressed, menu index=%u", mode_get_selected_index(&g_mode_ctrl));
        }

        if (enter_action == BUTTON_ACTION_ENTER) {
            mode_handle_enter(&g_mode_ctrl);
            ESP_LOGI(TAG, "Mode: ENTER pressed, current_mode=%u", mode_get(&g_mode_ctrl));
        }

        /* Clear button actions after processing */
        button_clear_actions(&g_button_ctrl);

        vTaskDelay(pdMS_TO_TICKS(100));  /* Update mode every 100ms */
    }

    vTaskDelete(NULL);
}

/* ==================== Motor Task (100ms update) ==================== */

/**
 * @brief Task for motor control logic
 * In AUTO mode: responds to LDR sensor state
 * In MANUAL mode: responds to button commands
 */
static void motor_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Motor task started");

    motor_init(&g_motor_ctrl);

    while (1) {
        OperatingMode current_mode = mode_get(&g_mode_ctrl);
        MotorState desired_state = MOTOR_STOPPED;

        if (current_mode == MODE_AUTO) {
            /* AUTO mode: LDR sensor drives motor */
            bool is_bright = ldr_is_bright();

            if (is_bright) {
                /* Light is bright: open the blinds */
                desired_state = MOTOR_OPENING;
            } else {
                /* Light is dark: close the blinds */
                desired_state = MOTOR_CLOSING;
            }

            ESP_LOGD(TAG, "AUTO mode: LDR bright=%d, motor=%u", is_bright, desired_state);

        } else if (current_mode == MODE_MANUAL) {
            /* MANUAL mode: buttons drive motor */
            ButtonAction up_action = button_get_up_action(&g_button_ctrl);
            ButtonAction down_action = button_get_down_action(&g_button_ctrl);

            if (up_action == BUTTON_ACTION_UP) {
                desired_state = MOTOR_OPENING;
                ESP_LOGD(TAG, "MANUAL mode: UP pressed -> OPENING");
            } else if (down_action == BUTTON_ACTION_DOWN) {
                desired_state = MOTOR_CLOSING;
                ESP_LOGD(TAG, "MANUAL mode: DOWN pressed -> CLOSING");
            }

        } else {
            /* MENU mode: stop motor */
            desired_state = MOTOR_STOPPED;
        }

        /* Update motor controller */
        bool state_changed = motor_set_desired(&g_motor_ctrl, desired_state);

        if (state_changed) {
            MotorState current = motor_get_current(&g_motor_ctrl);
            ESP_LOGI(TAG, "Motor state changed to: %u", current);

            /* TODO: Implement GPIO control for L298N motor driver
             * Example:
             * switch (current) {
             *     case MOTOR_OPENING:
             *         gpio_set_level(GPIO_MOTOR_IN1, 1);
             *         gpio_set_level(GPIO_MOTOR_IN2, 0);
             *         break;
             *     case MOTOR_CLOSING:
             *         gpio_set_level(GPIO_MOTOR_IN1, 0);
             *         gpio_set_level(GPIO_MOTOR_IN2, 1);
             *         break;
             *     case MOTOR_STOPPED:
             *         gpio_set_level(GPIO_MOTOR_IN1, 0);
             *         gpio_set_level(GPIO_MOTOR_IN2, 0);
             *         break;
             * }
             */

            motor_clear_changed_flag(&g_motor_ctrl);
        }

        vTaskDelay(pdMS_TO_TICKS(100));  /* Update motor every 100ms */
    }

    vTaskDelete(NULL);
}

/* ==================== Display Task (500ms update) ==================== */

/**
 * @brief Task for OLED display updates
 * Shows menu in MENU mode, operating info in AUTO/MANUAL modes
 */
static void display_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Display task started");

    oled_init();
    oled_clear();

    while (1) {
        OperatingMode current_mode = mode_get(&g_mode_ctrl);
        MotorState motor_state = motor_get_current(&g_motor_ctrl);

        if (current_mode == MODE_MENU) {
            /* Display menu screen */
            uint8_t selected_index = mode_get_selected_index(&g_mode_ctrl);
            oled_display_menu(selected_index);
            ESP_LOGD(TAG, "Display: MENU (selected=%u)", selected_index);

        } else {
            /* Display operating screen (AUTO or MANUAL) */
            uint8_t light_level = (ldr_get_light_level_1() + ldr_get_light_level_2()) / 2;
            oled_display_operating(current_mode, motor_state, light_level);
            ESP_LOGD(TAG, "Display: %s (motor=%u, light=%u%%)",
                     (current_mode == MODE_AUTO) ? "AUTO" : "MANUAL",
                     motor_state, light_level);
        }

        vTaskDelay(pdMS_TO_TICKS(500));  /* Update display every 500ms */
    }

    vTaskDelete(NULL);
}

/* ==================== System Initialization ==================== */

/**
 * @brief Initialize all system components in correct order
 */
static esp_err_t system_init(void)
{
    ESP_LOGI(TAG, "=== SunSense System Initialization ===");

    /* Step 1: GPIO configuration */
    ESP_RETURN_ON_ERROR(gpio_init(), TAG, "GPIO init failed");

    /* Step 2: LDR sensor initialization (creates its own FreeRTOS task) */
    ESP_RETURN_ON_ERROR(ldr_controller_init(), TAG, "LDR controller init failed");

    /* Step 3: OLED display initialization */
    oled_init();

    /* Step 4: Initialize state machines */
    mode_init(&g_mode_ctrl);
    motor_init(&g_motor_ctrl);
    button_init(&g_button_ctrl);

    ESP_LOGI(TAG, "System initialization complete");
    return ESP_OK;
}

/**
 * @brief Create application tasks
 */
static esp_err_t tasks_create(void)
{
    ESP_LOGI(TAG, "Creating FreeRTOS tasks");

    /* Button polling task (50ms) - Priority 6 */
    if (xTaskCreate(button_task, "button_task", 2048, NULL, 6, &g_button_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return ESP_FAIL;
    }

    /* Mode state machine task (100ms) - Priority 5 */
    if (xTaskCreate(mode_task, "mode_task", 2048, NULL, 5, &g_mode_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create mode task");
        return ESP_FAIL;
    }

    /* Motor control task (100ms) - Priority 5 */
    if (xTaskCreate(motor_task, "motor_task", 2048, NULL, 5, &g_motor_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create motor task");
        return ESP_FAIL;
    }

    /* Display update task (500ms) - Priority 4 */
    if (xTaskCreate(display_task, "display_task", 3072, NULL, 4, &g_display_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "All tasks created successfully");
    return ESP_OK;
}

/* ==================== Main Entry Point ==================== */

/**
 * @brief Main application entry point
 */
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "\n\n================= SunSense Blinds System =================");
    ESP_LOGI(TAG, "Starting up...\n");

    /* Initialize system components */
    if (system_init() != ESP_OK) {
        ESP_LOGE(TAG, "System initialization failed!");
        return;
    }

    /* Create application tasks */
    if (tasks_create() != ESP_OK) {
        ESP_LOGE(TAG, "Task creation failed!");
        return;
    }

    ESP_LOGI(TAG, "================= System Ready =================\n");

    /* Main thread sleeps; all work done in FreeRTOS tasks */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  /* Check every 10s */
        ESP_LOGD(TAG, "Heartbeat: System running");
    }
}
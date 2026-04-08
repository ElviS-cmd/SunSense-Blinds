/**
 * @file button_controller.c
 * @brief Button Controller Implementation
 * @author Elvis
 * @date 2026
 */

#include "button_controller.h"
#include "gpio_config.h"
#include "driver/gpio.h"
#include <string.h>

/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

bool button_init(ButtonController_t *btn) {
    memset(btn, 0, sizeof(ButtonController_t));

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    return (gpio_config(&io_conf) == ESP_OK);
}

/* ============================================================================
 * STATE MACHINE
 * ========================================================================== */

void button_update(ButtonController_t *btn, uint32_t current_time) {
    /* Read button state (active LOW) */
    int pin_level = gpio_get_level(GPIO_BUTTON);
    bool button_pressed_now = (pin_level == 0);  // 0 = pressed, 1 = released
    
    /* Detect press edge */
    if (button_pressed_now && !btn->is_pressed) {
        btn->is_pressed = true;
        btn->press_start_time = current_time;
        btn->current_action = BUTTON_ACTION_NONE;
    }
    
    /* Detect release edge */
    if (!button_pressed_now && btn->is_pressed) {
        btn->is_pressed = false;
        
        uint32_t press_duration = current_time - btn->press_start_time;
        
        /* Debounce check */
        if (press_duration < BUTTON_DEBOUNCE_MS) {
            return;  // Too short, noise
        }
        
        /* Classify action */
        if (press_duration >= BUTTON_HOLD_TIME_MS) {
            btn->current_action = BUTTON_ACTION_LONG;
        } else {
            btn->current_action = BUTTON_ACTION_SHORT;
        }
        
        btn->last_action_time = current_time;
    }
}

/* ============================================================================
 * ACTION RETRIEVAL
 * ========================================================================== */

ButtonAction_t button_get_action(ButtonController_t *btn) {
    return btn->current_action;
}

void button_clear_action(ButtonController_t *btn) {
    btn->current_action = BUTTON_ACTION_NONE;
}

bool button_is_pressed(ButtonController_t *btn) {
    return btn->is_pressed;
}

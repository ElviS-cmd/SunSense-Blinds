/**
 * @file led_controller.c
 * @brief LED Controller Implementation
 * @author Elvis
 * @date 2026
 */

#include "led_controller.h"
#include "gpio_config.h"
#include "driver/gpio.h"
#include <string.h>

/* ============================================================================
 * BLINK/PULSE TIMING CONSTANTS
 * ========================================================================== */

#define BLINK_SLOW_PERIOD_MS    500     // 500ms on/off for slow blink
#define BLINK_FAST_PERIOD_MS    250     // 250ms on/off for fast blink
#define PULSE_PERIOD_MS         1000    // 1s period for pulse effect

/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

bool led_init(LEDController_t *led) {
    memset(led, 0, sizeof(LEDController_t));
    led->green_state = LED_OFF;
    led->blue_state = LED_OFF;
    led->green_is_on = false;
    led->blue_is_on = false;
    
    /* Configure GPIO5 (Green LED) as output */
    gpio_config_t io_conf_green = {
        .pin_bit_mask = (1ULL << GPIO_LED_GREEN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    if (gpio_config(&io_conf_green) != ESP_OK) {
        return false;
    }
    
    /* Configure GPIO6 (Blue LED) as output */
    gpio_config_t io_conf_blue = {
        .pin_bit_mask = (1ULL << GPIO_LED_BLUE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    if (gpio_config(&io_conf_blue) != ESP_OK) {
        return false;
    }
    
    /* Start with all LEDs off */
    gpio_set_level(GPIO_LED_GREEN, 0);
    gpio_set_level(GPIO_LED_BLUE, 0);

    return true;
}

/* ============================================================================
 * LED CONTROL
 * ========================================================================== */

void led_set_green(LEDController_t *led, LEDState_t state) {
    led->green_state = state;
    
    if (state == LED_OFF) {
        gpio_set_level(GPIO_LED_GREEN, 0);
        led->green_is_on = false;
    } else if (state == LED_ON) {
        gpio_set_level(GPIO_LED_GREEN, 1);
        led->green_is_on = true;
    }
    /* Blinking states are handled in led_update() */
}

void led_set_blue(LEDController_t *led, LEDState_t state) {
    led->blue_state = state;
    
    if (state == LED_OFF) {
        gpio_set_level(GPIO_LED_BLUE, 0);
        led->blue_is_on = false;
    } else if (state == LED_ON) {
        gpio_set_level(GPIO_LED_BLUE, 1);
        led->blue_is_on = true;
    }
    /* Blinking states are handled in led_update() */
}

/* ============================================================================
 * PERIODIC UPDATE (handles blinking/pulsing)
 * ========================================================================== */

void led_update(LEDController_t *led, uint32_t current_time) {
    /* Update green LED */
    if (led->green_state == LED_BLINK_SLOW) {
        if ((current_time - led->green_last_toggle_time) >= BLINK_SLOW_PERIOD_MS) {
            led->green_is_on = !led->green_is_on;
            gpio_set_level(GPIO_LED_GREEN, led->green_is_on ? 1 : 0);
            led->green_last_toggle_time = current_time;
        }
    } else if (led->green_state == LED_BLINK_FAST) {
        if ((current_time - led->green_last_toggle_time) >= BLINK_FAST_PERIOD_MS) {
            led->green_is_on = !led->green_is_on;
            gpio_set_level(GPIO_LED_GREEN, led->green_is_on ? 1 : 0);
            led->green_last_toggle_time = current_time;
        }
    } else if (led->green_state == LED_PULSE) {
        if ((current_time - led->green_last_toggle_time) >= (PULSE_PERIOD_MS / 2)) {
            led->green_is_on = !led->green_is_on;
            gpio_set_level(GPIO_LED_GREEN, led->green_is_on ? 1 : 0);
            led->green_last_toggle_time = current_time;
        }
    }
    
    /* Update blue LED */
    if (led->blue_state == LED_BLINK_SLOW) {
        if ((current_time - led->blue_last_toggle_time) >= BLINK_SLOW_PERIOD_MS) {
            led->blue_is_on = !led->blue_is_on;
            gpio_set_level(GPIO_LED_BLUE, led->blue_is_on ? 1 : 0);
            led->blue_last_toggle_time = current_time;
        }
    } else if (led->blue_state == LED_BLINK_FAST) {
        if ((current_time - led->blue_last_toggle_time) >= BLINK_FAST_PERIOD_MS) {
            led->blue_is_on = !led->blue_is_on;
            gpio_set_level(GPIO_LED_BLUE, led->blue_is_on ? 1 : 0);
            led->blue_last_toggle_time = current_time;
        }
    } else if (led->blue_state == LED_PULSE) {
        if ((current_time - led->blue_last_toggle_time) >= (PULSE_PERIOD_MS / 2)) {
            led->blue_is_on = !led->blue_is_on;
            gpio_set_level(GPIO_LED_BLUE, led->blue_is_on ? 1 : 0);
            led->blue_last_toggle_time = current_time;
        }
    }
}

/* ============================================================================
 * CONVENIENCE FUNCTIONS
 * ========================================================================== */

void led_all_on(LEDController_t *led) {
    led_set_green(led, LED_ON);
    led_set_blue(led, LED_ON);
}

void led_all_off(LEDController_t *led) {
    led_set_green(led, LED_OFF);
    led_set_blue(led, LED_OFF);
}

bool led_get_green(LEDController_t *led) {
    return led->green_is_on;
}

bool led_get_blue(LEDController_t *led) {
    return led->blue_is_on;
}

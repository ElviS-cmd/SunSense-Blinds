/**
 * @file motor_controller.c
 * @brief Motor Controller Implementation
 * @author Elvis
 * @date 2026
 */

#include "motor_controller.h"
#include "gpio_config.h"
#include "driver/gpio.h"
#include <string.h>

/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

bool motor_init(MotorController_t *motor) {
    memset(motor, 0, sizeof(MotorController_t));
    motor->current_state = MOTOR_STOP;
    
    /* Configure GPIO12 (IN1) as output */
    gpio_config_t io_conf_in1 = {
        .pin_bit_mask = (1ULL << GPIO_MOTOR_IN1),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    if (gpio_config(&io_conf_in1) != ESP_OK) {
        return false;
    }
    
    /* Configure GPIO13 (IN2) as output */
    gpio_config_t io_conf_in2 = {
        .pin_bit_mask = (1ULL << GPIO_MOTOR_IN2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    if (gpio_config(&io_conf_in2) != ESP_OK) {
        return false;
    }
    
    /* Start in STOP state */
    gpio_set_level(GPIO_MOTOR_IN1, 0);
    gpio_set_level(GPIO_MOTOR_IN2, 0);

    return true;
}

/* ============================================================================
 * MOTOR CONTROL
 * ========================================================================== */

void motor_set_opening(MotorController_t *motor, uint32_t current_time) {
    if (motor->current_state == MOTOR_OPENING) {
        return;  // Already opening
    }
    
    /* IN1=HIGH, IN2=LOW -> motor forward */
    gpio_set_level(GPIO_MOTOR_IN1, 1);
    gpio_set_level(GPIO_MOTOR_IN2, 0);
    
    motor->current_state = MOTOR_OPENING;
    motor->state_start_time = current_time;
}

void motor_set_closing(MotorController_t *motor, uint32_t current_time) {
    if (motor->current_state == MOTOR_CLOSING) {
        return;  // Already closing
    }
    
    /* IN1=LOW, IN2=HIGH -> motor reverse */
    gpio_set_level(GPIO_MOTOR_IN1, 0);
    gpio_set_level(GPIO_MOTOR_IN2, 1);
    
    motor->current_state = MOTOR_CLOSING;
    motor->state_start_time = current_time;
}

void motor_stop(MotorController_t *motor, uint32_t current_time) {
    if (motor->current_state == MOTOR_STOP) {
        return;  // Already stopped
    }
    
    /* IN1=LOW, IN2=LOW -> motor stop */
    gpio_set_level(GPIO_MOTOR_IN1, 0);
    gpio_set_level(GPIO_MOTOR_IN2, 0);
    
    motor->current_state = MOTOR_STOP;
    motor->state_start_time = current_time;
}

/* ============================================================================
 * STATE QUERIES
 * ========================================================================== */

MotorState_t motor_get_state(MotorController_t *motor) {
    return motor->current_state;
}

bool motor_is_running(MotorController_t *motor) {
    return (motor->current_state != MOTOR_STOP);
}

uint32_t motor_get_elapsed_time(MotorController_t *motor, uint32_t current_time) {
    return (current_time - motor->state_start_time);
}

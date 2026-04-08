/**
 * @file servo_controller.c
 * @brief Servo Controller Implementation
 * @author Elvis
 * @date 2026
 */

#include "servo_controller.h"
#include "gpio_config.h"
#include "driver/ledc.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 * SERVO TIMING CONSTANTS
 * ========================================================================== */

#define SERVO_MOVEMENT_SPEED_DEG_PER_MS  0.5f  // Degrees per millisecond

/* ============================================================================
 * PWM DUTY CALCULATION
 * ========================================================================== */

static uint32_t angle_to_duty(float angle) {
    /* Clamp angle to 0-180 */
    if (angle < 0.0f) angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;
    
    /* Linear interpolation from angle to duty cycle */
    /* 0° = 1000µs, 180° = 2000µs */
    float pulse_us = 1000.0f + (angle * 1000.0f / 180.0f);
    
    /* Convert microseconds to duty cycle (12-bit @ 50Hz) */
    /* Period = 20ms = 20000µs */
    /* Duty = (pulse_us / 20000) * 4095 */
    uint32_t duty = (uint32_t)((pulse_us / 20000.0f) * 4095.0f);
    
    return duty;
}

/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

bool servo_init(ServoController_t *servo) {
    memset(servo, 0, sizeof(ServoController_t));
    servo->current_angle = 90.0f;   // Start at center
    servo->target_angle = 90.0f;
    servo->state = SERVO_IDLE;
    servo->is_moving = false;
    
    /* Configure LEDC timer */
    ledc_timer_config_t ledc_timer = {
        .speed_mode = SERVO_PWM_MODE,
        .timer_num = SERVO_PWM_TIMER,
        .duty_resolution = SERVO_PWM_RESOLUTION,
        .freq_hz = SERVO_PWM_FREQ,
    };
    
    if (ledc_timer_config(&ledc_timer) != ESP_OK) {
        return false;
    }
    
    /* Configure LEDC channel */
    ledc_channel_config_t ledc_channel = {
        .speed_mode = SERVO_PWM_MODE,
        .channel = SERVO_PWM_CHANNEL,
        .timer_sel = SERVO_PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = GPIO_SERVO,
        .duty = angle_to_duty(90.0f),  // Start at 90°
        .hpoint = 0,
    };
    
    if (ledc_channel_config(&ledc_channel) != ESP_OK) {
        return false;
    }
    
    /* Set initial PWM */
    ledc_set_duty(SERVO_PWM_MODE, SERVO_PWM_CHANNEL, angle_to_duty(90.0f));
    ledc_update_duty(SERVO_PWM_MODE, SERVO_PWM_CHANNEL);
    
    return true;
}

/* ============================================================================
 * SERVO MOVEMENT CONTROL
 * ========================================================================== */

void servo_move_to(ServoController_t *servo, float target_angle, uint32_t current_time) {
    /* Clamp target angle */
    if (target_angle < 0.0f) target_angle = 0.0f;
    if (target_angle > 180.0f) target_angle = 180.0f;
    
    servo->target_angle = target_angle;
    servo->move_start_time = current_time;
    servo->is_moving = true;
    servo->state = SERVO_MOVING;
    
    /* Calculate expected movement duration */
    float angle_diff = fabs(target_angle - servo->current_angle);
    servo->move_duration_ms = (uint32_t)(angle_diff / SERVO_MOVEMENT_SPEED_DEG_PER_MS);
    
    /* Immediately set PWM to target */
    uint32_t duty = angle_to_duty(target_angle);
    ledc_set_duty(SERVO_PWM_MODE, SERVO_PWM_CHANNEL, duty);
    ledc_update_duty(SERVO_PWM_MODE, SERVO_PWM_CHANNEL);
}

void servo_open(ServoController_t *servo, uint32_t current_time) {
    servo_move_to(servo, 0.0f, current_time);
}

void servo_close(ServoController_t *servo, uint32_t current_time) {
    servo_move_to(servo, 180.0f, current_time);
}

void servo_center(ServoController_t *servo, uint32_t current_time) {
    servo_move_to(servo, 90.0f, current_time);
}

/* ============================================================================
 * PERIODIC UPDATE
 * ========================================================================== */

void servo_update(ServoController_t *servo, uint32_t current_time) {
    if (!servo->is_moving) {
        return;
    }
    
    uint32_t elapsed = current_time - servo->move_start_time;
    
    /* Check if movement is complete */
    if (elapsed >= servo->move_duration_ms) {
        servo->current_angle = servo->target_angle;
        servo->is_moving = false;
        servo->state = SERVO_IDLE;
    } else {
        /* Interpolate current angle during movement */
        float progress = (float)elapsed / (float)servo->move_duration_ms;
        float angle_diff = servo->target_angle - servo->current_angle;
        servo->current_angle = servo->current_angle + (angle_diff * progress);
    }
}

/* ============================================================================
 * STATE QUERIES
 * ========================================================================== */

float servo_get_angle(ServoController_t *servo) {
    return servo->current_angle;
}

float servo_get_target(ServoController_t *servo) {
    return servo->target_angle;
}

bool servo_at_target(ServoController_t *servo) {
    float diff = fabs(servo->current_angle - servo->target_angle);
    return (diff < 1.0f);  // Within 1° tolerance
}

bool servo_is_moving(ServoController_t *servo) {
    return servo->is_moving;
}

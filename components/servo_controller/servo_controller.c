/**
 * @file servo_controller.c
 * @brief Deterministic MG996R positional servo controller implementation
 * @author Elvis
 * @date 2026
 */

#include "servo_controller.h"
#include "gpio_config.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

/* ============================================================================
 * SERVO CONSTANTS
 * ========================================================================== */

#define SERVO_PHYSICAL_MIN_ANGLE_DEG 0.0f
#define SERVO_PHYSICAL_MAX_ANGLE_DEG 180.0f
#define SERVO_PWM_PERIOD_US       (1000000UL / SERVO_PWM_FREQ)
#define SERVO_DUTY_MAX_12_BIT     ((1UL << 12) - 1UL)
#define SERVO_SAME_ANGLE_EPSILON  0.5f

static const char *TAG = "Servo";

/* ============================================================================
 * PWM DUTY CALCULATION
 * ========================================================================== */

static float clamp_angle(float angle) {
    if (angle < SERVO_PHYSICAL_MIN_ANGLE_DEG) {
        return SERVO_PHYSICAL_MIN_ANGLE_DEG;
    }
    if (angle > SERVO_PHYSICAL_MAX_ANGLE_DEG) {
        return SERVO_PHYSICAL_MAX_ANGLE_DEG;
    }
    return angle;
}

uint32_t servo_angle_to_duty(float angle) {
    angle = clamp_angle(angle);

    float pulse_us = (float)SERVO_PWM_MIN_US +
        ((angle / SERVO_PHYSICAL_MAX_ANGLE_DEG) * (float)(SERVO_PWM_MAX_US - SERVO_PWM_MIN_US));

    return (uint32_t)lroundf((pulse_us * (float)SERVO_DUTY_MAX_12_BIT) /
                             (float)SERVO_PWM_PERIOD_US);
}

/* ============================================================================
 * LOW-LEVEL PWM
 * ========================================================================== */

static bool servo_apply_pwm(ServoController_t *servo, float angle) {
    uint32_t duty = servo_angle_to_duty(angle);

    esp_err_t err = ledc_set_duty(SERVO_PWM_MODE, SERVO_PWM_CHANNEL, duty);
    if (err == ESP_OK) {
        err = ledc_update_duty(SERVO_PWM_MODE, SERVO_PWM_CHANNEL);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PWM apply failed angle=%.1f duty=%lu err=%s",
                 angle,
                 (unsigned long)duty,
                 esp_err_to_name(err));
        return false;
    }

    servo->current_duty = duty;
    ESP_LOGI(TAG, "PWM applied angle=%.1f duty=%lu",
             angle,
             (unsigned long)duty);
    return true;
}

static float smoothstep(float progress) {
    if (progress <= 0.0f) {
        return 0.0f;
    }
    if (progress >= 1.0f) {
        return 1.0f;
    }
    return progress * progress * (3.0f - (2.0f * progress));
}

static float ramp_angle_at_time(ServoController_t *servo, uint32_t current_time) {
    if (servo->ramp_duration_ms == 0U) {
        return servo->target_angle;
    }

    uint32_t elapsed = current_time - servo->command_time_ms;
    float progress = (float)elapsed / (float)servo->ramp_duration_ms;
    float eased = smoothstep(progress);
    return servo->start_angle + ((servo->target_angle - servo->start_angle) * eased);
}

static bool servo_apply_ramp_sample(ServoController_t *servo, uint32_t current_time, const char *reason) {
    float next_angle = ramp_angle_at_time(servo, current_time);
    if (fabsf(next_angle - servo->target_angle) <= SERVO_SAME_ANGLE_EPSILON ||
        (current_time - servo->command_time_ms) >= servo->ramp_duration_ms) {
        next_angle = servo->target_angle;
    }

    if (!servo_apply_pwm(servo, next_angle)) {
        servo->state = SERVO_IDLE;
        servo->is_moving = false;
        return false;
    }

    servo->current_angle = next_angle;
    servo->last_step_time_ms = current_time;

    ESP_LOGI(TAG,
             "Ramp sample (%s): angle=%.1f target=%.1f duty=%lu",
             reason,
             servo->current_angle,
             servo->target_angle,
             (unsigned long)servo->current_duty);

    if (fabsf(servo->current_angle - servo->target_angle) <= SERVO_SAME_ANGLE_EPSILON) {
        servo->current_angle = servo->target_angle;
        servo->target_pwm_applied = true;
        servo->settle_start_time_ms = current_time;
        ESP_LOGI(TAG,
                 "Target PWM applied angle=%.1f duty=%lu",
                 servo->target_angle,
                 (unsigned long)servo->current_duty);
    }

    return true;
}

/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

bool servo_init(ServoController_t *servo) {
    if (servo == NULL) {
        ESP_LOGE(TAG, "Init failed: servo pointer is NULL");
        return false;
    }

    memset(servo, 0, sizeof(ServoController_t));
    const float initial_angle = 90.0f;

    servo->current_angle = initial_angle;
    servo->target_angle = initial_angle;
    servo->start_angle = initial_angle;
    servo->state = SERVO_IDLE;
    servo->settle_duration_ms = SERVO_SETTLE_MS;
    servo->ramp_duration_ms = SERVO_RAMP_DURATION_MS;
    servo->target_pwm_applied = true;
    servo->is_moving = false;

    ledc_timer_config_t ledc_timer = {
        .speed_mode = SERVO_PWM_MODE,
        .timer_num = SERVO_PWM_TIMER,
        .duty_resolution = SERVO_PWM_RESOLUTION,
        .freq_hz = SERVO_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer init failed: %s", esp_err_to_name(err));
        return false;
    }

    uint32_t initial_duty = servo_angle_to_duty(initial_angle);
    ledc_channel_config_t ledc_channel = {
        .gpio_num = GPIO_SERVO,
        .speed_mode = SERVO_PWM_MODE,
        .channel = SERVO_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_PWM_TIMER,
        .duty = initial_duty,
        .hpoint = 0,
        .flags = {
            .output_invert = 0,
        },
    };

    err = ledc_channel_config(&ledc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel init failed: %s", esp_err_to_name(err));
        return false;
    }

    servo->current_duty = initial_duty;
    servo->command_valid = true;
    ESP_LOGI(TAG,
             "Initialized GPIO=%d freq=%dHz resolution=12bit pulse=%dus-%dus initial_angle=%.1f duty=%lu",
             GPIO_SERVO,
             SERVO_PWM_FREQ,
             SERVO_PWM_MIN_US,
             SERVO_PWM_MAX_US,
             initial_angle,
             (unsigned long)initial_duty);
    return true;
}

/* ============================================================================
 * SERVO MOVEMENT CONTROL
 * ========================================================================== */

void servo_move_to_ex(ServoController_t *servo,
                      float target_angle,
                      uint32_t current_time,
                      bool force_reapply) {
    if (servo == NULL || !servo->command_valid) {
        ESP_LOGW(TAG, "Command skipped: servo not initialized");
        return;
    }

    float clamped_target = clamp_angle(target_angle);
    if (fabsf(clamped_target - target_angle) > SERVO_SAME_ANGLE_EPSILON) {
        ESP_LOGW(TAG, "Command angle clamped requested=%.1f applied=%.1f",
                 target_angle,
                 clamped_target);
    }

    bool same_target = fabsf(clamped_target - servo->target_angle) <= SERVO_SAME_ANGLE_EPSILON;

    if (same_target && !force_reapply) {
        ESP_LOGI(TAG,
                 "Command skipped: target unchanged angle=%.1f duty=%lu state=%s",
                 clamped_target,
                 (unsigned long)servo->current_duty,
                 servo->state == SERVO_MOVING ? "moving" : "idle");
        return;
    }

    if (same_target && force_reapply) {
        servo->start_angle = servo->current_angle;
        servo->target_angle = clamped_target;
        servo->command_time_ms = current_time;
        servo->last_step_time_ms = current_time;
        servo->settle_start_time_ms = current_time;
        servo->target_pwm_applied = true;
        servo->state = SERVO_MOVING;
        servo->is_moving = true;

        if (!servo_apply_pwm(servo, clamped_target)) {
            servo->state = SERVO_IDLE;
            servo->is_moving = false;
            return;
        }

        servo->current_angle = clamped_target;
        servo->command_count++;
        ESP_LOGI(TAG,
                 "Commanded target=%.1f reapply duty=%lu command_count=%lu",
                 servo->target_angle,
                 (unsigned long)servo->current_duty,
                 (unsigned long)servo->command_count);
        return;
    }

    servo->start_angle = servo->current_angle;
    servo->target_angle = clamped_target;
    servo->command_time_ms = current_time;
    servo->last_step_time_ms = current_time;
    servo->settle_start_time_ms = current_time;
    servo->ramp_duration_ms = SERVO_RAMP_DURATION_MS;
    servo->target_pwm_applied = false;
    servo->state = SERVO_MOVING;
    servo->is_moving = true;

    if (!servo_apply_ramp_sample(servo, current_time, "command")) {
        return;
    }

    servo->command_count++;
    ESP_LOGI(TAG,
             "Commanded target=%.1f start=%.1f current=%.1f duty=%lu force=%s command_count=%lu",
             servo->target_angle,
             servo->start_angle,
             servo->current_angle,
             (unsigned long)servo->current_duty,
             force_reapply ? "yes" : "no",
             (unsigned long)servo->command_count);
}

void servo_move_to(ServoController_t *servo, float target_angle, uint32_t current_time) {
    servo_move_to_ex(servo, target_angle, current_time, true);
}

void servo_move_to_if_changed(ServoController_t *servo, float target_angle, uint32_t current_time) {
    servo_move_to_ex(servo, target_angle, current_time, false);
}

void servo_open(ServoController_t *servo, uint32_t current_time) {
    servo_move_to(servo, SERVO_SLAT_OPEN_ANGLE, current_time);
}

void servo_close(ServoController_t *servo, uint32_t current_time) {
    servo_move_to(servo, SERVO_SLAT_CLOSED_ANGLE, current_time);
}

void servo_center(ServoController_t *servo, uint32_t current_time) {
    servo_move_to(servo, 90.0f, current_time);
}

/* ============================================================================
 * PERIODIC UPDATE
 * ========================================================================== */

void servo_update(ServoController_t *servo, uint32_t current_time) {
    if (servo == NULL || !servo->is_moving) {
        return;
    }

    if (!servo->target_pwm_applied) {
        if ((current_time - servo->last_step_time_ms) < SERVO_RAMP_PERIOD_MS) {
            return;
        }
        servo_apply_ramp_sample(servo, current_time, "update");
        return;
    }

    uint32_t elapsed = current_time - servo->settle_start_time_ms;
    if (elapsed < servo->settle_duration_ms) {
        return;
    }

    servo->is_moving = false;
    servo->state = SERVO_IDLE;

    ESP_LOGI(TAG,
             "Settled angle=%.1f duty=%lu settle_elapsed=%lums total_elapsed=%lums",
             servo->current_angle,
             (unsigned long)servo->current_duty,
             (unsigned long)elapsed,
             (unsigned long)(current_time - servo->command_time_ms));
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
    return servo != NULL && servo->command_valid && !servo->is_moving;
}

bool servo_is_moving(ServoController_t *servo) {
    return servo != NULL && servo->is_moving;
}

uint32_t servo_get_duty(ServoController_t *servo) {
    return servo != NULL ? servo->current_duty : 0U;
}

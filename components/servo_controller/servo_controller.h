/**
 * @file servo_controller.h
 * @brief Deterministic MG996R positional servo controller for SunSense V2
 * @author Elvis
 * @date 2026
 *
 * Drives a standard positional servo using ESP-IDF LEDC PWM on GPIO10.
 * The controller tracks commanded angles only. It does not estimate physical
 * intermediate position because the MG996R provides no position feedback.
 */

#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SERVO CONTROLLER STRUCT
 * ========================================================================== */

typedef struct {
    float current_angle;            // Last commanded angle considered settled
    float target_angle;             // Last commanded target angle
    float start_angle;              // Commanded angle before latest move command
    uint32_t current_duty;          // Last LEDC duty written
    ServoState_t state;             // SERVO_MOVING until settle time expires
    uint32_t command_time_ms;       // Time the latest PWM command was applied
    uint32_t settle_duration_ms;    // Conservative no-feedback settle delay
    uint32_t command_count;         // Number of PWM commands applied
    bool command_valid;             // True after successful initialization
    bool is_moving;                 // Software settle timer is active
} ServoController_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ========================================================================== */

/**
 * @brief Initialize servo controller and PWM.
 * @param servo Pointer to ServoController_t structure
 * @return true if initialization successful, false otherwise
 */
bool servo_init(ServoController_t *servo);

/**
 * @brief Move servo to target angle and re-apply PWM even if target is unchanged.
 * @param servo Pointer to ServoController_t structure
 * @param target_angle Target angle in degrees (0-180)
 * @param current_time Current time in milliseconds
 */
void servo_move_to(ServoController_t *servo, float target_angle, uint32_t current_time);

/**
 * @brief Move servo to target angle.
 * @param servo Pointer to ServoController_t structure
 * @param target_angle Target angle in degrees (0-180)
 * @param current_time Current time in milliseconds
 * @param force_reapply If true, write PWM even when the target did not change
 */
void servo_move_to_ex(ServoController_t *servo,
                      float target_angle,
                      uint32_t current_time,
                      bool force_reapply);

/**
 * @brief Move servo to slats open angle.
 * @param servo Pointer to ServoController_t structure
 * @param current_time Current time in milliseconds
 */
void servo_open(ServoController_t *servo, uint32_t current_time);

/**
 * @brief Move servo to slats closed angle.
 * @param servo Pointer to ServoController_t structure
 * @param current_time Current time in milliseconds
 */
void servo_close(ServoController_t *servo, uint32_t current_time);

/**
 * @brief Move servo to middle position (90 degrees).
 * @param servo Pointer to ServoController_t structure
 * @param current_time Current time in milliseconds
 */
void servo_center(ServoController_t *servo, uint32_t current_time);

/**
 * @brief Update software settle state. Does not estimate physical position.
 * @param servo Pointer to ServoController_t structure
 * @param current_time Current time in milliseconds
 */
void servo_update(ServoController_t *servo, uint32_t current_time);

/**
 * @brief Get last settled commanded angle.
 * @param servo Pointer to ServoController_t structure
 * @return Last settled commanded angle in degrees
 */
float servo_get_angle(ServoController_t *servo);

/**
 * @brief Get latest target angle.
 * @param servo Pointer to ServoController_t structure
 * @return Target angle in degrees
 */
float servo_get_target(ServoController_t *servo);

/**
 * @brief Check whether the latest command's settle time has expired.
 * @param servo Pointer to ServoController_t structure
 * @return true if latest command is settled, false otherwise
 */
bool servo_at_target(ServoController_t *servo);

/**
 * @brief Check if software settle timer is active.
 * @param servo Pointer to ServoController_t structure
 * @return true if settling, false if idle
 */
bool servo_is_moving(ServoController_t *servo);

/**
 * @brief Get the last LEDC duty written for debugging.
 * @param servo Pointer to ServoController_t structure
 * @return Last LEDC duty value
 */
uint32_t servo_get_duty(ServoController_t *servo);

/**
 * @brief Convert angle to LEDC duty using project PWM settings.
 * @param angle Angle in degrees. Values outside 0-180 are clamped.
 * @return LEDC duty value
 */
uint32_t servo_angle_to_duty(float angle);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_CONTROLLER_H */

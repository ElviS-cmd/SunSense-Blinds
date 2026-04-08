/**
 * @file servo_controller.h
 * @brief Servo Controller for SunSense V2
 * @author Elvis
 * @date 2026
 * 
 * MG996R servo motor control via PWM on GPIO10
 * Controls slat tilt angle (0-180°)
 */

#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * SERVO CONTROLLER STRUCT
 * ========================================================================== */

typedef struct {
    float current_angle;            // Current angle (0-180°)
    float target_angle;             // Target angle (0-180°)
    ServoState_t state;             // Current servo state
    uint32_t move_start_time;       // When movement started
    uint32_t move_duration_ms;      // Expected duration to reach target
    bool is_moving;                 // Is servo actively moving?
} ServoController_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ========================================================================== */

/**
 * @brief Initialize servo controller and PWM
 * @param servo Pointer to ServoController_t structure
 * @return true if initialization successful, false otherwise
 */
bool servo_init(ServoController_t *servo);

/**
 * @brief Move servo to target angle
 * @param servo Pointer to ServoController_t structure
 * @param target_angle Target angle in degrees (0-180)
 * @param current_time Current time in milliseconds
 */
void servo_move_to(ServoController_t *servo, float target_angle, uint32_t current_time);

/**
 * @brief Move servo to fully open (0°)
 * @param servo Pointer to ServoController_t structure
 * @param current_time Current time in milliseconds
 */
void servo_open(ServoController_t *servo, uint32_t current_time);

/**
 * @brief Move servo to fully closed (180°)
 * @param servo Pointer to ServoController_t structure
 * @param current_time Current time in milliseconds
 */
void servo_close(ServoController_t *servo, uint32_t current_time);

/**
 * @brief Move servo to middle position (90°)
 * @param servo Pointer to ServoController_t structure
 * @param current_time Current time in milliseconds
 */
void servo_center(ServoController_t *servo, uint32_t current_time);

/**
 * @brief Update servo movement (call periodically)
 * @param servo Pointer to ServoController_t structure
 * @param current_time Current time in milliseconds
 */
void servo_update(ServoController_t *servo, uint32_t current_time);

/**
 * @brief Get current servo angle
 * @param servo Pointer to ServoController_t structure
 * @return Current angle in degrees (0-180)
 */
float servo_get_angle(ServoController_t *servo);

/**
 * @brief Get target servo angle
 * @param servo Pointer to ServoController_t structure
 * @return Target angle in degrees (0-180)
 */
float servo_get_target(ServoController_t *servo);

/**
 * @brief Check if servo is at target
 * @param servo Pointer to ServoController_t structure
 * @return true if at target, false otherwise
 */
bool servo_at_target(ServoController_t *servo);

/**
 * @brief Check if servo is moving
 * @param servo Pointer to ServoController_t structure
 * @return true if moving, false if idle
 */
bool servo_is_moving(ServoController_t *servo);

#endif /* SERVO_CONTROLLER_H */

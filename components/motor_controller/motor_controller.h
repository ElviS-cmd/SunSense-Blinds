/**
 * @file motor_controller.h
 * @brief Motor Controller for SunSense V2
 * @author Elvis
 * @date 2026
 * 
 * Motor control via L298N H-bridge on GPIO12/GPIO13
 * Drives JGY-370 worm gear motor
 */

#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * MOTOR CONTROLLER STRUCT
 * ========================================================================== */

typedef struct {
    MotorState_t current_state;     // Current motor state
    uint32_t state_start_time;      // When state was entered
} MotorController_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ========================================================================== */

/**
 * @brief Initialize motor controller and GPIO pins
 * @param motor Pointer to MotorController_t structure
 * @return true if GPIO initialization succeeded
 */
bool motor_init(MotorController_t *motor);

/**
 * @brief Set motor to OPENING state (forward)
 * @param motor Pointer to MotorController_t structure
 * @param current_time Current time in milliseconds
 */
void motor_set_opening(MotorController_t *motor, uint32_t current_time);

/**
 * @brief Set motor to CLOSING state (reverse)
 * @param motor Pointer to MotorController_t structure
 * @param current_time Current time in milliseconds
 */
void motor_set_closing(MotorController_t *motor, uint32_t current_time);

/**
 * @brief Stop motor
 * @param motor Pointer to MotorController_t structure
 * @param current_time Current time in milliseconds
 */
void motor_stop(MotorController_t *motor, uint32_t current_time);

/**
 * @brief Get current motor state
 * @param motor Pointer to MotorController_t structure
 * @return MotorState_t (STOP, OPENING, or CLOSING)
 */
MotorState_t motor_get_state(MotorController_t *motor);

/**
 * @brief Check if motor is running
 * @param motor Pointer to MotorController_t structure
 * @return true if motor is OPENING or CLOSING, false if STOP
 */
bool motor_is_running(MotorController_t *motor);

/**
 * @brief Get time since last state change
 * @param motor Pointer to MotorController_t structure
 * @param current_time Current time in milliseconds
 * @return Elapsed time in milliseconds
 */
uint32_t motor_get_elapsed_time(MotorController_t *motor, uint32_t current_time);

#endif /* MOTOR_CONTROLLER_H */

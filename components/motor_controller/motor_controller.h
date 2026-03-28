#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

/* ============ MOTOR CONTROLLER STATE ============ */
typedef struct {
    MotorState current_state;      // What the motor is actually doing
    MotorState desired_state;      // What we want it to do
    bool state_changed;            // Flag: did state change this update?
} MotorController;

/* ============ PUBLIC FUNCTIONS ============ */

/**
 * Initialize motor controller (all stopped)
 */
void motor_init(MotorController *m);

/**
 * Set desired motor state
 * Returns true if state changed, false if already in that state
 */
bool motor_set_desired(MotorController *m, MotorState desired);

/**
 * Get current motor state
 */
MotorState motor_get_current(MotorController *m);

/**
 * Get desired motor state
 */
MotorState motor_get_desired(MotorController *m);

/**
 * Check if state changed in last update
 * (useful for knowing when to apply GPIO changes)
 */
bool motor_state_changed(MotorController *m);

/**
 * Clear the state_changed flag
 */
void motor_clear_changed_flag(MotorController *m);

#ifdef __cplusplus
}
#endif

#endif // MOTOR_CONTROLLER_H
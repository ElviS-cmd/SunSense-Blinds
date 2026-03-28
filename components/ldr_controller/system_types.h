#ifndef SYSTEM_TYPES_H
#define SYSTEM_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ============ OPERATING MODES ============ */
typedef enum {
    MODE_MENU = 0,
    MODE_AUTO = 1,
    MODE_MANUAL = 2
} OperatingMode;

/* ============ MOTOR STATES ============ */
typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_OPENING = 1,
    MOTOR_CLOSING = 2
} MotorState;

/* ============ BUTTON ACTIONS (Menu Navigation) ============ */
typedef enum {
    BUTTON_ACTION_NONE = 0,
    BUTTON_ACTION_UP = 1,
    BUTTON_ACTION_DOWN = 2,
    BUTTON_ACTION_ENTER = 3
} ButtonAction;

#endif // SYSTEM_TYPES_H

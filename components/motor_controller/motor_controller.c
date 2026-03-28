#include "motor_controller.h"

/* ============ INITIALIZE ============ */
void motor_init(MotorController *m) {
    m->current_state = MOTOR_STOPPED;
    m->desired_state = MOTOR_STOPPED;
    m->state_changed = false;
}

/* ============ SET DESIRED STATE ============ */
bool motor_set_desired(MotorController *m, MotorState desired) {
    // Check if state actually changed
    bool changed = (m->desired_state != desired);
    
    m->desired_state = desired;
    m->current_state = desired;  // Assume motor responds immediately
    m->state_changed = changed;
    
    return changed;
}

/* ============ GET CURRENT STATE ============ */
MotorState motor_get_current(MotorController *m) {
    return m->current_state;
}

/* ============ GET DESIRED STATE ============ */
MotorState motor_get_desired(MotorController *m) {
    return m->desired_state;
}

/* ============ CHECK IF STATE CHANGED ============ */
bool motor_state_changed(MotorController *m) {
    return m->state_changed;
}

/* ============ CLEAR CHANGED FLAG ============ */
void motor_clear_changed_flag(MotorController *m) {
    m->state_changed = false;
}
#include "button_controller.h"

/* ============ INITIALIZE ALL BUTTONS ============ */
void button_init(ButtonController *btn) {
    btn->btn_up.last_press_time = 0;
    btn->btn_up.press_start_time = 0;
    btn->btn_up.pin_currently_low = false;
    btn->btn_up.last_action = BUTTON_ACTION_NONE;
    
    btn->btn_down.last_press_time = 0;
    btn->btn_down.press_start_time = 0;
    btn->btn_down.pin_currently_low = false;
    btn->btn_down.last_action = BUTTON_ACTION_NONE;
    
    btn->btn_enter.last_press_time = 0;
    btn->btn_enter.press_start_time = 0;
    btn->btn_enter.pin_currently_low = false;
    btn->btn_enter.last_action = BUTTON_ACTION_NONE;
}

/* ============ UPDATE UP BUTTON ============ */
void button_update_up(ButtonController *btn, bool pin_low, uint32_t now_ms) {
    ButtonState *state = &btn->btn_up;
    
    if ((now_ms - state->last_press_time) < BUTTON_DEBOUNCE_MS) {
        return;
    }
    
    if (pin_low && !state->pin_currently_low) {
        state->pin_currently_low = true;
        state->press_start_time = now_ms;
    }
    
    if (!pin_low && state->pin_currently_low) {
        state->pin_currently_low = false;
        uint32_t press_duration = now_ms - state->press_start_time;
        state->last_press_time = now_ms;
        
        if (press_duration < BUTTON_HOLD_TIME_MS) {
            state->last_action = BUTTON_ACTION_UP;
        }
    }
}

/* ============ UPDATE DOWN BUTTON ============ */
void button_update_down(ButtonController *btn, bool pin_low, uint32_t now_ms) {
    ButtonState *state = &btn->btn_down;
    
    if ((now_ms - state->last_press_time) < BUTTON_DEBOUNCE_MS) {
        return;
    }
    
    if (pin_low && !state->pin_currently_low) {
        state->pin_currently_low = true;
        state->press_start_time = now_ms;
    }
    
    if (!pin_low && state->pin_currently_low) {
        state->pin_currently_low = false;
        uint32_t press_duration = now_ms - state->press_start_time;
        state->last_press_time = now_ms;
        
        if (press_duration < BUTTON_HOLD_TIME_MS) {
            state->last_action = BUTTON_ACTION_DOWN;
        }
    }
}

/* ============ UPDATE ENTER BUTTON ============ */
void button_update_enter(ButtonController *btn, bool pin_low, uint32_t now_ms) {
    ButtonState *state = &btn->btn_enter;
    
    if ((now_ms - state->last_press_time) < BUTTON_DEBOUNCE_MS) {
        return;
    }
    
    if (pin_low && !state->pin_currently_low) {
        state->pin_currently_low = true;
        state->press_start_time = now_ms;
    }
    
    if (!pin_low && state->pin_currently_low) {
        state->pin_currently_low = false;
        uint32_t press_duration = now_ms - state->press_start_time;
        state->last_press_time = now_ms;
        
        if (press_duration < BUTTON_HOLD_TIME_MS) {
            state->last_action = BUTTON_ACTION_ENTER;
        }
    }
}

/* ============ GET ACTIONS ============ */
ButtonAction button_get_up_action(ButtonController *btn) {
    return btn->btn_up.last_action;
}

ButtonAction button_get_down_action(ButtonController *btn) {
    return btn->btn_down.last_action;
}

ButtonAction button_get_enter_action(ButtonController *btn) {
    return btn->btn_enter.last_action;
}

/* ============ CLEAR ALL ACTIONS ============ */
void button_clear_actions(ButtonController *btn) {
    btn->btn_up.last_action = BUTTON_ACTION_NONE;
    btn->btn_down.last_action = BUTTON_ACTION_NONE;
    btn->btn_enter.last_action = BUTTON_ACTION_NONE;
}
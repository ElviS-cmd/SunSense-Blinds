#ifndef BUTTON_CONTROLLER_H
#define BUTTON_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

/* ============ GPIO PIN DEFINITIONS ============ */
#define GPIO_BTN_UP    (gpio_num_t)13
#define GPIO_BTN_DOWN  (gpio_num_t)12
#define GPIO_BTN_ENTER (gpio_num_t)32

/* ============ SINGLE BUTTON STATE ============ */
typedef struct {
    uint32_t last_press_time;
    uint32_t press_start_time;
    bool pin_currently_low;
    ButtonAction last_action;
} ButtonState;

/* ============ THREE-BUTTON CONTROLLER ============ */
typedef struct {
    ButtonState btn_up;
    ButtonState btn_down;
    ButtonState btn_enter;
} ButtonController;

/* ============ DEBOUNCE & TIMING ============ */
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_HOLD_TIME_MS 2000

/* ============ PUBLIC FUNCTIONS ============ */
void button_init(ButtonController *btn);
void button_update_up(ButtonController *btn, bool pin_low, uint32_t now_ms);
void button_update_down(ButtonController *btn, bool pin_low, uint32_t now_ms);
void button_update_enter(ButtonController *btn, bool pin_low, uint32_t now_ms);

ButtonAction button_get_up_action(ButtonController *btn);
ButtonAction button_get_down_action(ButtonController *btn);
ButtonAction button_get_enter_action(ButtonController *btn);

void button_clear_actions(ButtonController *btn);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_CONTROLLER_H
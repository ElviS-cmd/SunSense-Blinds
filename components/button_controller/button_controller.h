/**
 * @file button_controller.h
 * @brief Button Controller for SunSense V2
 * @author Elvis
 * @date 2026
 * 
 * Single button input with debounce and long press detection
 * Button on GPIO3 with internal pull-up (active LOW)
 */

#ifndef BUTTON_CONTROLLER_H
#define BUTTON_CONTROLLER_H

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * BUTTON CONTROLLER STRUCT
 * ========================================================================== */

typedef struct {
    uint32_t press_start_time;      // When button was pressed
    uint32_t last_action_time;      // Last action timestamp
    bool is_pressed;                // Current button state
    ButtonAction_t current_action;  // Last detected action
} ButtonController_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ========================================================================== */

/**
 * @brief Initialize button controller
 * @param btn Pointer to ButtonController_t structure
 * @return true if GPIO initialization succeeded
 */
bool button_init(ButtonController_t *btn);

/**
 * @brief Update button state (call periodically from task)
 * @param btn Pointer to ButtonController_t structure
 * @param current_time Current time in milliseconds
 */
void button_update(ButtonController_t *btn, uint32_t current_time);

/**
 * @brief Get last detected action
 * @param btn Pointer to ButtonController_t structure
 * @return ButtonAction_t (NONE, SHORT, or LONG)
 */
ButtonAction_t button_get_action(ButtonController_t *btn);

/**
 * @brief Clear the current action
 * @param btn Pointer to ButtonController_t structure
 */
void button_clear_action(ButtonController_t *btn);

/**
 * @brief Get current button state
 * @param btn Pointer to ButtonController_t structure
 * @return true if button is pressed, false otherwise
 */
bool button_is_pressed(ButtonController_t *btn);

#endif /* BUTTON_CONTROLLER_H */

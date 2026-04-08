/**
 * @file mode_controller.c
 * @brief Mode Controller implementation
 */

#include "mode_controller.h"

#include <string.h>

#define MODE_IDLE_TIMEOUT_MS 5000

static void mark_mode_changed(ModeController_t *mode, uint32_t current_time) {
    mode->last_mode_change_time = current_time;
    mode->changed_since_last_check = true;
}

void mode_init(ModeController_t *mode) {
    memset(mode, 0, sizeof(*mode));
    mode->current_mode = MODE_AUTO;
}

void mode_handle_button(ModeController_t *mode, ButtonAction_t button_action, uint32_t current_time) {
    if (button_action == BUTTON_ACTION_SHORT) {
        mode_cycle_next(mode, current_time);
        mode->idle_start_time = current_time;
        mode->idle_timer_active = true;
    } else if (button_action == BUTTON_ACTION_LONG) {
        mode_return_to_auto(mode, current_time);
        mode->idle_timer_active = false;
    }
}

void mode_update_idle(ModeController_t *mode, uint32_t current_time) {
    if (!mode->idle_timer_active) {
        return;
    }

    if ((current_time - mode->idle_start_time) >= MODE_IDLE_TIMEOUT_MS) {
        mode_return_to_auto(mode, current_time);
        mode->idle_timer_active = false;
    }
}

void mode_note_activity(ModeController_t *mode, uint32_t current_time) {
    mode->idle_start_time = current_time;
    mode->idle_timer_active = true;
}

OperatingMode_t mode_get_current(ModeController_t *mode) {
    return mode->current_mode;
}

void mode_cycle_next(ModeController_t *mode, uint32_t current_time) {
    mode->current_mode = (mode->current_mode == MODE_AUTO) ? MODE_MANUAL : MODE_AUTO;
    mark_mode_changed(mode, current_time);
}

void mode_return_to_auto(ModeController_t *mode, uint32_t current_time) {
    if (mode->current_mode == MODE_AUTO) {
        return;
    }

    mode->current_mode = MODE_AUTO;
    mark_mode_changed(mode, current_time);
}

bool mode_changed(ModeController_t *mode) {
    bool changed = mode->changed_since_last_check;
    mode->changed_since_last_check = false;
    return changed;
}

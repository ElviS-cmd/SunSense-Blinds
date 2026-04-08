/**
 * @file mode_controller.h
 * @brief Mode Controller for SunSense V2
 * @author Elvis
 * @date 2026
 *
 * Finite State Machine for AUTO/MANUAL mode switching
 * Single button cycles through modes with idle timeout
 */

#ifndef MODE_CONTROLLER_H
#define MODE_CONTROLLER_H

#include "system_types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    OperatingMode_t current_mode;
    uint32_t last_mode_change_time;
    uint32_t idle_start_time;
    bool idle_timer_active;
    bool changed_since_last_check;
} ModeController_t;

void mode_init(ModeController_t *mode);
void mode_handle_button(ModeController_t *mode, ButtonAction_t button_action, uint32_t current_time);
void mode_update_idle(ModeController_t *mode, uint32_t current_time);
void mode_note_activity(ModeController_t *mode, uint32_t current_time);
OperatingMode_t mode_get_current(ModeController_t *mode);
void mode_cycle_next(ModeController_t *mode, uint32_t current_time);
void mode_return_to_auto(ModeController_t *mode, uint32_t current_time);
bool mode_changed(ModeController_t *mode);

#endif /* MODE_CONTROLLER_H */

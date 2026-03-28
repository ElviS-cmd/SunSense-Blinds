#ifndef MODE_CONTROLLER_H
#define MODE_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"
#include <stdint.h>

/* ============ MODE CONTROLLER STATE ============ */
typedef struct {
    OperatingMode current_mode;
    uint8_t menu_selected_index;  // Which menu item is selected (0-2)
} ModeController;

/* ============ PUBLIC FUNCTIONS ============ */

/**
 * Initialize mode controller to MENU mode
 */
void mode_init(ModeController *m);

/**
 * Set current mode directly
 */
void mode_set(ModeController *m, OperatingMode mode);

/**
 * Get current mode
 */
OperatingMode mode_get(ModeController *m);

/**
 * Handle UP button press (move menu pointer up)
 * Only works in MENU mode
 */
void mode_handle_up(ModeController *m);

/**
 * Handle DOWN button press (move menu pointer down)
 * Only works in MENU mode
 */
void mode_handle_down(ModeController *m);

/**
 * Handle ENTER button press (select menu item or return to menu)
 * In MENU mode: selects the highlighted mode
 * In other modes: returns to MENU
 */
void mode_handle_enter(ModeController *m);

/**
 * Get currently selected menu index (for display)
 */
uint8_t mode_get_selected_index(ModeController *m);

#ifdef __cplusplus
}
#endif

#endif // MODE_CONTROLLER_H
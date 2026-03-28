#include "mode_controller.h"

/* ============ MENU ITEMS ============ */
#define NUM_MENU_ITEMS 2  // AUTO and MANUAL (SETTINGS is placeholder)

/* ============ INITIALIZE ============ */
void mode_init(ModeController *m) {
    m->current_mode = MODE_MENU;
    m->menu_selected_index = 0;  // Start on AUTO MODE
}

/* ============ SET MODE ============ */
void mode_set(ModeController *m, OperatingMode mode) {
    m->current_mode = mode;
}

/* ============ GET MODE ============ */
OperatingMode mode_get(ModeController *m) {
    return m->current_mode;
}

/* ============ HANDLE UP BUTTON ============ */
void mode_handle_up(ModeController *m) {
    // Only navigate menu if in MENU mode
    if (m->current_mode != MODE_MENU) {
        return;
    }
    
    // Move pointer up (wrap around to bottom)
    if (m->menu_selected_index == 0) {
        m->menu_selected_index = NUM_MENU_ITEMS - 1;
    } else {
        m->menu_selected_index--;
    }
}

/* ============ HANDLE DOWN BUTTON ============ */
void mode_handle_down(ModeController *m) {
    // Only navigate menu if in MENU mode
    if (m->current_mode != MODE_MENU) {
        return;
    }
    
    // Move pointer down (wrap around to top)
    if (m->menu_selected_index >= NUM_MENU_ITEMS - 1) {
        m->menu_selected_index = 0;
    } else {
        m->menu_selected_index++;
    }
}

/* ============ HANDLE ENTER BUTTON ============ */
void mode_handle_enter(ModeController *m) {
    if (m->current_mode == MODE_MENU) {
        // Select the highlighted menu item
        if (m->menu_selected_index == 0) {
            m->current_mode = MODE_AUTO;
        } else if (m->menu_selected_index == 1) {
            m->current_mode = MODE_MANUAL;
        }
    } else {
        // In AUTO or MANUAL mode, return to menu
        m->current_mode = MODE_MENU;
        m->menu_selected_index = 0;
    }
}

/* ============ GET SELECTED INDEX ============ */
uint8_t mode_get_selected_index(ModeController *m) {
    return m->menu_selected_index;
}
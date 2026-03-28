#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"
#include <stdint.h>

/* ============ OLED DISPLAY INITIALIZATION ============ */
void oled_init(void);

/* ============ DISPLAY FUNCTIONS ============ */
void oled_clear(void);
void oled_display_menu(uint8_t selected_index);
void oled_display_operating(OperatingMode mode, MotorState motor_state, int ldr_value);
void oled_display_text(uint8_t x, uint8_t y, const char *text);

#ifdef __cplusplus
}
#endif

#endif // OLED_DISPLAY_H
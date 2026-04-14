/**
 * @file led_controller.h
 * @brief LED Controller for SunSense V2
 * @author Elvis
 * @date 2026
 * 
 * Controls 2 status LEDs (Green GPIO5, Blue GPIO6)
 * - Green: System status
 * - Blue: Mode indicator
 */

#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * LED TYPES
 * ========================================================================== */

typedef enum {
    LED_GREEN = 0,  // GPIO5 - system status
    LED_BLUE = 1    // GPIO6 - mode indicator
} LEDColor_t;

typedef enum {
    LED_STATUS_NORMAL = 0,       // Both solid on: connected and ready
    LED_STATUS_OFFLINE,          // Both off: not connected/offline
    LED_STATUS_OPENING,          // Green slow blink: opening blinds
    LED_STATUS_CLOSING,          // Blue slow blink: closing blinds
    LED_STATUS_PAIRING,          // Both slow blink together: provisioning/setup
    LED_STATUS_CALIBRATING,      // Both fast blink together: calibration
    LED_STATUS_RECONNECTING,     // Slow alternating blink: reconnecting/searching
    LED_STATUS_FAULT             // Fast alternating blink: jam/obstruction/fault
} LEDStatusPattern_t;

/* ============================================================================
 * LED CONTROLLER STRUCT
 * ========================================================================== */

typedef struct {
    LEDState_t green_state;         // Green LED state
    LEDState_t blue_state;          // Blue LED state
    LEDStatusPattern_t status_pattern;
    uint32_t green_last_toggle_time;
    uint32_t blue_last_toggle_time;
    uint32_t pattern_last_toggle_time;
    bool green_is_on;               // Current green LED output state
    bool blue_is_on;                // Current blue LED output state
    bool pattern_phase_on;
} LEDController_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ========================================================================== */

/**
 * @brief Initialize LED controller and GPIO pins
 * @param led Pointer to LEDController_t structure
 * @return true if both GPIOs were configured successfully
 */
bool led_init(LEDController_t *led);

/**
 * @brief Set green LED state
 * @param led Pointer to LEDController_t structure
 * @param state LED state (OFF, ON, BLINK_SLOW, BLINK_FAST, PULSE)
 */
void led_set_green(LEDController_t *led, LEDState_t state);

/**
 * @brief Set blue LED state
 * @param led Pointer to LEDController_t structure
 * @param state LED state (OFF, ON, BLINK_SLOW, BLINK_FAST, PULSE)
 */
void led_set_blue(LEDController_t *led, LEDState_t state);

/**
 * @brief Set the product-level two-LED status pattern
 * @param led Pointer to LEDController_t structure
 * @param pattern Status pattern to display
 * @param current_time Current time in milliseconds
 */
void led_set_status_pattern(LEDController_t *led, LEDStatusPattern_t pattern, uint32_t current_time);

/**
 * @brief Update LED blink/pulse patterns (call periodically)
 * @param led Pointer to LEDController_t structure
 * @param current_time Current time in milliseconds
 */
void led_update(LEDController_t *led, uint32_t current_time);

/**
 * @brief Turn on all LEDs
 * @param led Pointer to LEDController_t structure
 */
void led_all_on(LEDController_t *led);

/**
 * @brief Turn off all LEDs
 * @param led Pointer to LEDController_t structure
 */
void led_all_off(LEDController_t *led);

/**
 * @brief Get green LED current output state
 * @param led Pointer to LEDController_t structure
 * @return true if LED is currently on, false otherwise
 */
bool led_get_green(LEDController_t *led);

/**
 * @brief Get blue LED current output state
 * @param led Pointer to LEDController_t structure
 * @return true if LED is currently on, false otherwise
 */
bool led_get_blue(LEDController_t *led);

#endif /* LED_CONTROLLER_H */

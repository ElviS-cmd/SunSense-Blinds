/**
 * @file ldr_controller.h
 * @brief LDR Controller for SunSense V2
 * @author Elvis
 * @date 2026
 * 
 * Light-Dependent Resistor sensor on GPIO1 (ADC)
 * Analog light level detection with hysteresis filtering
 */

#ifndef LDR_CONTROLLER_H
#define LDR_CONTROLLER_H

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * LDR CONTROLLER STRUCT
 * ========================================================================== */

typedef struct {
    uint16_t raw_adc_value;         // Raw ADC reading (0-4095)
    uint16_t filtered_value;        // Filtered with moving average
    LightLevel_t current_level;     // Current light level state
    LightLevel_t previous_level;    // Previous light level state
    uint32_t last_transition_time;  // When light level last changed
    uint16_t sample_count;          // For moving average filter
} LDRController_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ========================================================================== */

/**
 * @brief Initialize LDR controller and ADC
 * @param ldr Pointer to LDRController_t structure
 * @return true if ADC initialization succeeded
 */
bool ldr_init(LDRController_t *ldr);

/**
 * @brief Read and update LDR sensor value
 * @param ldr Pointer to LDRController_t structure
 * @param current_time Current time in milliseconds
 */
void ldr_update(LDRController_t *ldr, uint32_t current_time);

/**
 * @brief Get current light level state
 * @param ldr Pointer to LDRController_t structure
 * @return LightLevel_t (DARK or BRIGHT)
 */
LightLevel_t ldr_get_level(LDRController_t *ldr);

/**
 * @brief Get raw ADC value
 * @param ldr Pointer to LDRController_t structure
 * @return Raw ADC value (0-4095)
 */
uint16_t ldr_get_raw(LDRController_t *ldr);

/**
 * @brief Get filtered ADC value
 * @param ldr Pointer to LDRController_t structure
 * @return Filtered ADC value (0-4095)
 */
uint16_t ldr_get_filtered(LDRController_t *ldr);

/**
 * @brief Check if light level changed this update
 * @param ldr Pointer to LDRController_t structure
 * @return true if light level transitioned, false otherwise
 */
bool ldr_level_changed(LDRController_t *ldr);

/**
 * @brief Check if light is bright
 * @param ldr Pointer to LDRController_t structure
 * @return true if bright, false if dark
 */
bool ldr_is_bright(LDRController_t *ldr);

/**
 * @brief Check if light is dark
 * @param ldr Pointer to LDRController_t structure
 * @return true if dark, false if bright
 */
bool ldr_is_dark(LDRController_t *ldr);

#endif /* LDR_CONTROLLER_H */

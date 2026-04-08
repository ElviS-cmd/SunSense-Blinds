/**
 * @file encoder_controller.h
 * @brief Encoder Controller for SunSense V2
 * @author Elvis
 * @date 2026
 * 
 * AS5600 magnetic rotary encoder on I2C bus
 * Tracks blind position angle (0-360°)
 */

#ifndef ENCODER_CONTROLLER_H
#define ENCODER_CONTROLLER_H

#include "system_types.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * ENCODER CONTROLLER STRUCT
 * ========================================================================== */

typedef struct {
    uint16_t raw_angle;             // Raw 12-bit angle (0-4095 = 0-360°)
    float angle_degrees;            // Converted to degrees (0.0-360.0)
    float angle_percent;            // Normalized to 0-100%
    EncoderStatus_t status;         // Current encoder status
    uint32_t last_read_time;        // Last successful read
    uint8_t error_count;            // Consecutive I2C errors
} EncoderController_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ========================================================================== */

/**
 * @brief Initialize encoder controller and I2C bus
 * @param encoder Pointer to EncoderController_t structure
 * @return true if initialization successful, false otherwise
 */
bool encoder_init(EncoderController_t *encoder);

/**
 * @brief Read encoder position via I2C
 * @param encoder Pointer to EncoderController_t structure
 * @param current_time Current time in milliseconds
 * @return true if read successful, false otherwise
 */
bool encoder_update(EncoderController_t *encoder, uint32_t current_time);

/**
 * @brief Get raw 12-bit angle value
 * @param encoder Pointer to EncoderController_t structure
 * @return Raw angle (0-4095)
 */
uint16_t encoder_get_raw(EncoderController_t *encoder);

/**
 * @brief Get angle in degrees
 * @param encoder Pointer to EncoderController_t structure
 * @return Angle in degrees (0.0-360.0)
 */
float encoder_get_degrees(EncoderController_t *encoder);

/**
 * @brief Get angle as percentage
 * @param encoder Pointer to EncoderController_t structure
 * @return Percentage (0.0-100.0)
 */
float encoder_get_percent(EncoderController_t *encoder);

/**
 * @brief Get encoder status
 * @param encoder Pointer to EncoderController_t structure
 * @return EncoderStatus_t (OK, MAGNET_WEAK, MAGNET_STRONG, or ERROR)
 */
EncoderStatus_t encoder_get_status(EncoderController_t *encoder);

/**
 * @brief Check if encoder is healthy
 * @param encoder Pointer to EncoderController_t structure
 * @return true if status is OK, false otherwise
 */
bool encoder_is_healthy(EncoderController_t *encoder);

#endif /* ENCODER_CONTROLLER_H */

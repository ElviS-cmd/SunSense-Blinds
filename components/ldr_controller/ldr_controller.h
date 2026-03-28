#ifndef LDR_CONTROLLER_H
#define LDR_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

/**
 * @brief LDR Controller - Light Dependent Resistor sensor interface
 * 
 * Features:
 * - Dual ADC input (GPIO34, GPIO35)
 * - Moving average filtering (10 readings)
 * - Normalized output (0-100%)
 * - Hysteresis logic for stable thresholds
 * - FreeRTOS task-based sampling (1000ms interval)
 */

/* ==================== Configuration ==================== */

#define LDR_ADC_CHANNEL_1     ADC_CHANNEL_6    /* GPIO34 */
#define LDR_ADC_CHANNEL_2     ADC_CHANNEL_7    /* GPIO35 */
#define LDR_FILTER_SIZE       10                /* Moving average window */
#define LDR_UPDATE_INTERVAL_MS 1000             /* Task update rate */
#define LDR_HYSTERESIS_THRESHOLD 5              /* % change to trigger event */

/* ==================== Data Types ==================== */

/**
 * @brief Hysteresis state tracking
 */
typedef struct {
    uint8_t last_normalized;      /* Last reported normalized value */
    bool is_bright;               /* Current state (bright/dark) */
} ldr_hysteresis_t;

/**
 * @brief LDR controller context
 */
typedef struct {
    uint16_t raw_adc_1;           /* Raw ADC reading from GPIO34 */
    uint16_t raw_adc_2;           /* Raw ADC reading from GPIO35 */
    uint8_t normalized_1;         /* Normalized (0-100%) GPIO34 */
    uint8_t normalized_2;         /* Normalized (0-100%) GPIO35 */
    uint16_t filter_buffer_1[LDR_FILTER_SIZE];  /* Moving average buffer */
    uint16_t filter_buffer_2[LDR_FILTER_SIZE];  /* Moving average buffer */
    uint8_t filter_index;         /* Current position in filter buffer */
    ldr_hysteresis_t hysteresis;  /* Hysteresis state */
    bool initialized;             /* Initialization flag */
} ldr_controller_t;

/* ==================== Public Functions ==================== */

/**
 * @brief Initialize LDR controller and ADC
 * 
 * - Configures ADC1 for GPIO34 and GPIO35
 * - Initializes filter buffers
 * - Creates FreeRTOS task for periodic sampling
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ldr_controller_init(void);

/**
 * @brief Get normalized light level from GPIO34
 * 
 * @return Light level (0-100%), where 0=dark, 100=bright
 */
uint8_t ldr_get_light_level_1(void);

/**
 * @brief Get normalized light level from GPIO35
 * 
 * @return Light level (0-100%), where 0=dark, 100=bright
 */
uint8_t ldr_get_light_level_2(void);

/**
 * @brief Get raw ADC reading from GPIO34
 * 
 * @return Raw ADC value (0-4095)
 */
uint16_t ldr_get_raw_adc_1(void);

/**
 * @brief Get raw ADC reading from GPIO35
 * 
 * @return Raw ADC value (0-4095)
 */
uint16_t ldr_get_raw_adc_2(void);

/**
 * @brief Check if light level crossed hysteresis threshold (bright)
 * 
 * Returns true once when transitioning to bright state,
 * requires drop below (threshold - hysteresis) to reset.
 * 
 * @return true if bright threshold crossed, false otherwise
 */
bool ldr_is_bright(void);

/**
 * @brief Check if light level crossed hysteresis threshold (dark)
 * 
 * Returns true once when transitioning to dark state,
 * requires rise above (threshold + hysteresis) to reset.
 * 
 * @return true if dark threshold crossed, false otherwise
 */
bool ldr_is_dark(void);

/**
 * @brief Get detailed LDR controller state
 * 
 * @param controller Pointer to controller context
 * @return ESP_OK on success
 */
esp_err_t ldr_get_state(ldr_controller_t *controller);

/**
 * @brief Deinitialize LDR controller and stop FreeRTOS task
 * 
 * @return ESP_OK on success
 */
esp_err_t ldr_controller_deinit(void);

#endif /* LDR_CONTROLLER_H */
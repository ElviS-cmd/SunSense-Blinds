/**
 * @file ldr_controller.c
 * @brief LDR Controller Implementation
 * @author Elvis
 * @date 2026
 */

#include "ldr_controller.h"
#include "gpio_config.h"
#include "esp_adc/adc_oneshot.h"
#include <string.h>

/* ============================================================================
 * MOVING AVERAGE FILTER
 * ========================================================================== */

#define MOVING_AVERAGE_SIZE     10      // 10-sample moving average

typedef struct {
    uint16_t samples[MOVING_AVERAGE_SIZE];
    uint8_t index;
    uint8_t count;
} MovingAverage_t;

static MovingAverage_t ma_filter = {0};
static adc_oneshot_unit_handle_t s_adc_handle = NULL;

static uint16_t moving_average_update(uint16_t new_sample) {
    ma_filter.samples[ma_filter.index] = new_sample;
    ma_filter.index = (ma_filter.index + 1) % MOVING_AVERAGE_SIZE;
    
    if (ma_filter.count < MOVING_AVERAGE_SIZE) {
        ma_filter.count++;
    }
    
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ma_filter.count; i++) {
        sum += ma_filter.samples[i];
    }
    
    return (uint16_t)(sum / ma_filter.count);
}

/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

bool ldr_init(LDRController_t *ldr) {
    memset(ldr, 0, sizeof(LDRController_t));
    ldr->current_level = LIGHT_DARK;
    ldr->previous_level = LIGHT_DARK;
    memset(&ma_filter, 0, sizeof(ma_filter));
    
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_PORT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&init_config, &s_adc_handle) != ESP_OK) {
        s_adc_handle = NULL;
        return false;
    }
    
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_RESOLUTION,
        .atten = ADC_ATTEN,
    };
    if (adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL, &config) != ESP_OK) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return false;
    }

    return true;
}

/* ============================================================================
 * SENSOR UPDATE
 * ========================================================================== */

void ldr_update(LDRController_t *ldr, uint32_t current_time) {
    /* Read ADC */
    int adc_reading = 0;
    if (s_adc_handle == NULL) {
        return;
    }

    adc_oneshot_read(s_adc_handle, ADC_CHANNEL, &adc_reading);
    
    ldr->raw_adc_value = (uint16_t)adc_reading;
    
    /* Apply moving average filter */
    ldr->filtered_value = moving_average_update(ldr->raw_adc_value);
    
    /* Store previous level for change detection */
    ldr->previous_level = ldr->current_level;
    
    /* Lower ADC values mean bright light, higher values mean darkness. */
    if (ldr->filtered_value <= LDR_BRIGHT_THRESHOLD) {
        ldr->current_level = LIGHT_BRIGHT;
    } else if (ldr->filtered_value >= LDR_DARK_THRESHOLD) {
        ldr->current_level = LIGHT_DARK;
    }
    /* Else: stay in current state (hysteresis band) */
    
    /* Track transition time */
    if (ldr->current_level != ldr->previous_level) {
        ldr->last_transition_time = current_time;
    }
}

/* ============================================================================
 * STATE QUERIES
 * ========================================================================== */

LightLevel_t ldr_get_level(LDRController_t *ldr) {
    return ldr->current_level;
}

uint16_t ldr_get_raw(LDRController_t *ldr) {
    return ldr->raw_adc_value;
}

uint16_t ldr_get_filtered(LDRController_t *ldr) {
    return ldr->filtered_value;
}

bool ldr_level_changed(LDRController_t *ldr) {
    return (ldr->current_level != ldr->previous_level);
}

bool ldr_is_bright(LDRController_t *ldr) {
    return (ldr->current_level == LIGHT_BRIGHT);
}

bool ldr_is_dark(LDRController_t *ldr) {
    return (ldr->current_level == LIGHT_DARK);
}

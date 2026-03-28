#include "ldr_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_log.h"
#include <string.h>

/* ==================== Defines & Statics ==================== */

static const char *TAG = "LDR_CONTROLLER";

static ldr_controller_t g_ldr_ctx = {0};
static TaskHandle_t g_ldr_task_handle = NULL;
static adc_oneshot_unit_handle_t g_adc_handle = NULL;
static adc_cali_handle_t g_adc_cali_handle = NULL;

/* ==================== Private Function Prototypes ==================== */

static void ldr_task(void *pvParameters);
static uint16_t ldr_filter_and_average(uint16_t new_reading, uint16_t *filter_buffer);
static uint8_t ldr_normalize_adc(uint16_t raw_adc);
static void ldr_update_hysteresis(uint8_t normalized_value);

/* ==================== Implementation ==================== */

/**
 * @brief Initialize ADC and create FreeRTOS task
 */
esp_err_t ldr_controller_init(void)
{
    if (g_ldr_ctx.initialized) {
        ESP_LOGW(TAG, "LDR controller already initialized");
        return ESP_OK;
    }

    /* Initialize ADC1 */
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    ESP_RETURN_ON_ERROR(
        adc_oneshot_new_unit(&init_config1, &g_adc_handle),
        TAG,
        "Failed to initialize ADC unit"
    );

    /* Configure ADC1 Channel 6 (GPIO34) */
    adc_oneshot_chan_cfg_t config1 = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,  /* Full range 0-3.3V */
    };

    ESP_RETURN_ON_ERROR(
        adc_oneshot_config_channel(g_adc_handle, LDR_ADC_CHANNEL_1, &config1),
        TAG,
        "Failed to configure ADC channel 1 (GPIO34)"
    );

    /* Configure ADC1 Channel 7 (GPIO35) */
    adc_oneshot_chan_cfg_t config2 = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,  /* Full range 0-3.3V */
    };

    ESP_RETURN_ON_ERROR(
        adc_oneshot_config_channel(g_adc_handle, LDR_ADC_CHANNEL_2, &config2),
        TAG,
        "Failed to configure ADC channel 2 (GPIO35)"
    );

    /* Initialize ADC calibration */
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ESP_RETURN_ON_ERROR(
        adc_cali_create_scheme_line_fitting(&cali_config, &g_adc_cali_handle),
        TAG,
        "Failed to create ADC calibration scheme"
    );

    /* Initialize filter buffers and hysteresis */
    memset(g_ldr_ctx.filter_buffer_1, 0, sizeof(g_ldr_ctx.filter_buffer_1));
    memset(g_ldr_ctx.filter_buffer_2, 0, sizeof(g_ldr_ctx.filter_buffer_2));
    g_ldr_ctx.filter_index = 0;
    g_ldr_ctx.hysteresis.last_normalized = 50;
    g_ldr_ctx.hysteresis.is_bright = false;
    g_ldr_ctx.normalized_1 = 50;
    g_ldr_ctx.normalized_2 = 50;

    /* Create FreeRTOS task */
    BaseType_t task_result = xTaskCreate(
        ldr_task,
        "ldr_task",
        2048,           /* Stack size */
        NULL,
        5,              /* Priority */
        &g_ldr_task_handle
    );

    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LDR task");
        return ESP_FAIL;
    }

    g_ldr_ctx.initialized = true;
    ESP_LOGI(TAG, "LDR controller initialized successfully");

    return ESP_OK;
}

/**
 * @brief FreeRTOS task for periodic ADC sampling
 */
static void ldr_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        /* Read ADC Channel 1 (GPIO34) */
        int adc_raw_1 = 0;
        adc_oneshot_read(g_adc_handle, LDR_ADC_CHANNEL_1, &adc_raw_1);
        g_ldr_ctx.raw_adc_1 = (uint16_t)adc_raw_1;

        /* Read ADC Channel 2 (GPIO35) */
        int adc_raw_2 = 0;
        adc_oneshot_read(g_adc_handle, LDR_ADC_CHANNEL_2, &adc_raw_2);
        g_ldr_ctx.raw_adc_2 = (uint16_t)adc_raw_2;

        /* Apply moving average filter and normalize */
        uint16_t filtered_1 = ldr_filter_and_average(g_ldr_ctx.raw_adc_1, g_ldr_ctx.filter_buffer_1);
        uint16_t filtered_2 = ldr_filter_and_average(g_ldr_ctx.raw_adc_2, g_ldr_ctx.filter_buffer_2);

        g_ldr_ctx.normalized_1 = ldr_normalize_adc(filtered_1);
        g_ldr_ctx.normalized_2 = ldr_normalize_adc(filtered_2);

        /* Update hysteresis with averaged value */
        ldr_update_hysteresis((g_ldr_ctx.normalized_1 + g_ldr_ctx.normalized_2) / 2);

        ESP_LOGD(TAG, "LDR1: raw=%u, norm=%u%% | LDR2: raw=%u, norm=%u%% | Hysteresis: %s",
                 g_ldr_ctx.raw_adc_1, g_ldr_ctx.normalized_1,
                 g_ldr_ctx.raw_adc_2, g_ldr_ctx.normalized_2,
                 g_ldr_ctx.hysteresis.is_bright ? "BRIGHT" : "DARK");

        /* Sleep for update interval */
        vTaskDelay(pdMS_TO_TICKS(LDR_UPDATE_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}

/**
 * @brief Apply moving average filter to new reading
 */
static uint16_t ldr_filter_and_average(uint16_t new_reading, uint16_t *filter_buffer)
{
    /* Add new reading to buffer */
    filter_buffer[g_ldr_ctx.filter_index] = new_reading;

    /* Calculate average across all buffer values */
    uint32_t sum = 0;
    for (int i = 0; i < LDR_FILTER_SIZE; i++) {
        sum += filter_buffer[i];
    }
    uint16_t average = sum / LDR_FILTER_SIZE;

    /* Move to next buffer position (circular) */
    g_ldr_ctx.filter_index = (g_ldr_ctx.filter_index + 1) % LDR_FILTER_SIZE;

    return average;
}

/**
 * @brief Normalize raw ADC (0-4095) to 0-100%
 */
static uint8_t ldr_normalize_adc(uint16_t raw_adc)
{
    /* Clamp to ADC range and convert to percentage */
    if (raw_adc > 4095) {
        raw_adc = 4095;
    }
    return (uint8_t)((raw_adc * 100) / 4095);
}

/**
 * @brief Update hysteresis state based on normalized light level
 * 
 * Hysteresis prevents rapid oscillation around threshold.
 * Requires 5% change to transition between bright/dark states.
 */
static void ldr_update_hysteresis(uint8_t normalized_value)
{
    int16_t delta = (int16_t)normalized_value - (int16_t)g_ldr_ctx.hysteresis.last_normalized;

    if (g_ldr_ctx.hysteresis.is_bright) {
        /* Currently in bright state: check if it dropped enough to go dark */
        if (delta < -(int16_t)LDR_HYSTERESIS_THRESHOLD) {
            g_ldr_ctx.hysteresis.is_bright = false;
            ESP_LOGI(TAG, "Hysteresis transition: BRIGHT -> DARK (delta=%d)", delta);
        }
    } else {
        /* Currently in dark state: check if it rose enough to go bright */
        if (delta > (int16_t)LDR_HYSTERESIS_THRESHOLD) {
            g_ldr_ctx.hysteresis.is_bright = true;
            ESP_LOGI(TAG, "Hysteresis transition: DARK -> BRIGHT (delta=%d)", delta);
        }
    }

    g_ldr_ctx.hysteresis.last_normalized = normalized_value;
}

/**
 * @brief Get normalized light level from GPIO34
 */
uint8_t ldr_get_light_level_1(void)
{
    return g_ldr_ctx.normalized_1;
}

/**
 * @brief Get normalized light level from GPIO35
 */
uint8_t ldr_get_light_level_2(void)
{
    return g_ldr_ctx.normalized_2;
}

/**
 * @brief Get raw ADC reading from GPIO34
 */
uint16_t ldr_get_raw_adc_1(void)
{
    return g_ldr_ctx.raw_adc_1;
}

/**
 * @brief Get raw ADC reading from GPIO35
 */
uint16_t ldr_get_raw_adc_2(void)
{
    return g_ldr_ctx.raw_adc_2;
}

/**
 * @brief Check if light level is bright (hysteresis-aware)
 */
bool ldr_is_bright(void)
{
    return g_ldr_ctx.hysteresis.is_bright;
}

/**
 * @brief Check if light level is dark (hysteresis-aware)
 */
bool ldr_is_dark(void)
{
    return !g_ldr_ctx.hysteresis.is_bright;
}

/**
 * @brief Get complete LDR controller state
 */
esp_err_t ldr_get_state(ldr_controller_t *controller)
{
    if (!controller) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(controller, &g_ldr_ctx, sizeof(ldr_controller_t));
    return ESP_OK;
}

/**
 * @brief Deinitialize LDR controller
 */
esp_err_t ldr_controller_deinit(void)
{
    if (!g_ldr_ctx.initialized) {
        return ESP_OK;
    }

    /* Delete FreeRTOS task */
    if (g_ldr_task_handle) {
        vTaskDelete(g_ldr_task_handle);
        g_ldr_task_handle = NULL;
    }

    /* Clean up ADC */
    if (g_adc_cali_handle) {
        adc_cali_delete_scheme_line_fitting(g_adc_cali_handle);
        g_adc_cali_handle = NULL;
    }

    if (g_adc_handle) {
        adc_oneshot_del_unit(g_adc_handle);
        g_adc_handle = NULL;
    }

    g_ldr_ctx.initialized = false;
    ESP_LOGI(TAG, "LDR controller deinitialized");

    return ESP_OK;
}
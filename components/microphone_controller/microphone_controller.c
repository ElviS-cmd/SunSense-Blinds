/**
 * @file microphone_controller.c
 * @brief Microphone Controller Implementation
 * @author Elvis
 * @date 2026
 */

#include "microphone_controller.h"
#include "gpio_config.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 * I2S HANDLE (global)
 * ========================================================================== */

static i2s_chan_handle_t rx_handle = NULL;

/* ============================================================================
 * AUDIO PROCESSING HELPERS
 * ========================================================================== */

static uint16_t calculate_rms(int16_t *samples, uint32_t count) {
    if (count == 0) return 0;
    
    uint64_t sum_squares = 0;
    for (uint32_t i = 0; i < count; i++) {
        int32_t sample = (int32_t)samples[i];
        sum_squares += sample * sample;
    }
    
    float mean_square = (float)sum_squares / (float)count;
    uint16_t rms = (uint16_t)sqrt(mean_square);
    
    return rms;
}

/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

bool microphone_init(MicrophoneController_t *mic) {
    memset(mic, 0, sizeof(MicrophoneController_t));
    
    /* Configure I2S channel as RX (receive) */
    i2s_chan_config_t chan_cfg = {
        .id = I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = I2S_DMA_BUF_COUNT,
        .dma_frame_num = I2S_DMA_BUF_LEN,
        .auto_clear = false,
        .intr_priority = 0,
    };
    
    if (i2s_new_channel(&chan_cfg, NULL, &rx_handle) != ESP_OK) {
        return false;
    }
    
    /* Configure I2S standard mode */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_I2S_CLK,
            .ws = GPIO_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = GPIO_I2S_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    if (i2s_channel_init_std_mode(rx_handle, &std_cfg) != ESP_OK) {
        return false;
    }
    
    /* Enable I2S RX channel */
    if (i2s_channel_enable(rx_handle) != ESP_OK) {
        return false;
    }
    
    return true;
}

/* ============================================================================
 * AUDIO CAPTURE
 * ========================================================================== */

bool microphone_update(MicrophoneController_t *mic, uint32_t current_time) {
    if (rx_handle == NULL) {
        return false;
    }
    
    /* Read samples from I2S */
    size_t bytes_read = 0;
    int16_t temp_buffer[64] = {0};
    
    esp_err_t ret = i2s_channel_read(
        rx_handle,
        temp_buffer,
        sizeof(temp_buffer),
        &bytes_read,
        pdMS_TO_TICKS(5)
    );
    
    if (ret != ESP_OK || bytes_read == 0) {
        return false;
    }
    
    /* Add samples to circular buffer */
    uint32_t samples_read = bytes_read / sizeof(int16_t);
    for (uint32_t i = 0; i < samples_read; i++) {
        mic->audio_buffer[mic->buffer_index] = temp_buffer[i];
        mic->buffer_index = (mic->buffer_index + 1) % MIC_BUFFER_SIZE;
        mic->sample_count++;
        
        /* Check if buffer is full */
        if (mic->buffer_index == 0) {
            mic->buffer_ready = true;
        }
    }
    
    mic->last_sample_time = current_time;
    return true;
}

/* ============================================================================
 * BUFFER MANAGEMENT
 * ========================================================================== */

int16_t* microphone_get_buffer(MicrophoneController_t *mic) {
    return mic->audio_buffer;
}

bool microphone_is_buffer_ready(MicrophoneController_t *mic) {
    return mic->buffer_ready;
}

void microphone_clear_buffer(MicrophoneController_t *mic) {
    memset(mic->audio_buffer, 0, sizeof(mic->audio_buffer));
    mic->buffer_index = 0;
    mic->buffer_ready = false;
}

/* ============================================================================
 * AUDIO ANALYSIS
 * ========================================================================== */

uint16_t microphone_get_level(MicrophoneController_t *mic) {
    return calculate_rms(mic->audio_buffer, MIC_BUFFER_SIZE);
}

bool microphone_sound_detected(MicrophoneController_t *mic, uint16_t threshold) {
    uint16_t level = microphone_get_level(mic);
    return (level > threshold);
}

uint32_t microphone_get_sample_count(MicrophoneController_t *mic) {
    return mic->sample_count;
}

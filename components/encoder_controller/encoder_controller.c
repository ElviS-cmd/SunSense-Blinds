/**
 * @file encoder_controller.c
 * @brief Encoder Controller Implementation
 * @author Elvis
 * @date 2026
 */

#include "encoder_controller.h"
#include "gpio_config.h"
#include "driver/i2c_master.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 * I2C HANDLE (global)
 * ========================================================================== */

static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;

/* ============================================================================
 * AS5600 REGISTER READ
 * ========================================================================== */

static bool encoder_read_register(uint8_t reg, uint8_t *data, uint8_t len) {
    if (dev_handle == NULL) {
        return false;
    }
    
    esp_err_t ret = i2c_master_transmit_receive(
        dev_handle,
        &reg,
        1,
        data,
        len,
        I2C_TRANSACTION_TIMEOUT_MS);
    return (ret == ESP_OK);
}

/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

bool encoder_init(EncoderController_t *encoder) {
    memset(encoder, 0, sizeof(EncoderController_t));
    encoder->status = ENCODER_STATUS_OK;
    
    /* Initialize I2C master bus */
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_PORT,
        .scl_io_num = GPIO_I2C_SCL,
        .sda_io_num = GPIO_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    if (i2c_new_master_bus(&i2c_mst_config, &bus_handle) != ESP_OK) {
        return false;
    }
    
    /* Initialize device handle for AS5600 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = AS5600_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    
    if (i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle) != ESP_OK) {
        return false;
    }
    
    return true;
}

/* ============================================================================
 * SENSOR UPDATE
 * ========================================================================== */

bool encoder_update(EncoderController_t *encoder, uint32_t current_time) {
    uint8_t raw_data[2] = {0};
    
    /* Read raw angle register (12-bit value across 2 bytes) */
    if (!encoder_read_register(AS5600_REG_RAW_ANGLE, raw_data, 2)) {
        encoder->error_count++;
        if (encoder->error_count > 5) {
            encoder->status = ENCODER_STATUS_ERROR;
        }
        return false;
    }
    
    encoder->error_count = 0;
    
    /* Combine bytes into 12-bit value */
    uint16_t raw_12bit = ((raw_data[0] & 0x0F) << 8) | raw_data[1];
    encoder->raw_angle = raw_12bit;
    
    /* Convert to degrees (0-360°) */
    encoder->angle_degrees = (float)raw_12bit * 360.0f / 4095.0f;
    
    /* Convert to percentage (0-100%) */
    encoder->angle_percent = (float)raw_12bit * 100.0f / 4095.0f;
    
    /* Read status register for magnet health */
    uint8_t status_byte = 0;
    if (encoder_read_register(AS5600_REG_STATUS, &status_byte, 1)) {
        bool magnet_detected = ((status_byte & (1U << 5)) != 0U);
        bool magnet_too_weak = ((status_byte & (1U << 4)) != 0U);
        bool magnet_too_strong = ((status_byte & (1U << 3)) != 0U);

        if (magnet_too_weak) {
            encoder->status = ENCODER_STATUS_MAGNET_WEAK;
        } else if (magnet_too_strong) {
            encoder->status = ENCODER_STATUS_MAGNET_STRONG;
        } else if (magnet_detected) {
            encoder->status = ENCODER_STATUS_OK;
        } else {
            encoder->status = ENCODER_STATUS_ERROR;
        }
    }
    
    encoder->last_read_time = current_time;
    return true;
}

/* ============================================================================
 * STATE QUERIES
 * ========================================================================== */

uint16_t encoder_get_raw(EncoderController_t *encoder) {
    return encoder->raw_angle;
}

float encoder_get_degrees(EncoderController_t *encoder) {
    return encoder->angle_degrees;
}

float encoder_get_percent(EncoderController_t *encoder) {
    return encoder->angle_percent;
}

EncoderStatus_t encoder_get_status(EncoderController_t *encoder) {
    return encoder->status;
}

bool encoder_is_healthy(EncoderController_t *encoder) {
    return (encoder->status == ENCODER_STATUS_OK);
}

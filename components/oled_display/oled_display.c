#include "oled_display.h"
#include <stdio.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_ops.h"

/* ============ I2C & OLED CONFIG ============ */
#define OLED_SDA_PIN 21
#define OLED_SCL_PIN 22
#define I2C_HOST 0
#define I2C_FREQ_HZ 400000

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

/* ============ GLOBAL DISPLAY HANDLE ============ */
static esp_lcd_panel_handle_t panel_handle = NULL;

/* ============ INITIALIZATION ============ */
void oled_init(void) {
    // Step 1: Initialize I2C bus
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA_PIN,
        .scl_io_num = OLED_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    i2c_param_config(I2C_HOST, &i2c_conf);
    i2c_driver_install(I2C_HOST, I2C_MODE_MASTER, 0, 0, 0);
    
    // Step 2: Create I2C device handle
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = 0x3C,  // SSD1306 default I2C address
    };
    esp_lcd_new_panel_io_i2c(I2C_HOST, &io_config, &io_handle);
    
    // Step 3: Create SSD1306 panel handle
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,  // No reset pin
        .color_space = ESP_LCD_COLOR_SPACE_MONOCHROME,
        .bits_per_pixel = 1,
    };
    esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle);
    
    // Step 4: Initialize panel
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_disp_on_off(panel_handle, true);
}

/* ============ CLEAR DISPLAY ============ */
void oled_clear(void) {
    if (panel_handle == NULL) return;
    
    // Create a buffer filled with zeros (black)
    uint8_t buffer[OLED_WIDTH * OLED_HEIGHT / 8];
    memset(buffer, 0, sizeof(buffer));
    
    // Draw the buffer to display
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, OLED_WIDTH, OLED_HEIGHT, buffer);
}

/* ============ DISPLAY MENU SCREEN ============ */
void oled_display_menu(uint8_t selected_index) {
    oled_clear();
    
    // Title
    oled_display_text(0, 0, "SELECT MODE");
    
    // Menu items
    const char *menu_items[] = {
        "AUTO MODE",
        "MANUAL MODE",
        "SETTINGS"
    };
    
    uint8_t num_items = 3;
    
    for (uint8_t i = 0; i < num_items; i++) {
        uint8_t y = 16 + (i * 16);
        
        // Draw pointer if selected
        if (i == selected_index) {
            oled_display_text(0, y, ">");
        }
        
        // Draw menu item
        oled_display_text(16, y, menu_items[i]);
    }
    
    // Footer
    oled_display_text(0, 56, "UP/DOWN to nav");
}

/* ============ DISPLAY OPERATING SCREEN ============ */
void oled_display_operating(OperatingMode mode, MotorState motor_state, int ldr_value) {
    oled_clear();
    
    // Title
    if (mode == MODE_AUTO) {
        oled_display_text(0, 0, "=== AUTO ===");
    } else if (mode == MODE_MANUAL) {
        oled_display_text(0, 0, "=== MANUAL ===");
    }
    
    // Motor state
    char motor_str[32];
    switch (motor_state) {
        case MOTOR_STOPPED:
            sprintf(motor_str, "Motor: STOPPED");
            break;
        case MOTOR_OPENING:
            sprintf(motor_str, "Motor: OPENING");
            break;
        case MOTOR_CLOSING:
            sprintf(motor_str, "Motor: CLOSING");
            break;
        default:
            sprintf(motor_str, "Motor: ?");
    }
    oled_display_text(0, 16, motor_str);
    
    // LDR value
    char ldr_str[32];
    sprintf(ldr_str, "Light: %d", ldr_value);
    oled_display_text(0, 32, ldr_str);
    
    // Footer
    oled_display_text(0, 56, "Hold 2s for menu");
}

/* ============ DISPLAY TEXT (PLACEHOLDER) ============ */
void oled_display_text(uint8_t x, uint8_t y, const char *text) {
    // TODO: Implement text rendering with font
    // For now, just print to serial for debugging
    printf("OLED[%d,%d]: %s\n", x, y, text);
}
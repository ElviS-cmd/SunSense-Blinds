idf_component_register(
    SRCS "oled_display.c"
    INCLUDE_DIRS "."
    REQUIRES 
        system_types
        esp_lcd
        driver
)
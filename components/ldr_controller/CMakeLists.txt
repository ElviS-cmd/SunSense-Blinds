idf_component_register(
    SRCS "ldr_controller.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_adc esp_common
)

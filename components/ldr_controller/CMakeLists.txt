idf_component_register(
    SRCS "ldr_controller.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES driver esp_adc sunsense_types
)

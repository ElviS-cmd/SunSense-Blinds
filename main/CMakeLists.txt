idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES 
        freertos 
        esp_system 
        esp_common 
        driver 
        ldr_controller 
        mode_controller 
        motor_controller 
        button_controller 
        oled_display 
        system_types
)

# Compile flags
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -O2)
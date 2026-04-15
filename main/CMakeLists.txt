idf_component_register(
    SRCS "main.cpp" "network.cpp" "mqtt_handlers.cpp"
    INCLUDE_DIRS "." "../include"
    REQUIRES 
        driver
        freertos
        esp_common
        esp_event
        esp_netif
        esp_wifi
        mqtt
        nvs_flash
        protocomm
        wifi_provisioning
        button_controller
        mode_controller
        motor_controller
        led_controller
        ldr_controller
        servo_controller
        microphone_controller
        voice_command_controller
        runtime_state
        sunsense_types
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)

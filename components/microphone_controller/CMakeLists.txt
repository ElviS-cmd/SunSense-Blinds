idf_component_register(
    SRCS "microphone_controller.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES driver freertos sunsense_types
)

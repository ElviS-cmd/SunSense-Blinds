idf_component_register(
    SRCS "voice_command_controller.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES driver sunsense_types log
)

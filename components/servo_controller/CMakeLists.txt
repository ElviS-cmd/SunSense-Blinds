idf_component_register(
    SRCS "servo_controller.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES driver sunsense_types
)

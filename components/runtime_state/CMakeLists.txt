idf_component_register(
    SRCS "runtime_state.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES
        nvs_flash
        sunsense_types
)

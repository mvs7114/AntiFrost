idf_component_register(
    SRCS "fan_control.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config esp_driver_ledc log
)

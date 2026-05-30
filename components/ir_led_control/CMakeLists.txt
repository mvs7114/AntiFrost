idf_component_register(
    SRCS "ir_led_control.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config esp_driver_gpio log
)

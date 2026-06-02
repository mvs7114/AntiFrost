idf_component_register(
    SRCS "web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config camera_manager esp_driver_gpio esp_http_server fatfs led_control log wear_levelling
    PRIV_REQUIRES wifi_manager
)

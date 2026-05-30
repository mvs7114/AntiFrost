idf_component_register(
    SRCS "web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES camera_manager esp_http_server fatfs ir_led_control log wear_levelling
    PRIV_REQUIRES wifi_manager
)

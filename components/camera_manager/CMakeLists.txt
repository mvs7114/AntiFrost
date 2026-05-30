idf_component_register(
    SRCS "camera_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config esp32-camera esp_driver_ledc esp_http_server log vfs
)

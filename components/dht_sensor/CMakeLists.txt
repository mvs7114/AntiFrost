idf_component_register(
    SRCS "dht_sensor.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config esp_driver_gpio esp_timer
)

idf_component_register(
    SRCS "web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server fatfs log wear_levelling
    PRIV_REQUIRES wifi_manager
)

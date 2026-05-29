idf_component_register(
    SRCS "app_logger.c"
    INCLUDE_DIRS "include"
    REQUIRES sd_storage esp_timer log
)

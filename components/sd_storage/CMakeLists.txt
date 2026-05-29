idf_component_register(
    SRCS "sd_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config fatfs sdmmc esp_driver_sdmmc log
)

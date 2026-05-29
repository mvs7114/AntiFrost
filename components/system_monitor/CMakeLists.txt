idf_component_register(
    SRCS "system_monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_hw_support esp_system heap log spi_flash
)

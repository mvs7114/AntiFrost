idf_component_register(
    SRCS "system_services.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash log
)

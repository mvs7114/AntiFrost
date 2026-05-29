#ifndef ANTIFROST_APP_LOGGER_H
#define ANTIFROST_APP_LOGGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_logger_init(void);
esp_err_t app_logger_write(const char *level, const char *source, const char *message);
esp_err_t app_logger_flush(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef ANTIFROST_WEB_SERVER_H
#define ANTIFROST_WEB_SERVER_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_server_start(void);
bool web_server_is_running(void);
const char *web_server_mount_point(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef ANTIFROST_CAMERA_MANAGER_H
#define ANTIFROST_CAMERA_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAMERA_MANAGER_PARAM_SAFE = 0,
    CAMERA_MANAGER_PARAM_ADVANCED,
    CAMERA_MANAGER_PARAM_DANGEROUS,
} camera_manager_param_risk_t;

typedef struct {
    const char *name;
    const char *label;
    camera_manager_param_risk_t risk;
    int min_value;
    int max_value;
    int step;
    int default_value;
    const char *help;
} camera_manager_param_info_t;

esp_err_t camera_manager_init(void);
const camera_manager_param_info_t *camera_manager_get_params(size_t *count);
const camera_manager_param_info_t *camera_manager_find_param(const char *name);
esp_err_t camera_manager_get_value(const char *name, int *value);
esp_err_t camera_manager_set_value(const char *name, int value, bool dangerous_confirmed);
esp_err_t camera_manager_restore_defaults(void);
bool camera_manager_is_stream_active(void);
esp_err_t camera_manager_send_status(httpd_req_t *req);
esp_err_t camera_manager_send_capture(httpd_req_t *req);
const char *camera_manager_risk_to_string(camera_manager_param_risk_t risk);

#ifdef __cplusplus
}
#endif

#endif

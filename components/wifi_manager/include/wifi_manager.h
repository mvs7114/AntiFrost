#ifndef ANTIFROST_WIFI_MANAGER_H
#define ANTIFROST_WIFI_MANAGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MANAGER_STATE_UNINITIALIZED = 0,
    WIFI_MANAGER_STATE_IDLE,
    WIFI_MANAGER_STATE_STA_CONNECTING,
    WIFI_MANAGER_STATE_STA_CONNECTED,
    WIFI_MANAGER_STATE_STA_DISCONNECTED,
    WIFI_MANAGER_STATE_SOFTAP_ACTIVE,
    WIFI_MANAGER_STATE_ERROR,
} wifi_manager_state_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_configure_sta(const char *ssid, const char *password);
esp_err_t wifi_manager_clear_config(void);
wifi_manager_state_t wifi_manager_get_state(void);

#ifdef __cplusplus
}
#endif

#endif

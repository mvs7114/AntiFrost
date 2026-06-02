#ifndef ANTIFROST_LED_CONTROL_H
#define ANTIFROST_LED_CONTROL_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t led_control_init(void);
esp_err_t led_control_set_enabled(bool enabled);
bool led_control_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif

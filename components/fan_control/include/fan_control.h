#ifndef ANTIFROST_FAN_CONTROL_H
#define ANTIFROST_FAN_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t fan_control_init(void);
esp_err_t fan_control_set(bool enabled, uint8_t duty_percent);
bool fan_control_is_enabled(void);
uint8_t fan_control_get_duty_percent(void);

#ifdef __cplusplus
}
#endif

#endif

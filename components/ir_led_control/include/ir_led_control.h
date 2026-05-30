#ifndef ANTIFROST_IR_LED_CONTROL_H
#define ANTIFROST_IR_LED_CONTROL_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ir_led_control_init(void);
esp_err_t ir_led_control_set_enabled(bool enabled);
bool ir_led_control_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif

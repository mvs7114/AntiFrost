#ifndef ANTIFROST_DHT_SENSOR_H
#define ANTIFROST_DHT_SENSOR_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dht_init(void);
esp_err_t dht_read_data(float *temperature_c, float *humidity_percent);

#ifdef __cplusplus
}
#endif

#endif

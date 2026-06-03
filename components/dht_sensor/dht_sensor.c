#include "dht_sensor.h"

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dht_sensor";

#define DHT_START_LOW_MS 20
#define DHT_START_RELEASE_US 40
#define DHT_TIMEOUT_US 100
#define DHT_BITS 40
#define DHT_ONE_THRESHOLD_US 40

static bool s_initialized;

static esp_err_t dht_wait_level(int level, uint32_t timeout_us, uint32_t *elapsed_us)
{
    int64_t start = esp_timer_get_time();

    while (gpio_get_level(BOARD_DHT11_GPIO) != level) {
        if ((uint32_t)(esp_timer_get_time() - start) > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    if (elapsed_us != NULL) {
        *elapsed_us = (uint32_t)(esp_timer_get_time() - start);
    }

    return ESP_OK;
}

esp_err_t dht_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = BIT64(BOARD_DHT11_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configurazione DHT GPIO%d fallita: %s",
                 BOARD_DHT11_GPIO, esp_err_to_name(err));
        return err;
    }

    gpio_set_level(BOARD_DHT11_GPIO, 1);
    s_initialized = true;
    ESP_LOGI(TAG, "DHT inizializzato su GPIO%d", BOARD_DHT11_GPIO);
    return ESP_OK;
}

esp_err_t dht_read_data(float *temperature_c, float *humidity_percent)
{
    if (temperature_c == NULL || humidity_percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        esp_err_t init_err = dht_init();
        if (init_err != ESP_OK) {
            return init_err;
        }
    }

    uint8_t data[5] = {0};

    gpio_set_direction(BOARD_DHT11_GPIO, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(BOARD_DHT11_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(DHT_START_LOW_MS));
    gpio_set_level(BOARD_DHT11_GPIO, 1);
    esp_rom_delay_us(DHT_START_RELEASE_US);
    gpio_set_direction(BOARD_DHT11_GPIO, GPIO_MODE_INPUT);

    esp_err_t err = dht_wait_level(0, DHT_TIMEOUT_US, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = dht_wait_level(1, DHT_TIMEOUT_US, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = dht_wait_level(0, DHT_TIMEOUT_US, NULL);
    if (err != ESP_OK) {
        return err;
    }

    for (int bit = 0; bit < DHT_BITS; bit++) {
        err = dht_wait_level(1, DHT_TIMEOUT_US, NULL);
        if (err != ESP_OK) {
            return err;
        }

        uint32_t high_us = 0;
        err = dht_wait_level(0, DHT_TIMEOUT_US, &high_us);
        if (err != ESP_OK) {
            return err;
        }

        data[bit / 8] <<= 1;
        if (high_us > DHT_ONE_THRESHOLD_US) {
            data[bit / 8] |= 1U;
        }
    }

    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        ESP_LOGW(TAG, "Checksum DHT non valido: letto=%u atteso=%u",
                 (unsigned int)data[4], (unsigned int)checksum);
        return ESP_ERR_INVALID_CRC;
    }

    *humidity_percent = (float)data[0] + ((float)data[1] / 10.0f);
    *temperature_c = (float)data[2] + ((float)data[3] / 10.0f);
    return ESP_OK;
}

#include "board_config.h"

#include "esp_log.h"

static const char *TAG = "board_config";

esp_err_t board_gpio_init(void)
{
    gpio_config_t dht_config = {
        .pin_bit_mask = BIT64(BOARD_DHT11_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&dht_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configurazione DHT11 GPIO%d fallita: %s",
                 BOARD_DHT11_GPIO, esp_err_to_name(err));
        return err;
    }

    gpio_config_t output_config = {
        .pin_bit_mask = BIT64(BOARD_FAN_PWM_GPIO) | BIT64(BOARD_LED_GPIO) | BIT64(BOARD_GPIO2_TEST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    err = gpio_config(&output_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configurazione uscite GPIO fallita: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(BOARD_FAN_PWM_GPIO, 0));
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(BOARD_LED_GPIO, 0));
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(BOARD_GPIO2_TEST_GPIO, 0));

    ESP_LOGI(TAG, "Board: %s", BOARD_NAME);
    ESP_LOGI(TAG, "GPIO riservati: DHT11=%d, Fan PWM=%d, LED=%d, GPIO2 test=%d",
             BOARD_DHT11_GPIO, BOARD_FAN_PWM_GPIO, BOARD_LED_GPIO, BOARD_GPIO2_TEST_GPIO);

    return ESP_OK;
}

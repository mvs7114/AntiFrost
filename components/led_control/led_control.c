#include "led_control.h"

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "led_control";

static bool s_initialized;
static bool s_enabled;

esp_err_t led_control_init(void)
{
    esp_err_t err = gpio_set_level(BOARD_LED_GPIO, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Spegnimento iniziale LED fallito: %s", esp_err_to_name(err));
        return err;
    }

    s_enabled = false;
    s_initialized = true;
    ESP_LOGI(TAG, "LED control inizializzato su GPIO%d", BOARD_LED_GPIO);
    return ESP_OK;
}

esp_err_t led_control_set_enabled(bool enabled)
{
    if (!s_initialized) {
        esp_err_t err = led_control_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    esp_err_t err = gpio_set_level(BOARD_LED_GPIO, enabled ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Comando LED %s fallito: %s",
                 enabled ? "ON" : "OFF",
                 esp_err_to_name(err));
        return err;
    }

    s_enabled = enabled;
    ESP_LOGI(TAG, "LED %s", enabled ? "ON" : "OFF");
    return ESP_OK;
}

bool led_control_is_enabled(void)
{
    return s_enabled;
}

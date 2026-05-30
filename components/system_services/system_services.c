#include "system_services.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "system_services";

esp_err_t system_services_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS non valida o incompatibile, erase e reinizializzazione");

        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Erase NVS fallito: %s", esp_err_to_name(err));
            return err;
        }

        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Inizializzazione NVS fallita: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "NVS inizializzata");
    return ESP_OK;
}

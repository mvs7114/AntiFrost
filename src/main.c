#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>

#include "app_logger.h"
#include "board_config.h"
#include "sd_storage.h"
#include "system_monitor.h"

static const char *TAG = "antifrost";

#define HEARTBEAT_ENABLED 1

void app_main(void)
{
    bool app_logger_ready = false;

    ESP_ERROR_CHECK(board_gpio_init());
    ESP_ERROR_CHECK(sys_monitor_init());

    sys_monitor_log_boot();

    esp_err_t sd_err = sd_storage_init();
    if (sd_err == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(sd_storage_write_test_file());

        esp_err_t logger_err = app_logger_init();
        if (logger_err == ESP_OK) {
            app_logger_ready = true;
            ESP_ERROR_CHECK_WITHOUT_ABORT(app_logger_write("INFO", TAG, "boot completato"));
            ESP_ERROR_CHECK_WITHOUT_ABORT(app_logger_write("INFO", TAG, "test SD/log persistente avviato"));
        }
    } else {
        ESP_LOGW(TAG, "SD non disponibile, log persistenti disattivati");
    }

    while (true) {
#if HEARTBEAT_ENABLED
        ESP_LOGI(TAG, "Sistema attivo");
        sys_monitor_log_snapshot();
        if (app_logger_ready) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(app_logger_write("INFO", TAG, "heartbeat"));
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

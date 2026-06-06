#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>

#include "app_logger.h"
#include "board_config.h"
#include "dht_sensor.h"
#include "fan_control.h"
#include "led_control.h"
#include "sd_storage.h"
#include "system_services.h"
#include "system_monitor.h"
#include "web_server.h"
#include "wifi_manager.h"

static const char *TAG = "antifrost";

#define WIFI_MANAGER_ENABLED 1
#define WEB_SERVER_ENABLED 1
#define HEARTBEAT_ENABLED 0
#define HEARTBEAT_INTERVAL_MS 30000
#ifndef APP_SERIAL_LOG_LEVEL
#define APP_SERIAL_LOG_LEVEL ESP_LOG_INFO
#endif

static const char *app_log_level_to_string(esp_log_level_t level)
{
    switch (level) {
    case ESP_LOG_NONE:
        return "NONE";
    case ESP_LOG_ERROR:
        return "ERROR";
    case ESP_LOG_WARN:
        return "WARNING";
    case ESP_LOG_INFO:
        return "INFO";
    case ESP_LOG_DEBUG:
        return "DEBUG";
    case ESP_LOG_VERBOSE:
        return "VERBOSE";
    default:
        return "UNKNOWN";
    }
}

static void app_configure_serial_logging(void)
{
    esp_log_level_set("*", APP_SERIAL_LOG_LEVEL);
    ESP_LOGI(TAG, "Log seriale impostato a livello %s",
             app_log_level_to_string(APP_SERIAL_LOG_LEVEL));
}

void app_main(void)
{
#if HEARTBEAT_ENABLED
    bool app_logger_ready = false;
#endif

    app_configure_serial_logging();

    ESP_ERROR_CHECK(board_gpio_init());
    ESP_ERROR_CHECK(dht_init());
    ESP_ERROR_CHECK(fan_control_init());
    ESP_ERROR_CHECK(led_control_init());
    ESP_ERROR_CHECK(system_services_init());
    ESP_ERROR_CHECK(sys_monitor_init());

    sys_monitor_log_boot();

#if WIFI_MANAGER_ENABLED
    esp_err_t wifi_err = wifi_manager_start();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi manager non avviato: %s", esp_err_to_name(wifi_err));
    }
#endif

#if WEB_SERVER_ENABLED
    esp_err_t web_err = web_server_start();
    if (web_err != ESP_OK) {
        ESP_LOGW(TAG, "Web server non avviato: %s", esp_err_to_name(web_err));
    }
#endif

    esp_err_t sd_err = sd_storage_init();
    if (sd_err == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(sd_storage_write_test_file());

        esp_err_t logger_err = app_logger_init();
        if (logger_err == ESP_OK) {
#if HEARTBEAT_ENABLED
            app_logger_ready = true;
#endif
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
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
    }
}

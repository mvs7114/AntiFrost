#include "system_monitor.h"

#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "system_monitor";

static const char *reset_reason_to_string(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "SDIO";
    case ESP_RST_UNKNOWN:
    default:
        return "unknown";
    }
}

static const char *chip_model_to_string(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32S3:
        return "ESP32-S3";
    default:
        return "ESP32";
    }
}

esp_err_t sys_monitor_init(void)
{
    uint32_t flash_size = 0;
    esp_err_t err = esp_flash_get_size(NULL, &flash_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lettura dimensione flash fallita: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "System monitor inizializzato");
    return ESP_OK;
}

esp_err_t sys_monitor_get_snapshot(sys_monitor_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t flash_size = 0;
    esp_err_t err = esp_flash_get_size(NULL, &flash_size);
    if (err != ESP_OK) {
        return err;
    }

    snapshot->flash_size_bytes = flash_size;
    snapshot->psram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    snapshot->psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    snapshot->heap_internal_total_bytes = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    snapshot->heap_internal_free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    snapshot->uptime_ms = sys_monitor_get_uptime_ms();
    snapshot->reset_reason = esp_reset_reason();

    return ESP_OK;
}

uint32_t sys_monitor_get_heap_free(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

uint32_t sys_monitor_get_psram_free(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

uint64_t sys_monitor_get_uptime_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000U;
}

void sys_monitor_log_boot(void)
{
    esp_chip_info_t chip_info;
    sys_monitor_snapshot_t snapshot;

    esp_chip_info(&chip_info);

    esp_err_t err = sys_monitor_get_snapshot(&snapshot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Snapshot diagnostico non disponibile: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "AntiFrost avviato");
    ESP_LOGI(TAG, "Chip %s, %d core, revision %d",
             chip_model_to_string(chip_info.model),
             chip_info.cores,
             chip_info.revision);
    ESP_LOGI(TAG, "Flash %" PRIu32 " MB", snapshot.flash_size_bytes / (1024U * 1024U));
    ESP_LOGI(TAG, "PSRAM circa %" PRIu32 " MB (%" PRIu32 " KB liberi)",
             snapshot.psram_total_bytes / (1024U * 1024U),
             snapshot.psram_free_bytes / 1024U);
    ESP_LOGI(TAG, "Heap interno: %" PRIu32 " KB liberi / %" PRIu32 " KB totali",
             snapshot.heap_internal_free_bytes / 1024U,
             snapshot.heap_internal_total_bytes / 1024U);
    ESP_LOGI(TAG, "Reset reason: %s", reset_reason_to_string(snapshot.reset_reason));
}

void sys_monitor_log_snapshot(void)
{
    sys_monitor_snapshot_t snapshot;
    esp_err_t err = sys_monitor_get_snapshot(&snapshot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Snapshot diagnostico non disponibile: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG,
             "Sistema attivo - uptime=%" PRIu64 " ms, heap=%" PRIu32 " KB, psram=%" PRIu32 " KB",
             snapshot.uptime_ms,
             snapshot.heap_internal_free_bytes / 1024U,
             snapshot.psram_free_bytes / 1024U);
}

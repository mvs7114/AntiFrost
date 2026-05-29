#include "app_logger.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "sd_storage.h"

#define APP_LOGGER_MAX_FILE_BYTES (64 * 1024)
#define APP_LOGGER_ROTATED_FILES 4

static const char *TAG = "app_logger";
static const char *LOG_PATH = SD_STORAGE_MOUNT_POINT "/logs/app.log";

static bool s_ready;

static void rotate_logs_if_needed(void)
{
    struct stat st;
    if (stat(LOG_PATH, &st) != 0 || st.st_size < APP_LOGGER_MAX_FILE_BYTES) {
        return;
    }

    char old_path[96];
    char new_path[96];

    snprintf(old_path,
             sizeof(old_path),
             SD_STORAGE_MOUNT_POINT "/logs/app.%d.log",
             APP_LOGGER_ROTATED_FILES);
    unlink(old_path);

    for (int index = APP_LOGGER_ROTATED_FILES - 1; index >= 1; index--) {
        snprintf(old_path, sizeof(old_path), SD_STORAGE_MOUNT_POINT "/logs/app.%d.log", index);
        snprintf(new_path, sizeof(new_path), SD_STORAGE_MOUNT_POINT "/logs/app.%d.log", index + 1);
        rename(old_path, new_path);
    }

    rename(LOG_PATH, SD_STORAGE_MOUNT_POINT "/logs/app.1.log");
}

esp_err_t app_logger_init(void)
{
    if (!sd_storage_is_mounted()) {
        ESP_LOGW(TAG, "Logger persistente non attivo: SD non montata");
        return ESP_ERR_INVALID_STATE;
    }

    rotate_logs_if_needed();
    s_ready = true;

    return app_logger_write("INFO", TAG, "app_logger inizializzato");
}

esp_err_t app_logger_write(const char *level, const char *source, const char *message)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (level == NULL || source == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rotate_logs_if_needed();

    FILE *file = fopen(LOG_PATH, "a");
    if (file == NULL) {
        ESP_LOGE(TAG, "Apertura log persistente fallita");
        return ESP_FAIL;
    }

    uint64_t timestamp_ms = (uint64_t)esp_timer_get_time() / 1000U;
    int written = fprintf(file,
                          "%" PRIu64 ";%s;%s;%s\n",
                          timestamp_ms,
                          level,
                          source,
                          message);
    int flush_result = fflush(file);
    int close_result = fclose(file);

    if (written < 0 || flush_result != 0 || close_result != 0) {
        ESP_LOGE(TAG, "Scrittura log persistente fallita");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t app_logger_flush(void)
{
    return s_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

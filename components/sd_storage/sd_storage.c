#include "sd_storage.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board_config.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_storage";

static sdmmc_card_t *s_card;
static bool s_mounted;
static uint32_t s_next_image_index;

static esp_err_t ensure_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    if (mkdir(path, 0775) == 0) {
        return ESP_OK;
    }

    return ESP_FAIL;
}

static esp_err_t build_absolute_path(const char *relative_path,
                                     char *path,
                                     size_t path_len)
{
    if (relative_path == NULL || relative_path[0] == '\0' ||
        path == NULL || path_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *clean_path = relative_path[0] == '/' ? relative_path + 1 : relative_path;
    int written = snprintf(path, path_len, "%s/%s", SD_STORAGE_MOUNT_POINT, clean_path);
    if (written < 0 || (size_t)written >= path_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static void select_next_image_index(void)
{
    char path[96];

    s_next_image_index = 0;
    for (uint32_t index = 0; index < MAX_IMAGES; index++) {
        int written = snprintf(path,
                               sizeof(path),
                               "%s/images/capture_%03" PRIu32 ".jpg",
                               SD_STORAGE_MOUNT_POINT,
                               index);
        if (written < 0 || (size_t)written >= sizeof(path)) {
            return;
        }

        if (access(path, F_OK) != 0) {
            s_next_image_index = index;
            return;
        }
    }
}

esp_err_t sd_storage_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = BOARD_SD_CLK_GPIO;
    slot_config.cmd = BOARD_SD_CMD_GPIO;
    slot_config.d0 = BOARD_SD_D0_GPIO;

    ESP_LOGI(TAG, "Mount SD FAT32 su %s", SD_STORAGE_MOUNT_POINT);

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_STORAGE_MOUNT_POINT,
                                            &host,
                                            &slot_config,
                                            &mount_config,
                                            &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mount SD fallito: %s", esp_err_to_name(err));
        return err;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);

    ESP_ERROR_CHECK_WITHOUT_ABORT(ensure_directory(SD_STORAGE_MOUNT_POINT "/logs"));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ensure_directory(SD_STORAGE_MOUNT_POINT "/images"));
    select_next_image_index();

    ESP_LOGI(TAG, "SD montata, prossimo indice immagine: %" PRIu32, s_next_image_index);
    return ESP_OK;
}

bool sd_storage_is_mounted(void)
{
    return s_mounted;
}

const char *sd_storage_mount_point(void)
{
    return SD_STORAGE_MOUNT_POINT;
}

esp_err_t sd_storage_write_text_file(const char *relative_path, const char *content)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (content == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[160];
    esp_err_t err = build_absolute_path(relative_path, path, sizeof(path));
    if (err != ESP_OK) {
        return err;
    }

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Apertura file fallita: %s", path);
        return ESP_FAIL;
    }

    size_t written = fwrite(content, 1, strlen(content), file);
    int close_result = fclose(file);
    if (written != strlen(content) || close_result != 0) {
        ESP_LOGE(TAG, "Scrittura file fallita: %s", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File scritto: %s", path);
    return ESP_OK;
}

esp_err_t sd_storage_save_jpeg(const uint8_t *jpeg_data,
                               size_t jpeg_len,
                               char *saved_path,
                               size_t saved_path_len)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (jpeg_data == NULL || jpeg_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[96];
    uint32_t image_index = s_next_image_index % MAX_IMAGES;
    int written = snprintf(path,
                           sizeof(path),
                           "%s/images/capture_%03" PRIu32 ".jpg",
                           SD_STORAGE_MOUNT_POINT,
                           image_index);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Apertura JPEG fallita: %s", path);
        return ESP_FAIL;
    }

    size_t bytes_written = fwrite(jpeg_data, 1, jpeg_len, file);
    int close_result = fclose(file);
    if (bytes_written != jpeg_len || close_result != 0) {
        ESP_LOGE(TAG, "Scrittura JPEG fallita: %s", path);
        return ESP_FAIL;
    }

    s_next_image_index = (image_index + 1U) % MAX_IMAGES;
    if (saved_path != NULL && saved_path_len > 0) {
        snprintf(saved_path, saved_path_len, "%s", path);
    }

    ESP_LOGI(TAG, "JPEG salvato: %s (%u byte)", path, (unsigned int)jpeg_len);
    return ESP_OK;
}

esp_err_t sd_storage_write_test_file(void)
{
    return sd_storage_write_text_file("logs/sd_test.txt", "AntiFrost SD test OK\n");
}

#ifndef ANTIFROST_SD_STORAGE_H
#define ANTIFROST_SD_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_STORAGE_MOUNT_POINT "/sdcard"

#ifndef MAX_IMAGES
#define MAX_IMAGES 100
#endif

#if MAX_IMAGES <= 0
#error "MAX_IMAGES deve essere maggiore di zero"
#endif

esp_err_t sd_storage_init(void);
bool sd_storage_is_mounted(void);
const char *sd_storage_mount_point(void);
esp_err_t sd_storage_write_text_file(const char *relative_path, const char *content);
esp_err_t sd_storage_save_jpeg(const uint8_t *jpeg_data,
                               size_t jpeg_len,
                               char *saved_path,
                               size_t saved_path_len);
esp_err_t sd_storage_write_test_file(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef ANTIFROST_SYSTEM_MONITOR_H
#define ANTIFROST_SYSTEM_MONITOR_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t flash_size_bytes;
    uint32_t psram_total_bytes;
    uint32_t psram_free_bytes;
    uint32_t heap_internal_total_bytes;
    uint32_t heap_internal_free_bytes;
    uint64_t uptime_ms;
    esp_reset_reason_t reset_reason;
} sys_monitor_snapshot_t;

esp_err_t sys_monitor_init(void);
esp_err_t sys_monitor_get_snapshot(sys_monitor_snapshot_t *snapshot);
uint32_t sys_monitor_get_heap_free(void);
uint32_t sys_monitor_get_psram_free(void);
uint64_t sys_monitor_get_uptime_ms(void);
void sys_monitor_log_boot(void);
void sys_monitor_log_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif

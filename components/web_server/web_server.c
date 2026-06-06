#include "web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "camera_manager.h"
#include "dht_sensor.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "fan_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_control.h"
#include "wear_levelling.h"
#include "wifi_manager.h"

static const char *TAG = "web_server";

#define WEB_SERVER_MOUNT_POINT "/www"
#define WEB_SERVER_PARTITION_LABEL "storage"
#define WEB_SERVER_MAX_PATH_LEN 160
#define WEB_SERVER_FILE_BUFFER_SIZE 1024
#define WEB_SERVER_MAX_STATIC_FILE_SIZE 65536
#define WEB_SERVER_FORM_BUFFER_SIZE 256
#define WEB_SERVER_QUERY_BUFFER_SIZE 192
#define WEB_SERVER_QUERY_VALUE_SIZE 64
#define WEB_SERVER_PROFILE_BUFFER_SIZE 4096
#define WEB_SERVER_SSID_BUFFER_SIZE 33
#define WEB_SERVER_PASSWORD_BUFFER_SIZE 65
#define WEB_SERVER_WIFI_APPLY_DELAY_MS 1500
#define WEB_SERVER_RESTART_DELAY_MS 1500
#define WEB_SERVER_MAX_URI_HANDLERS 24
#define WEB_SERVER_CTRL_PORT 32768
#define WEB_SERVER_STACK_SIZE 8192
#define WEB_SERVER_MAX_OPEN_SOCKETS 7
#define WEB_SERVER_RECV_WAIT_TIMEOUT_SEC 5
#define WEB_SERVER_SEND_WAIT_TIMEOUT_SEC 5
#define WEB_SERVER_SEND_BUDGET_MS (WEB_SERVER_SEND_WAIT_TIMEOUT_SEC * 1000)
#define WEB_SERVER_SENSOR_CACHE_MS 300000
#define WEB_SERVER_SENSOR_FIRST_READ_RETRY_DELAY_MS 2500

#ifndef ANTIFROST_BUILD_VERSION
#define ANTIFROST_BUILD_VERSION "dev"
#endif

typedef struct {
    char ssid[WEB_SERVER_SSID_BUFFER_SIZE];
    char password[WEB_SERVER_PASSWORD_BUFFER_SIZE];
} web_server_wifi_request_t;

static httpd_handle_t s_server;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static bool s_fs_mounted;
static bool s_sensor_cache_valid;
static float s_sensor_temperature_c;
static float s_sensor_humidity_percent;
static int64_t s_sensor_last_read_ms;

static const char *FALLBACK_INDEX_HTML =
    "<!doctype html><html lang=\"it\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>AntiFrost</title><style>"
    "body{margin:0;font-family:system-ui,-apple-system,Segoe UI,sans-serif;"
    "background:#f7f8fa;color:#17202a;display:grid;min-height:100vh;place-items:center}"
    "main{width:min(34rem,calc(100vw - 2rem));padding:2rem;border:1px solid #d8dee6;"
    "background:#fff;border-radius:8px}h1{margin:0 0 .75rem;font-size:2rem}"
    "p{margin:.35rem 0;color:#4f5b66;line-height:1.45}</style></head><body>"
    "<main><h1>AntiFrost</h1><p>Firmware avviato.</p>"
    "<p>Interfaccia web base attiva.</p>"
    "<p><a href=\"/setup.html\">Configura Wi-Fi</a></p></main></body></html>";

static int64_t web_server_now_ms(void)
{
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static const char *web_server_wifi_state_to_string(wifi_manager_state_t state)
{
    switch (state) {
    case WIFI_MANAGER_STATE_UNINITIALIZED:
        return "UNINITIALIZED";
    case WIFI_MANAGER_STATE_IDLE:
        return "IDLE";
    case WIFI_MANAGER_STATE_STA_CONNECTING:
        return "STA_CONNECTING";
    case WIFI_MANAGER_STATE_STA_CONNECTED:
        return "STA_CONNECTED";
    case WIFI_MANAGER_STATE_STA_DISCONNECTED:
        return "STA_DISCONNECTED";
    case WIFI_MANAGER_STATE_SOFTAP_ACTIVE:
        return "SOFTAP_ACTIVE";
    case WIFI_MANAGER_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static const char *web_server_wifi_mode_to_string(wifi_mode_t mode)
{
    switch (mode) {
    case WIFI_MODE_NULL:
        return "NULL";
    case WIFI_MODE_STA:
        return "STA";
    case WIFI_MODE_AP:
        return "SOFTAP";
    case WIFI_MODE_APSTA:
        return "APSTA";
    default:
        return "UNKNOWN";
    }
}

static void web_server_json_safe_copy(char *out, size_t out_len, const uint8_t *in)
{
    size_t dst = 0;

    for (size_t src = 0; in[src] != '\0' && dst + 1 < out_len; src++) {
        char c = (char)in[src];
        if (c == '"' || c == '\\') {
            if (dst + 2 >= out_len) {
                break;
            }
            out[dst++] = '\\';
            out[dst++] = c;
            continue;
        }

        if ((unsigned char)c < 32) {
            out[dst++] = '_';
            continue;
        }

        out[dst++] = c;
    }

    out[dst] = '\0';
}

static bool web_server_get_ip_for_ifkey(const char *ifkey, char *ip_out, size_t ip_out_len)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(ifkey);
    if (netif == NULL) {
        return false;
    }

    esp_netif_ip_info_t ip_info = {0};
    esp_err_t err = esp_netif_get_ip_info(netif, &ip_info);
    if (err != ESP_OK || ip_info.ip.addr == 0) {
        return false;
    }

    int written = snprintf(ip_out, ip_out_len, IPSTR, IP2STR(&ip_info.ip));
    return written > 0 && (size_t)written < ip_out_len;
}

static int web_server_hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

static void web_server_url_decode(char *value)
{
    char *src = value;
    char *dst = value;

    while (*src != '\0') {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
            continue;
        }

        if (*src == '%' && src[1] != '\0' && src[2] != '\0') {
            int high = web_server_hex_value(src[1]);
            int low = web_server_hex_value(src[2]);
            if (high >= 0 && low >= 0) {
                *dst++ = (char)((high << 4) | low);
                src += 3;
                continue;
            }
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}

static esp_err_t web_server_query_value(httpd_req_t *req,
                                        const char *key,
                                        char *out,
                                        size_t out_len)
{
    char query[WEB_SERVER_QUERY_BUFFER_SIZE] = {0};
    esp_err_t err = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (err != ESP_OK) {
        return err;
    }

    err = httpd_query_key_value(query, key, out, out_len);
    if (err == ESP_OK) {
        web_server_url_decode(out);
    }

    return err;
}

static esp_err_t web_server_form_value(const char *body,
                                       const char *key,
                                       char *out,
                                       size_t out_len)
{
    size_t key_len = strlen(key);
    const char *cursor = body;

    while (cursor != NULL && *cursor != '\0') {
        const char *next = strchr(cursor, '&');
        size_t pair_len = next == NULL ? strlen(cursor) : (size_t)(next - cursor);

        if (pair_len > key_len && strncmp(cursor, key, key_len) == 0 && cursor[key_len] == '=') {
            size_t value_len = pair_len - key_len - 1;
            if (value_len >= out_len) {
                return ESP_ERR_NO_MEM;
            }

            memcpy(out, cursor + key_len + 1, value_len);
            out[value_len] = '\0';
            web_server_url_decode(out);
            return ESP_OK;
        }

        cursor = next == NULL ? NULL : next + 1;
    }

    return ESP_ERR_NOT_FOUND;
}

static bool web_server_string_is_true(const char *value)
{
    return strcmp(value, "1") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "yes") == 0;
}

static esp_err_t web_server_system_status_get_handler(httpd_req_t *req)
{
    char response[160] = {0};
    int len = snprintf(response,
                       sizeof(response),
                       "{\"version\":\"%s\",\"build_date\":\"%s\",\"build_time\":\"%s\"}",
                       ANTIFROST_BUILD_VERSION,
                       __DATE__,
                       __TIME__);
    if (len <= 0 || (size_t)len >= sizeof(response)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status sistema troppo lungo");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static esp_err_t web_server_sensor_status_get_handler(httpd_req_t *req)
{
    char refresh_text[WEB_SERVER_QUERY_VALUE_SIZE] = {0};
    bool force_refresh = false;
    esp_err_t query_err = web_server_query_value(req, "refresh", refresh_text, sizeof(refresh_text));
    if (query_err == ESP_OK && refresh_text[0] != '\0') {
        force_refresh = web_server_string_is_true(refresh_text);
    }

    int64_t now_ms = web_server_now_ms();
    bool cache_expired = !s_sensor_cache_valid ||
                         (now_ms - s_sensor_last_read_ms) >= WEB_SERVER_SENSOR_CACHE_MS;
    esp_err_t read_err = ESP_OK;
    bool refreshed = false;

    if (force_refresh || cache_expired) {
        float temperature_c = 0.0f;
        float humidity_percent = 0.0f;
        read_err = dht_read_data(&temperature_c, &humidity_percent);
        if (read_err != ESP_OK && !s_sensor_cache_valid) {
            vTaskDelay(pdMS_TO_TICKS(WEB_SERVER_SENSOR_FIRST_READ_RETRY_DELAY_MS));
            read_err = dht_read_data(&temperature_c, &humidity_percent);
        }
        refreshed = true;
        if (read_err == ESP_OK) {
            s_sensor_temperature_c = temperature_c;
            s_sensor_humidity_percent = humidity_percent;
            s_sensor_last_read_ms = web_server_now_ms();
            s_sensor_cache_valid = true;
            now_ms = s_sensor_last_read_ms;
        }
    }

    char response[192] = {0};
    int len = 0;
    if (s_sensor_cache_valid) {
        int64_t age_ms = now_ms - s_sensor_last_read_ms;
        if (age_ms < 0) {
            age_ms = 0;
        }

        int temperature_tenths = (int)(s_sensor_temperature_c * 10.0f);
        int humidity_tenths = (int)(s_sensor_humidity_percent * 10.0f);
        len = snprintf(response,
                       sizeof(response),
                       "{\"ok\":true,\"temperature_c\":%d.%d,\"humidity_percent\":%d.%d,"
                       "\"age_ms\":%lld,\"cached\":%s,\"refreshed\":%s}",
                       temperature_tenths / 10,
                       abs(temperature_tenths % 10),
                       humidity_tenths / 10,
                       abs(humidity_tenths % 10),
                       (long long)age_ms,
                       refreshed && read_err == ESP_OK ? "false" : "true",
                       refreshed && read_err == ESP_OK ? "true" : "false");
    } else {
        len = snprintf(response,
                       sizeof(response),
                       "{\"ok\":false,\"error\":\"%s\",\"temperature_c\":null,"
                       "\"humidity_percent\":null,\"age_ms\":null,\"cached\":false,"
                       "\"refreshed\":%s}",
                       esp_err_to_name(read_err),
                       refreshed ? "true" : "false");
    }

    if (len <= 0 || (size_t)len >= sizeof(response)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status sensore troppo lungo");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static esp_err_t web_server_camera_params_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP camera parameters");

    size_t count = 0;
    const camera_manager_param_info_t *params = camera_manager_get_params(&count);

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr_chunk(req, "{\"parameters\":[");
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < count; i++) {
        const camera_manager_param_info_t *param = &params[i];
        int value = 0;
        (void)camera_manager_get_value(param->name, &value);

        char item[512] = {0};
        int len = snprintf(
            item,
            sizeof(item),
            "%s{\"name\":\"%s\",\"label\":\"%s\",\"risk\":\"%s\",\"min\":%d,\"max\":%d,\"step\":%d,\"default\":%d,\"value\":%d,\"help\":\"%s\"}",
            i == 0 ? "" : ",",
            param->name,
            param->label,
            camera_manager_risk_to_string(param->risk),
            param->min_value,
            param->max_value,
            param->step,
            param->default_value,
            value,
            param->help);
        if (len <= 0 || (size_t)len >= sizeof(item)) {
            httpd_resp_sendstr_chunk(req, NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Parametro camera troppo lungo");
            return ESP_FAIL;
        }

        err = httpd_resp_send_chunk(req, item, len);
        if (err != ESP_OK) {
            httpd_resp_sendstr_chunk(req, NULL);
            return err;
        }
    }

    err = httpd_resp_sendstr_chunk(req, "]}");
    if (err != ESP_OK) {
        httpd_resp_sendstr_chunk(req, NULL);
        return err;
    }

    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t web_server_camera_control_get_handler(httpd_req_t *req)
{
    char name[WEB_SERVER_QUERY_VALUE_SIZE] = {0};
    char value_text[WEB_SERVER_QUERY_VALUE_SIZE] = {0};
    char dangerous_text[WEB_SERVER_QUERY_VALUE_SIZE] = {0};

    esp_err_t err = web_server_query_value(req, "var", name, sizeof(name));
    if (err != ESP_OK || name[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Parametro camera mancante");
        return ESP_ERR_INVALID_ARG;
    }

    err = web_server_query_value(req, "val", value_text, sizeof(value_text));
    if (err != ESP_OK || value_text[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Valore camera mancante");
        return ESP_ERR_INVALID_ARG;
    }

    char *end = NULL;
    long value = strtol(value_text, &end, 10);
    if (end == value_text || *end != '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Valore camera non numerico");
        return ESP_ERR_INVALID_ARG;
    }

    bool dangerous_confirmed = false;
    if (web_server_query_value(req, "dangerous", dangerous_text, sizeof(dangerous_text)) == ESP_OK) {
        dangerous_confirmed = web_server_string_is_true(dangerous_text);
    }

    err = camera_manager_set_value(name, (int)value, dangerous_confirmed);
    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Parametro camera sconosciuto");
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Parametro rischioso non confermato");
        return err;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Parametro camera non valido");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t web_server_camera_defaults_post_handler(httpd_req_t *req)
{
    esp_err_t err = camera_manager_restore_defaults();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Reset default camera fallito");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t web_server_camera_status_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP camera status");
    return camera_manager_send_status(req);
}

static esp_err_t web_server_camera_stream_status_get_handler(httpd_req_t *req)
{
    char response[32] = {0};
    bool active = camera_manager_is_stream_active();
    int len = snprintf(response,
                       sizeof(response),
                       "{\"active\":%s}",
                       active ? "true" : "false");
    if (len <= 0 || (size_t)len >= sizeof(response)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status stream troppo lungo");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static esp_err_t web_server_camera_capture_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP camera capture");
    return camera_manager_send_capture(req);
}

static esp_err_t web_server_led_status_get_handler(httpd_req_t *req)
{
    bool enabled = led_control_is_enabled();
    char response[32] = {0};
    int len = snprintf(response,
                       sizeof(response),
                       "{\"enabled\":%s}",
                       enabled ? "true" : "false");
    if (len <= 0 || (size_t)len >= sizeof(response)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status LED troppo lungo");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static esp_err_t web_server_led_control_get_handler(httpd_req_t *req)
{
    char enabled_text[WEB_SERVER_QUERY_VALUE_SIZE] = {0};
    esp_err_t err = web_server_query_value(req, "enabled", enabled_text, sizeof(enabled_text));
    if (err != ESP_OK || enabled_text[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Parametro LED mancante");
        return ESP_ERR_INVALID_ARG;
    }

    err = led_control_set_enabled(web_server_string_is_true(enabled_text));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Comando LED fallito");
        return err;
    }

    return web_server_led_status_get_handler(req);
}

static esp_err_t web_server_fan_status_get_handler(httpd_req_t *req)
{
    bool enabled = fan_control_is_enabled();
    uint8_t duty_percent = fan_control_get_duty_percent();
    char response[64] = {0};
    int len = snprintf(response,
                       sizeof(response),
                       "{\"enabled\":%s,\"duty_percent\":%u}",
                       enabled ? "true" : "false",
                       (unsigned int)duty_percent);
    if (len <= 0 || (size_t)len >= sizeof(response)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status ventola troppo lungo");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static esp_err_t web_server_fan_control_get_handler(httpd_req_t *req)
{
    char enabled_text[WEB_SERVER_QUERY_VALUE_SIZE] = {0};
    char duty_text[WEB_SERVER_QUERY_VALUE_SIZE] = {0};

    esp_err_t err = web_server_query_value(req, "enabled", enabled_text, sizeof(enabled_text));
    if (err != ESP_OK || enabled_text[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Parametro ventola mancante");
        return ESP_ERR_INVALID_ARG;
    }

    err = web_server_query_value(req, "duty", duty_text, sizeof(duty_text));
    if (err != ESP_OK || duty_text[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Duty ventola mancante");
        return ESP_ERR_INVALID_ARG;
    }

    char *end = NULL;
    long duty = strtol(duty_text, &end, 10);
    if (end == duty_text || *end != '\0' || duty < 0 || duty > 100) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Duty ventola non valido");
        return ESP_ERR_INVALID_ARG;
    }

    err = fan_control_set(web_server_string_is_true(enabled_text), (uint8_t)duty);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Comando ventola fallito");
        return err;
    }

    return web_server_fan_status_get_handler(req);
}

static esp_err_t web_server_test_status_get_handler(httpd_req_t *req)
{
    char response[128] = {0};
    int len = snprintf(response,
                       sizeof(response),
                       "{\"gpio2\":%d,\"gpio21\":{\"enabled\":%s,\"duty\":%u}}",
                       gpio_get_level(BOARD_GPIO2_TEST_GPIO),
                       fan_control_is_enabled() ? "true" : "false",
                       (unsigned int)fan_control_get_duty_percent());
    if (len <= 0 || (size_t)len >= sizeof(response)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status test troppo lungo");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static esp_err_t web_server_test_gpio_control_get_handler(httpd_req_t *req)
{
    char target[WEB_SERVER_QUERY_VALUE_SIZE] = {0};
    char level_text[WEB_SERVER_QUERY_VALUE_SIZE] = {0};

    esp_err_t err = web_server_query_value(req, "target", target, sizeof(target));
    if (err != ESP_OK || target[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Target test non valido");
        return ESP_ERR_INVALID_ARG;
    }

    err = web_server_query_value(req, "level", level_text, sizeof(level_text));
    if (err != ESP_OK || level_text[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Livello test mancante");
        return ESP_ERR_INVALID_ARG;
    }

    int level = web_server_string_is_true(level_text) ? 1 : 0;
    if (strcmp(target, "gpio2") == 0) {
        err = gpio_set_level(BOARD_GPIO2_TEST_GPIO, level);
    } else if (strcmp(target, "gpio21") == 0 || strcmp(target, "fan") == 0) {
        err = fan_control_set(level != 0, level != 0 ? 100 : 0);
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Target test non valido");
        return ESP_ERR_INVALID_ARG;
    }

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Comando test GPIO fallito");
        return err;
    }

    return web_server_test_status_get_handler(req);
}

static esp_err_t web_server_camera_profile_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= WEB_SERVER_PROFILE_BUFFER_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Profilo camera non valido");
        return ESP_ERR_INVALID_ARG;
    }

    char *body = calloc(1, WEB_SERVER_PROFILE_BUFFER_SIZE);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memoria profilo insufficiente");
        return ESP_ERR_NO_MEM;
    }

    int received_total = 0;

    while (received_total < req->content_len) {
        int received = httpd_req_recv(req,
                                      body + received_total,
                                      req->content_len - received_total);
        if (received <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Ricezione profilo fallita");
            return ESP_FAIL;
        }

        received_total += received;
    }
    body[received_total] = '\0';

    FILE *file = fopen(WEB_SERVER_MOUNT_POINT "/camera_profile.json", "w");
    if (file == NULL) {
        free(body);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Apertura profilo FAT fallita");
        return ESP_FAIL;
    }

    size_t written = fwrite(body, 1, (size_t)received_total, file);
    if (written != (size_t)received_total) {
        fclose(file);
        free(body);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scrittura profilo FAT fallita");
        return ESP_FAIL;
    }

    if (fclose(file) != 0) {
        free(body);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Chiusura profilo FAT fallita");
        return ESP_FAIL;
    }

    free(body);
    ESP_LOGI(TAG, "Profilo camera salvato su FAT");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t web_server_camera_reference_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Riferimento camera vuoto");
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(WEB_SERVER_MOUNT_POINT "/camera_reference.jpg", "wb");
    if (file == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Apertura riferimento FAT fallita");
        return ESP_FAIL;
    }

    char buffer[WEB_SERVER_FILE_BUFFER_SIZE];
    int remaining = req->content_len;

    while (remaining > 0) {
        int to_read = remaining > (int)sizeof(buffer) ? (int)sizeof(buffer) : remaining;
        int received = httpd_req_recv(req, buffer, to_read);
        if (received <= 0) {
            fclose(file);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Ricezione riferimento fallita");
            return ESP_FAIL;
        }

        size_t written = fwrite(buffer, 1, (size_t)received, file);
        if (written != (size_t)received) {
            fclose(file);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scrittura riferimento FAT fallita");
            return ESP_FAIL;
        }

        remaining -= received;
    }

    if (fclose(file) != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Chiusura riferimento FAT fallita");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Riferimento camera salvato su FAT");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"path\":\"/camera_reference.jpg\"}");
}

static void web_server_wifi_apply_task(void *arg)
{
    web_server_wifi_request_t *request = (web_server_wifi_request_t *)arg;

    vTaskDelay(pdMS_TO_TICKS(WEB_SERVER_WIFI_APPLY_DELAY_MS));

    esp_err_t err = wifi_manager_configure_sta(request->ssid, request->password);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Applicazione configurazione Wi-Fi fallita: %s", esp_err_to_name(err));
    }

    free(request);
    vTaskDelete(NULL);
}

static void web_server_restart_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(WEB_SERVER_RESTART_DELAY_MS));
    ESP_LOGI(TAG, "Riavvio dopo reset configurazione Wi-Fi");
    esp_restart();
}

static esp_err_t web_server_wifi_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= WEB_SERVER_FORM_BUFFER_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body non valido");
        return ESP_ERR_INVALID_ARG;
    }

    char body[WEB_SERVER_FORM_BUFFER_SIZE] = {0};
    int received_total = 0;

    while (received_total < req->content_len) {
        int received = httpd_req_recv(req,
                                      body + received_total,
                                      req->content_len - received_total);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Ricezione fallita");
            return ESP_FAIL;
        }

        received_total += received;
    }
    body[received_total] = '\0';

    char ssid[WEB_SERVER_SSID_BUFFER_SIZE] = {0};
    char password[WEB_SERVER_PASSWORD_BUFFER_SIZE] = {0};

    esp_err_t err = web_server_form_value(body, "ssid", ssid, sizeof(ssid));
    if (err != ESP_OK || ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID mancante");
        return ESP_ERR_INVALID_ARG;
    }

    err = web_server_form_value(body, "password", password, sizeof(password));
    if (err == ESP_ERR_NOT_FOUND) {
        password[0] = '\0';
    } else if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password non valida");
        return err;
    }

    web_server_wifi_request_t *request = calloc(1, sizeof(*request));
    if (request == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memoria insufficiente");
        return ESP_ERR_NO_MEM;
    }

    strlcpy(request->ssid, ssid, sizeof(request->ssid));
    strlcpy(request->password, password, sizeof(request->password));

    BaseType_t task_created = xTaskCreate(web_server_wifi_apply_task,
                                          "wifi_apply",
                                          4096,
                                          request,
                                          tskIDLE_PRIORITY + 2,
                                          NULL);
    if (task_created != pdPASS) {
        free(request);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task Wi-Fi non creata");
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req,
                              "<!doctype html><html lang=\"it\"><head><meta charset=\"utf-8\">"
                              "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                              "<title>AntiFrost Wi-Fi</title></head><body>"
                              "<main><h1>Wi-Fi salvata</h1>"
                              "<p>AntiFrost sta provando a connettersi alla WLAN indicata.</p>"
                              "<p>Controlla il monitor seriale per vedere l'indirizzo IP ottenuto.</p>"
                              "</main></body></html>");
}

static esp_err_t web_server_wifi_reset_post_handler(httpd_req_t *req)
{
    (void)req;

    esp_err_t err = wifi_manager_clear_config();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Reset Wi-Fi fallito");
        return err;
    }

    BaseType_t task_created = xTaskCreate(web_server_restart_task,
                                          "wifi_reset_restart",
                                          3072,
                                          NULL,
                                          tskIDLE_PRIORITY + 2,
                                          NULL);
    if (task_created != pdPASS) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task reboot non creata");
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req,
                              "<!doctype html><html lang=\"it\"><head><meta charset=\"utf-8\">"
                              "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                              "<title>AntiFrost Wi-Fi</title></head><body>"
                              "<main><h1>Wi-Fi cancellata</h1>"
                              "<p>AntiFrost si riavvia e tornera' in modalita' SoftAP.</p>"
                              "<p>Ricollegati alla rete AntiFrost e apri http://192.168.4.1/.</p>"
                              "</main></body></html>");
}

static esp_err_t web_server_wifi_status_get_handler(httpd_req_t *req)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    (void)esp_wifi_get_mode(&mode);

    wifi_config_t sta_cfg = {0};
    wifi_config_t ap_cfg = {0};
    (void)esp_wifi_get_config(WIFI_IF_STA, &sta_cfg);
    (void)esp_wifi_get_config(WIFI_IF_AP, &ap_cfg);

    char sta_ssid[33] = {0};
    char ap_ssid[33] = {0};
    char sta_ip[16] = {0};
    char ap_ip[16] = {0};

    web_server_json_safe_copy(sta_ssid, sizeof(sta_ssid), sta_cfg.sta.ssid);
    web_server_json_safe_copy(ap_ssid, sizeof(ap_ssid), ap_cfg.ap.ssid);

    bool sta_has_ip = web_server_get_ip_for_ifkey("WIFI_STA_DEF", sta_ip, sizeof(sta_ip));
    bool ap_has_ip = web_server_get_ip_for_ifkey("WIFI_AP_DEF", ap_ip, sizeof(ap_ip));

    const char *state = web_server_wifi_state_to_string(wifi_manager_get_state());
    const char *mode_str = web_server_wifi_mode_to_string(mode);

    char response[512] = {0};
    int len = snprintf(
        response,
        sizeof(response),
        "{\"state\":\"%s\",\"mode\":\"%s\",\"sta\":{\"ssid\":\"%s\",\"has_ip\":%s,\"ip\":\"%s\"},\"softap\":{\"ssid\":\"%s\",\"has_ip\":%s,\"ip\":\"%s\"}}",
        state,
        mode_str,
        sta_ssid,
        sta_has_ip ? "true" : "false",
        sta_has_ip ? sta_ip : "",
        ap_ssid,
        ap_has_ip ? "true" : "false",
        ap_has_ip ? ap_ip : "");

    if (len <= 0 || (size_t)len >= sizeof(response)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status troppo lungo");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

static const char *web_server_content_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }

    if (strcmp(ext, ".html") == 0) {
        return "text/html";
    }
    if (strcmp(ext, ".css") == 0) {
        return "text/css";
    }
    if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    }
    if (strcmp(ext, ".json") == 0) {
        return "application/json";
    }
    if (strcmp(ext, ".png") == 0) {
        return "image/png";
    }
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcmp(ext, ".ico") == 0) {
        return "image/x-icon";
    }

    return "application/octet-stream";
}

static esp_err_t web_server_build_file_path(const httpd_req_t *req,
                                            char *path,
                                            size_t path_len)
{
    const char *uri = req->uri;
    size_t uri_len = strcspn(uri, "?");

    if (uri_len == 1 && uri[0] == '/') {
        uri = "/index.html";
        uri_len = strlen(uri);
    } else if (uri_len == strlen("/camera") && strncmp(uri, "/camera", uri_len) == 0) {
        uri = "/camera.html";
        uri_len = strlen(uri);
    } else if (uri_len == strlen("/led") && strncmp(uri, "/led", uri_len) == 0) {
        uri = "/led.html";
        uri_len = strlen(uri);
    } else if (uri_len == strlen("/sensor") && strncmp(uri, "/sensor", uri_len) == 0) {
        uri = "/sensor.html";
        uri_len = strlen(uri);
    } else if (uri_len == strlen("/test") && strncmp(uri, "/test", uri_len) == 0) {
        uri = "/test.html";
        uri_len = strlen(uri);
    }

    if (uri_len == 0 || uri[0] != '/' || strstr(uri, "..") != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(path,
                           path_len,
                           "%s%.*s",
                           WEB_SERVER_MOUNT_POINT,
                           (int)uri_len,
                           uri);
    if (written < 0 || (size_t)written >= path_len) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t web_server_send_file(httpd_req_t *req, const char *path)
{
    int64_t start_ms = web_server_now_ms();
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        if (strcmp(path, WEB_SERVER_MOUNT_POINT "/index.html") == 0) {
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, FALLBACK_INDEX_HTML, HTTPD_RESP_USE_STRLEN);
        }
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File non trovato");
        return ESP_FAIL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Lettura file fallita");
        return ESP_FAIL;
    }

    long file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Dimensione file non valida");
        return ESP_FAIL;
    }

    if (file_size > WEB_SERVER_MAX_STATIC_FILE_SIZE) {
        fclose(file);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File statico troppo grande");
        return ESP_FAIL;
    }

    int64_t stat_done_ms = web_server_now_ms();

    char *buffer = malloc(WEB_SERVER_FILE_BUFFER_SIZE);
    if (buffer == NULL) {
        fclose(file);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memoria file insufficiente");
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, web_server_content_type(path));
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");

    size_t total_sent = 0;
    unsigned int chunks = 0;
    esp_err_t err = ESP_OK;
    int64_t read_total_ms = 0;
    int64_t send_total_ms = 0;
    while (total_sent < (size_t)file_size) {
        int64_t read_start_ms = web_server_now_ms();
        size_t read_len = fread(buffer, 1, WEB_SERVER_FILE_BUFFER_SIZE, file);
        int64_t read_done_ms = web_server_now_ms();
        read_total_ms += read_done_ms - read_start_ms;
        if (read_len == 0) {
            err = ferror(file) ? ESP_FAIL : ESP_OK;
            break;
        }

        int64_t send_start_ms = web_server_now_ms();
        err = httpd_resp_send_chunk(req, buffer, read_len);
        int64_t send_done_ms = web_server_now_ms();
        send_total_ms += send_done_ms - send_start_ms;
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Invio file %s fallito dopo %u/%ld bytes: %s timing open/stat=%lldms read=%lldms send=%lldms total=%lldms chunks=%u bytes=%u",
                     path,
                     (unsigned int)total_sent,
                     file_size,
                     esp_err_to_name(err),
                     (long long)(stat_done_ms - start_ms),
                     (long long)read_total_ms,
                     (long long)send_total_ms,
                     (long long)(send_done_ms - start_ms),
                     chunks,
                     (unsigned int)total_sent);
            break;
        }

        total_sent += read_len;
        chunks++;
        if (send_total_ms > WEB_SERVER_SEND_BUDGET_MS) {
            ESP_LOGW(TAG,
                     "Invio file %s interrotto per budget send dopo %u/%ld bytes timing open/stat=%lldms read=%lldms send=%lldms total=%lldms chunks=%u bytes=%u",
                     path,
                     (unsigned int)total_sent,
                     file_size,
                     (long long)(stat_done_ms - start_ms),
                     (long long)read_total_ms,
                     (long long)send_total_ms,
                     (long long)(send_done_ms - start_ms),
                     chunks,
                     (unsigned int)total_sent);
            err = ESP_ERR_TIMEOUT;
            break;
        }
    }

    fclose(file);
    free(buffer);
    if (err != ESP_OK) {
        return err;
    }
    if (total_sent != (size_t)file_size) {
        httpd_resp_sendstr_chunk(req, NULL);
        ESP_LOGW(TAG, "Lettura file %s incompleta: %u/%ld bytes",
                 path, (unsigned int)total_sent, file_size);
        return ESP_FAIL;
    }

    err = httpd_resp_sendstr_chunk(req, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Chiusura chunk file %s fallita: %s", path, esp_err_to_name(err));
        return err;
    }

    int64_t done_ms = web_server_now_ms();
    ESP_LOGI(TAG,
             "HTTP sent %s (%ld bytes) mode=chunked timing open/stat=%lldms read=%lldms send=%lldms total=%lldms chunks=%u bytes=%u",
             path,
             file_size,
             (long long)(stat_done_ms - start_ms),
             (long long)read_total_ms,
             (long long)send_total_ms,
             (long long)(done_ms - start_ms),
             chunks,
             (unsigned int)total_sent);
    return ESP_OK;
}

static esp_err_t web_server_static_handler(httpd_req_t *req)
{
    char path[WEB_SERVER_MAX_PATH_LEN] = {0};
    esp_err_t err = web_server_build_file_path(req, path, sizeof(path));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Percorso non valido");
        return err;
    }

    ESP_LOGI(TAG, "HTTP static %s -> %s", req->uri, path);
    return web_server_send_file(req, path);
}

static esp_err_t web_server_mount_fatfs(void)
{
    if (s_fs_mounted) {
        return ESP_OK;
    }

    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = true,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(WEB_SERVER_MOUNT_POINT,
                                                      WEB_SERVER_PARTITION_LABEL,
                                                      &mount_config,
                                                      &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mount FATFS interna fallito: %s", esp_err_to_name(err));
        return err;
    }

    s_fs_mounted = true;
    ESP_LOGI(TAG, "FATFS interna montata su %s", WEB_SERVER_MOUNT_POINT);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    esp_err_t err = web_server_mount_fatfs();
    if (err != ESP_OK) {
        return err;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = WEB_SERVER_STACK_SIZE;
    config.ctrl_port = WEB_SERVER_CTRL_PORT;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = WEB_SERVER_MAX_URI_HANDLERS;
    config.max_open_sockets = WEB_SERVER_MAX_OPEN_SOCKETS;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = WEB_SERVER_RECV_WAIT_TIMEOUT_SEC;
    config.send_wait_timeout = WEB_SERVER_SEND_WAIT_TIMEOUT_SEC;

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Avvio HTTP server fallito: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(camera_manager_init());

    const httpd_uri_t static_files = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = web_server_static_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t wifi_post = {
        .uri = "/api/wifi",
        .method = HTTP_POST,
        .handler = web_server_wifi_post_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t wifi_reset_post = {
        .uri = "/api/wifi/reset",
        .method = HTTP_POST,
        .handler = web_server_wifi_reset_post_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t wifi_status_get = {
        .uri = "/api/wifi/status",
        .method = HTTP_GET,
        .handler = web_server_wifi_status_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t system_status_get = {
        .uri = "/api/system/status",
        .method = HTTP_GET,
        .handler = web_server_system_status_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t sensor_status_get = {
        .uri = "/api/sensor/status",
        .method = HTTP_GET,
        .handler = web_server_sensor_status_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t camera_params_get = {
        .uri = "/api/camera/parameters",
        .method = HTTP_GET,
        .handler = web_server_camera_params_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t camera_control_get = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = web_server_camera_control_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t camera_status_get = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = web_server_camera_status_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t camera_stream_status_get = {
        .uri = "/api/camera/stream/status",
        .method = HTTP_GET,
        .handler = web_server_camera_stream_status_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t camera_capture_get = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = web_server_camera_capture_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t camera_capture_alias_get = {
        .uri = "/camera/capture",
        .method = HTTP_GET,
        .handler = web_server_camera_capture_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t led_status_get = {
        .uri = "/api/led/status",
        .method = HTTP_GET,
        .handler = web_server_led_status_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t led_control_get = {
        .uri = "/api/led/control",
        .method = HTTP_GET,
        .handler = web_server_led_control_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t fan_status_get = {
        .uri = "/api/fan/status",
        .method = HTTP_GET,
        .handler = web_server_fan_status_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t fan_control_get = {
        .uri = "/api/fan/control",
        .method = HTTP_GET,
        .handler = web_server_fan_control_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t test_status_get = {
        .uri = "/api/test/status",
        .method = HTTP_GET,
        .handler = web_server_test_status_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t test_gpio_control_get = {
        .uri = "/api/test/gpio/control",
        .method = HTTP_GET,
        .handler = web_server_test_gpio_control_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t camera_defaults_post = {
        .uri = "/api/camera/defaults",
        .method = HTTP_POST,
        .handler = web_server_camera_defaults_post_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t camera_profile_post = {
        .uri = "/api/camera/profile",
        .method = HTTP_POST,
        .handler = web_server_camera_profile_post_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t camera_reference_post = {
        .uri = "/api/camera/reference",
        .method = HTTP_POST,
        .handler = web_server_camera_reference_post_handler,
        .user_ctx = NULL,
    };

    err = httpd_register_uri_handler(s_server, &wifi_post);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler Wi-Fi fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &wifi_reset_post);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler reset Wi-Fi fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &wifi_status_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler stato Wi-Fi fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &system_status_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler stato sistema fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &sensor_status_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler stato sensore fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &camera_params_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler parametri camera fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &camera_control_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler control camera fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &camera_status_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler status camera fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &camera_stream_status_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler stato stream camera fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &camera_capture_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler capture camera fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &camera_capture_alias_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler alias capture camera fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &led_status_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler stato LED fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &led_control_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler controllo LED fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &fan_status_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler stato ventola fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &fan_control_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler controllo ventola fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &test_status_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler stato test fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &test_gpio_control_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler controllo GPIO test fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &camera_defaults_post);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler default camera fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &camera_profile_post);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler profilo camera fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &camera_reference_post);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler riferimento camera fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &static_files);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler statico fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG,
             "HTTP server avviato su porta %d ctrl_port=%d stack=%u sockets=%d timeout send/recv=%ds/%ds",
             config.server_port,
             WEB_SERVER_CTRL_PORT,
             (unsigned int)WEB_SERVER_STACK_SIZE,
             WEB_SERVER_MAX_OPEN_SOCKETS,
             WEB_SERVER_SEND_WAIT_TIMEOUT_SEC,
             WEB_SERVER_RECV_WAIT_TIMEOUT_SEC);
    return ESP_OK;
}

bool web_server_is_running(void)
{
    return s_server != NULL;
}

const char *web_server_mount_point(void)
{
    return WEB_SERVER_MOUNT_POINT;
}

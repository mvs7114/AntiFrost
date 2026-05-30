#include "web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wear_levelling.h"
#include "wifi_manager.h"

static const char *TAG = "web_server";

#define WEB_SERVER_MOUNT_POINT "/www"
#define WEB_SERVER_PARTITION_LABEL "storage"
#define WEB_SERVER_MAX_PATH_LEN 160
#define WEB_SERVER_FILE_BUFFER_SIZE 1024
#define WEB_SERVER_FORM_BUFFER_SIZE 256
#define WEB_SERVER_SSID_BUFFER_SIZE 33
#define WEB_SERVER_PASSWORD_BUFFER_SIZE 65
#define WEB_SERVER_WIFI_APPLY_DELAY_MS 1500
#define WEB_SERVER_RESTART_DELAY_MS 1500

typedef struct {
    char ssid[WEB_SERVER_SSID_BUFFER_SIZE];
    char password[WEB_SERVER_PASSWORD_BUFFER_SIZE];
} web_server_wifi_request_t;

static httpd_handle_t s_server;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static bool s_fs_mounted;

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
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        if (strcmp(path, WEB_SERVER_MOUNT_POINT "/index.html") == 0) {
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, FALLBACK_INDEX_HTML, HTTPD_RESP_USE_STRLEN);
        }

        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File non trovato");
        return ESP_FAIL;
    }

    char buffer[WEB_SERVER_FILE_BUFFER_SIZE];
    httpd_resp_set_type(req, web_server_content_type(path));

    while (!feof(file)) {
        size_t read_len = fread(buffer, 1, sizeof(buffer), file);
        if (read_len > 0) {
            esp_err_t err = httpd_resp_send_chunk(req, buffer, read_len);
            if (err != ESP_OK) {
                fclose(file);
                httpd_resp_sendstr_chunk(req, NULL);
                return err;
            }
        }
    }

    fclose(file);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t web_server_static_handler(httpd_req_t *req)
{
    char path[WEB_SERVER_MAX_PATH_LEN] = {0};
    esp_err_t err = web_server_build_file_path(req, path, sizeof(path));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Percorso non valido");
        return err;
    }

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
    config.uri_match_fn = httpd_uri_match_wildcard;

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Avvio HTTP server fallito: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

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

    err = httpd_register_uri_handler(s_server, &static_files);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler statico fallita: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "HTTP server avviato");
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

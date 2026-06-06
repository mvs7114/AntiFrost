#include "camera_manager.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "camera_manager";

#define CAMERA_MANAGER_CONFIG_PATH "/www/camera_params.cfg"
#define CAMERA_MANAGER_LINE_BUFFER_SIZE 96
#define CAMERA_MANAGER_STATUS_BUFFER_SIZE 1024
#define CAMERA_MANAGER_STREAM_PORT 81
#define CAMERA_MANAGER_STREAM_CTRL_PORT 32769
#define CAMERA_MANAGER_STREAM_STACK_SIZE 8192
#define CAMERA_MANAGER_XCLK_FREQ_HZ 10000000
#define CAMERA_MANAGER_FRAMEBUFFER_COUNT 1
#define CAMERA_MANAGER_STREAM_MAX_OPEN_SOCKETS 2
#define CAMERA_MANAGER_STREAM_SEND_WAIT_TIMEOUT_SEC 15
#define CAMERA_MANAGER_STREAM_RECV_WAIT_TIMEOUT_SEC 5
#define CAMERA_MANAGER_STREAM_FRAME_INTERVAL_MS 160
#define CAMERA_MANAGER_STREAM_PAYLOAD_CHUNK_SIZE 2048
#define CAMERA_MANAGER_WARMUP_FRAME_COUNT 10
#define CAMERA_MANAGER_WARMUP_DELAY_MS 200
#define CAMERA_MANAGER_MUTEX_TIMEOUT_MS 50

static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char *STREAM_PART_HEADER = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static httpd_handle_t s_stream_server;
static bool s_initialized;
static bool s_camera_ready;
static volatile bool s_stream_active;
static SemaphoreHandle_t s_camera_mutex;

static const camera_manager_param_info_t s_params[] = {
    {.name = "framesize", .label = "Formato frame", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = FRAMESIZE_QXGA, .step = 1, .default_value = FRAMESIZE_QVGA, .help = "Cambia dimensione frame e memoria richiesta. OV3660 supporta fino a QXGA, ma i formati alti richiedono piu PSRAM e banda."},
    {.name = "quality", .label = "Qualita JPEG", .risk = CAMERA_MANAGER_PARAM_SAFE, .min_value = 4, .max_value = 63, .step = 1, .default_value = 12, .help = "Regola compressione JPEG."},
    {.name = "brightness", .label = "Luminosita", .risk = CAMERA_MANAGER_PARAM_SAFE, .min_value = -2, .max_value = 2, .step = 1, .default_value = 1, .help = "Regola la luminosita dell'immagine."},
    {.name = "contrast", .label = "Contrasto", .risk = CAMERA_MANAGER_PARAM_SAFE, .min_value = -2, .max_value = 2, .step = 1, .default_value = 0, .help = "Regola il contrasto."},
    {.name = "saturation", .label = "Saturazione", .risk = CAMERA_MANAGER_PARAM_SAFE, .min_value = -2, .max_value = 2, .step = 1, .default_value = 0, .help = "Regola l'intensita dei colori."},
    {.name = "sharpness", .label = "Nitidezza", .risk = CAMERA_MANAGER_PARAM_SAFE, .min_value = -3, .max_value = 3, .step = 1, .default_value = 0, .help = "Regola l'enfasi dei bordi."},
    {.name = "gainceiling", .label = "Gain ceiling", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = 6, .step = 1, .default_value = 3, .help = "Limita il guadagno automatico massimo."},
    {.name = "colorbar", .label = "Barre colore test", .risk = CAMERA_MANAGER_PARAM_DANGEROUS, .min_value = 0, .max_value = 1, .step = 1, .default_value = 0, .help = "Sostituisce l'immagine reale con un pattern di test."},
    {.name = "awb", .label = "Auto white balance", .risk = CAMERA_MANAGER_PARAM_SAFE, .min_value = 0, .max_value = 1, .step = 1, .default_value = 1, .help = "Abilita il bilanciamento automatico del bianco."},
    {.name = "agc", .label = "Auto gain", .risk = CAMERA_MANAGER_PARAM_DANGEROUS, .min_value = 0, .max_value = 1, .step = 1, .default_value = 1, .help = "Disabilitarlo puo produrre frame troppo scuri o saturi."},
    {.name = "aec", .label = "Auto esposizione", .risk = CAMERA_MANAGER_PARAM_DANGEROUS, .min_value = 0, .max_value = 1, .step = 1, .default_value = 1, .help = "Disabilitarla puo produrre immagini nere o sovraesposte."},
    {.name = "hmirror", .label = "Flip orizzontale", .risk = CAMERA_MANAGER_PARAM_SAFE, .min_value = 0, .max_value = 1, .step = 1, .default_value = 0, .help = "Inverte l'immagine sull'asse orizzontale."},
    {.name = "vflip", .label = "Flip verticale", .risk = CAMERA_MANAGER_PARAM_SAFE, .min_value = 0, .max_value = 1, .step = 1, .default_value = 0, .help = "Inverte l'immagine sull'asse verticale."},
    {.name = "awb_gain", .label = "AWB gain", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = 1, .step = 1, .default_value = 1, .help = "Regola il guadagno del bilanciamento del bianco."},
    {.name = "agc_gain", .label = "Gain manuale", .risk = CAMERA_MANAGER_PARAM_DANGEROUS, .min_value = 0, .max_value = 30, .step = 1, .default_value = 0, .help = "Valori elevati aumentano rumore e instabilita dell'immagine."},
    {.name = "aec_value", .label = "Valore esposizione", .risk = CAMERA_MANAGER_PARAM_DANGEROUS, .min_value = 0, .max_value = 1200, .step = 1, .default_value = 300, .help = "Valori estremi possono bloccare l'immagine."},
    {.name = "aec2", .label = "AEC DSP", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = 1, .step = 1, .default_value = 0, .help = "Modifica la gestione DSP dell'esposizione automatica."},
    {.name = "dcw", .label = "Downsize CW", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = 1, .step = 1, .default_value = 1, .help = "Influenza il ridimensionamento interno del sensore."},
    {.name = "bpc", .label = "Black pixel correction", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = 1, .step = 1, .default_value = 0, .help = "Corregge pixel neri difettosi."},
    {.name = "wpc", .label = "White pixel correction", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = 1, .step = 1, .default_value = 1, .help = "Corregge pixel bianchi difettosi."},
    {.name = "raw_gma", .label = "Gamma RAW", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = 1, .step = 1, .default_value = 1, .help = "Applica la correzione gamma interna."},
    {.name = "lenc", .label = "Lens correction", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = 1, .step = 1, .default_value = 1, .help = "Applica la correzione lente del sensore."},
    {.name = "special_effect", .label = "Effetto speciale", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = 6, .step = 1, .default_value = 0, .help = "Modifica il rendering colore del sensore."},
    {.name = "wb_mode", .label = "Bilanciamento bianco", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = 0, .max_value = 4, .step = 1, .default_value = 0, .help = "Cambia la resa cromatica."},
    {.name = "ae_level", .label = "Livello AE", .risk = CAMERA_MANAGER_PARAM_ADVANCED, .min_value = -2, .max_value = 2, .step = 1, .default_value = 1, .help = "Compensa il livello dell'esposizione automatica."},
};

static int s_values[sizeof(s_params) / sizeof(s_params[0])];

static size_t camera_manager_param_count(void)
{
    return sizeof(s_params) / sizeof(s_params[0]);
}

static esp_err_t camera_manager_find_index(const char *name, size_t *index)
{
    if (name == NULL || index == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < camera_manager_param_count(); i++) {
        if (strcmp(s_params[i].name, name) == 0) {
            *index = i;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

static bool camera_manager_value_in_range(const camera_manager_param_info_t *param, int value)
{
    return value >= param->min_value && value <= param->max_value;
}

static void camera_manager_load_defaults(void)
{
    for (size_t i = 0; i < camera_manager_param_count(); i++) {
        s_values[i] = s_params[i].default_value;
    }
}

static esp_err_t camera_manager_save_config(void)
{
    FILE *file = fopen(CAMERA_MANAGER_CONFIG_PATH, "w");
    if (file == NULL) {
        ESP_LOGW(TAG, "Salvataggio parametri camera su FAT fallito");
        return ESP_FAIL;
    }

    for (size_t i = 0; i < camera_manager_param_count(); i++) {
        if (fprintf(file, "%s=%d\n", s_params[i].name, s_values[i]) < 0) {
            fclose(file);
            return ESP_FAIL;
        }
    }

    return fclose(file) == 0 ? ESP_OK : ESP_FAIL;
}

static void camera_manager_trim_line(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' ' || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }
}

static esp_err_t camera_manager_load_config(void)
{
    FILE *file = fopen(CAMERA_MANAGER_CONFIG_PATH, "r");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char line[CAMERA_MANAGER_LINE_BUFFER_SIZE] = {0};
    while (fgets(line, sizeof(line), file) != NULL) {
        camera_manager_trim_line(line);
        char *separator = strchr(line, '=');
        if (separator == NULL || separator == line || separator[1] == '\0') {
            continue;
        }

        *separator = '\0';
        char *end = NULL;
        long parsed_value = strtol(separator + 1, &end, 10);
        if (end == separator + 1 || *end != '\0') {
            continue;
        }

        size_t index = 0;
        if (camera_manager_find_index(line, &index) != ESP_OK) {
            continue;
        }

        if (s_params[index].risk == CAMERA_MANAGER_PARAM_DANGEROUS) {
            ESP_LOGW(TAG, "Parametro rischioso %s ignorato al boot", s_params[index].name);
            continue;
        }

        int value = (int)parsed_value;
        if (camera_manager_value_in_range(&s_params[index], value)) {
            s_values[index] = value;
        }
    }

    fclose(file);
    return ESP_OK;
}

static sensor_t *camera_manager_sensor(void)
{
    if (!s_camera_ready) {
        return NULL;
    }

    return esp_camera_sensor_get();
}

static esp_err_t camera_manager_apply_sensor_value(const char *name, int value)
{
    sensor_t *sensor = camera_manager_sensor();
    if (sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (strcmp(name, "framesize") == 0) {
        return sensor->set_framesize(sensor, (framesize_t)value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "quality") == 0) {
        return sensor->set_quality(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "brightness") == 0) {
        return sensor->set_brightness(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "contrast") == 0) {
        return sensor->set_contrast(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "saturation") == 0) {
        return sensor->set_saturation(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "sharpness") == 0) {
        return sensor->set_sharpness(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "gainceiling") == 0) {
        return sensor->set_gainceiling(sensor, (gainceiling_t)value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "colorbar") == 0) {
        return sensor->set_colorbar(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "awb") == 0) {
        return sensor->set_whitebal(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "agc") == 0) {
        return sensor->set_gain_ctrl(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "aec") == 0) {
        return sensor->set_exposure_ctrl(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "hmirror") == 0) {
        return sensor->set_hmirror(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "vflip") == 0) {
        return sensor->set_vflip(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "awb_gain") == 0) {
        return sensor->set_awb_gain(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "agc_gain") == 0) {
        return sensor->set_agc_gain(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "aec_value") == 0) {
        return sensor->set_aec_value(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "aec2") == 0) {
        return sensor->set_aec2(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "dcw") == 0) {
        return sensor->set_dcw(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "bpc") == 0) {
        return sensor->set_bpc(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "wpc") == 0) {
        return sensor->set_wpc(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "raw_gma") == 0) {
        return sensor->set_raw_gma(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "lenc") == 0) {
        return sensor->set_lenc(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "special_effect") == 0) {
        return sensor->set_special_effect(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "wb_mode") == 0) {
        return sensor->set_wb_mode(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (strcmp(name, "ae_level") == 0) {
        return sensor->set_ae_level(sensor, value) == 0 ? ESP_OK : ESP_FAIL;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static void camera_manager_apply_loaded_config(void)
{
    for (size_t i = 0; i < camera_manager_param_count(); i++) {
        esp_err_t err = camera_manager_apply_sensor_value(s_params[i].name, s_values[i]);
        if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Applicazione parametro camera %s fallita: %s",
                     s_params[i].name, esp_err_to_name(err));
        }
    }
}

static bool camera_is_valid_jpeg(const camera_fb_t *fb)
{
    return fb != NULL &&
           fb->buf != NULL &&
           fb->len >= 4 &&
           fb->buf[0] == 0xFF &&
           fb->buf[1] == 0xD8 &&
           fb->buf[fb->len - 2] == 0xFF &&
           fb->buf[fb->len - 1] == 0xD9;
}

static void camera_manager_log_frame_bytes(const char *context, const camera_fb_t *fb)
{
    if (fb == NULL || fb->buf == NULL || fb->len < 2) {
        ESP_LOGW(TAG, "%s: frame non valido o troppo corto", context);
        return;
    }

    ESP_LOGI(TAG,
             "%s: len=%u format=%d first=%02X %02X last=%02X %02X valid_jpeg=%s",
             context,
             (unsigned int)fb->len,
             fb->format,
             fb->buf[0],
             fb->buf[1],
             fb->buf[fb->len - 2],
             fb->buf[fb->len - 1],
             camera_is_valid_jpeg(fb) ? "true" : "false");
}

static bool camera_manager_take_mutex(TickType_t timeout_ticks)
{
    return s_camera_mutex != NULL &&
           xSemaphoreTake(s_camera_mutex, timeout_ticks) == pdTRUE;
}

static void camera_manager_give_mutex(void)
{
    if (s_camera_mutex != NULL) {
        xSemaphoreGive(s_camera_mutex);
    }
}

static esp_err_t camera_manager_send_busy(httpd_req_t *req)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "camera busy");
}

static void camera_manager_warmup(void)
{
    for (int i = 0; i < CAMERA_MANAGER_WARMUP_FRAME_COUNT; i++) {
        if (!camera_manager_take_mutex(pdMS_TO_TICKS(CAMERA_MANAGER_MUTEX_TIMEOUT_MS))) {
            ESP_LOGW(TAG, "Warmup frame %d: camera occupata", i + 1);
            vTaskDelay(pdMS_TO_TICKS(CAMERA_MANAGER_WARMUP_DELAY_MS));
            continue;
        }

        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            ESP_LOGW(TAG, "Warmup frame %d: timeout frame", i + 1);
            camera_manager_give_mutex();
            vTaskDelay(pdMS_TO_TICKS(CAMERA_MANAGER_WARMUP_DELAY_MS));
            continue;
        }

        char context[32] = {0};
        snprintf(context, sizeof(context), "Warmup frame %d", i + 1);
        camera_manager_log_frame_bytes(context, fb);
        esp_camera_fb_return(fb);
        camera_manager_give_mutex();
        vTaskDelay(pdMS_TO_TICKS(CAMERA_MANAGER_WARMUP_DELAY_MS));
    }
}

static esp_err_t camera_manager_send_frame_chunked(httpd_req_t *req, const uint8_t *frame, size_t frame_len)
{
    esp_err_t err = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (err != ESP_OK) {
        return err;
    }

    char header[64] = {0};
    int header_len = snprintf(header, sizeof(header), STREAM_PART_HEADER, (unsigned int)frame_len);
    if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
        return ESP_FAIL;
    }

    err = httpd_resp_send_chunk(req, header, header_len);
    if (err != ESP_OK) {
        return err;
    }

    size_t offset = 0;
    while (offset < frame_len) {
        size_t remaining = frame_len - offset;
        size_t chunk_len = remaining > CAMERA_MANAGER_STREAM_PAYLOAD_CHUNK_SIZE
                               ? CAMERA_MANAGER_STREAM_PAYLOAD_CHUNK_SIZE
                               : remaining;

        err = httpd_resp_send_chunk(req, (const char *)(frame + offset), chunk_len);
        if (err != ESP_OK) {
            return err;
        }

        offset += chunk_len;
        taskYIELD();
    }

    return ESP_OK;
}

static esp_err_t camera_manager_stream_handler(httpd_req_t *req)
{
    if (!s_camera_ready) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera non inizializzata");
        return ESP_FAIL;
    }
    if (s_stream_active) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "Multi-stream non supportato");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (err != ESP_OK) {
        return err;
    }

    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Framerate", "6");
    s_stream_active = true;
    ESP_LOGI(TAG, "Client stream connesso");

    while (true) {
        uint8_t *frame_copy = NULL;
        size_t frame_len = 0;

        if (!camera_manager_take_mutex(pdMS_TO_TICKS(CAMERA_MANAGER_MUTEX_TIMEOUT_MS))) {
            ESP_LOGW(TAG, "Frame stream saltato: camera occupata");
            s_stream_active = false;
            return ESP_ERR_TIMEOUT;
        }

        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            ESP_LOGW(TAG, "Frame stream non disponibile");
            camera_manager_give_mutex();
            s_stream_active = false;
            return ESP_FAIL;
        }

        if (fb->format != PIXFORMAT_JPEG || !camera_is_valid_jpeg(fb)) {
            camera_manager_log_frame_bytes("Frame stream JPEG non valido", fb);
            esp_camera_fb_return(fb);
            camera_manager_give_mutex();
            s_stream_active = false;
            return ESP_ERR_NOT_SUPPORTED;
        }

        frame_copy = malloc(fb->len);
        if (frame_copy == NULL) {
            ESP_LOGW(TAG, "Memoria insufficiente per frame stream (%u bytes)", (unsigned int)fb->len);
            esp_camera_fb_return(fb);
            camera_manager_give_mutex();
            s_stream_active = false;
            return ESP_ERR_NO_MEM;
        }

        frame_len = fb->len;
        memcpy(frame_copy, fb->buf, frame_len);
        esp_camera_fb_return(fb);
        camera_manager_give_mutex();

        err = camera_manager_send_frame_chunked(req, frame_copy, frame_len);
        free(frame_copy);

        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Client stream disconnesso: %s", esp_err_to_name(err));
            s_stream_active = false;
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(CAMERA_MANAGER_STREAM_FRAME_INTERVAL_MS));
    }
}

static esp_err_t camera_manager_start_stream_server(void)
{
    if (s_stream_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = CAMERA_MANAGER_STREAM_STACK_SIZE;
    config.server_port = CAMERA_MANAGER_STREAM_PORT;
    config.ctrl_port = CAMERA_MANAGER_STREAM_CTRL_PORT;
    config.max_open_sockets = CAMERA_MANAGER_STREAM_MAX_OPEN_SOCKETS;
    config.lru_purge_enable = true;
    config.send_wait_timeout = CAMERA_MANAGER_STREAM_SEND_WAIT_TIMEOUT_SEC;
    config.recv_wait_timeout = CAMERA_MANAGER_STREAM_RECV_WAIT_TIMEOUT_SEC;

    esp_err_t err = httpd_start(&s_stream_server, &config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Avvio stream server camera fallito: %s", esp_err_to_name(err));
        s_stream_server = NULL;
        return err;
    }

    const httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = camera_manager_stream_handler,
        .user_ctx = NULL,
    };

    err = httpd_register_uri_handler(s_stream_server, &stream_uri);
    if (err != ESP_OK) {
        httpd_stop(s_stream_server);
        s_stream_server = NULL;
        return err;
    }

    ESP_LOGI(TAG,
             "Stream camera attivo su porta %d ctrl_port=%d stack=%u sockets=%d timeout send/recv=%ds/%ds",
             CAMERA_MANAGER_STREAM_PORT,
             CAMERA_MANAGER_STREAM_CTRL_PORT,
             (unsigned int)CAMERA_MANAGER_STREAM_STACK_SIZE,
             CAMERA_MANAGER_STREAM_MAX_OPEN_SOCKETS,
             CAMERA_MANAGER_STREAM_SEND_WAIT_TIMEOUT_SEC,
             CAMERA_MANAGER_STREAM_RECV_WAIT_TIMEOUT_SEC);
    return ESP_OK;
}

static esp_err_t camera_manager_init_sensor(void)
{
    camera_config_t config = {
        .pin_pwdn = BOARD_CAM_PWDN_GPIO,
        .pin_reset = BOARD_CAM_RESET_GPIO,
        .pin_xclk = BOARD_CAM_XCLK_GPIO,
        .pin_sccb_sda = BOARD_CAM_SIOD_GPIO,
        .pin_sccb_scl = BOARD_CAM_SIOC_GPIO,
        .pin_d7 = BOARD_CAM_Y9_GPIO,
        .pin_d6 = BOARD_CAM_Y8_GPIO,
        .pin_d5 = BOARD_CAM_Y7_GPIO,
        .pin_d4 = BOARD_CAM_Y6_GPIO,
        .pin_d3 = BOARD_CAM_Y5_GPIO,
        .pin_d2 = BOARD_CAM_Y4_GPIO,
        .pin_d1 = BOARD_CAM_Y3_GPIO,
        .pin_d0 = BOARD_CAM_Y2_GPIO,
        .pin_vsync = BOARD_CAM_VSYNC_GPIO,
        .pin_href = BOARD_CAM_HREF_GPIO,
        .pin_pclk = BOARD_CAM_PCLK_GPIO,
        .xclk_freq_hz = CAMERA_MANAGER_XCLK_FREQ_HZ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = CAMERA_MANAGER_FRAMEBUFFER_COUNT,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Inizializzazione camera fallita: %s", esp_err_to_name(err));
        return err;
    }

    s_camera_ready = true;
    camera_manager_warmup();
    ESP_ERROR_CHECK_WITHOUT_ABORT(camera_manager_apply_sensor_value("colorbar", 0));
    ESP_ERROR_CHECK_WITHOUT_ABORT(camera_manager_apply_sensor_value("awb", 1));
    ESP_ERROR_CHECK_WITHOUT_ABORT(camera_manager_apply_sensor_value("agc", 1));
    ESP_ERROR_CHECK_WITHOUT_ABORT(camera_manager_apply_sensor_value("aec", 1));
    camera_manager_apply_loaded_config();
    ESP_LOGI(TAG, "Camera inizializzata");
    return ESP_OK;
}

esp_err_t camera_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    camera_manager_load_defaults();
    (void)camera_manager_load_config();

    if (s_camera_mutex == NULL) {
        s_camera_mutex = xSemaphoreCreateMutex();
        if (s_camera_mutex == NULL) {
            ESP_LOGE(TAG, "Creazione mutex camera fallita");
            return ESP_ERR_NO_MEM;
        }
    }

    s_initialized = true;

    esp_err_t camera_err = camera_manager_init_sensor();
    if (camera_err == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(camera_manager_start_stream_server());
    }

    return camera_err;
}

const camera_manager_param_info_t *camera_manager_get_params(size_t *count)
{
    if (count != NULL) {
        *count = camera_manager_param_count();
    }

    return s_params;
}

const camera_manager_param_info_t *camera_manager_find_param(const char *name)
{
    size_t index = 0;
    if (camera_manager_find_index(name, &index) == ESP_OK) {
        return &s_params[index];
    }

    return NULL;
}

esp_err_t camera_manager_get_value(const char *name, int *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t index = 0;
    esp_err_t err = camera_manager_find_index(name, &index);
    if (err != ESP_OK) {
        return err;
    }

    *value = s_values[index];
    return ESP_OK;
}

esp_err_t camera_manager_set_value(const char *name, int value, bool dangerous_confirmed)
{
    size_t index = 0;
    esp_err_t err = camera_manager_find_index(name, &index);
    if (err != ESP_OK) {
        return err;
    }

    const camera_manager_param_info_t *param = &s_params[index];
    if (!camera_manager_value_in_range(param, value)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (param->risk == CAMERA_MANAGER_PARAM_DANGEROUS && !dangerous_confirmed) {
        return ESP_ERR_INVALID_STATE;
    }

    err = camera_manager_apply_sensor_value(name, value);
    if (err != ESP_OK) {
        return err;
    }

    s_values[index] = value;
    ESP_ERROR_CHECK_WITHOUT_ABORT(camera_manager_save_config());
    return ESP_OK;
}

esp_err_t camera_manager_restore_defaults(void)
{
    camera_manager_load_defaults();
    for (size_t i = 0; i < camera_manager_param_count(); i++) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(camera_manager_apply_sensor_value(s_params[i].name, s_values[i]));
    }

    return camera_manager_save_config();
}

bool camera_manager_is_stream_active(void)
{
    return s_stream_active;
}

esp_err_t camera_manager_send_status(httpd_req_t *req)
{
    sensor_t *sensor = camera_manager_sensor();
    if (sensor == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera non inizializzata");
        return ESP_FAIL;
    }

    camera_status_t *status = &sensor->status;
    char response[CAMERA_MANAGER_STATUS_BUFFER_SIZE] = {0};
    int len = snprintf(
        response,
        sizeof(response),
        "{\"framesize\":%d,\"quality\":%d,\"brightness\":%d,\"contrast\":%d,\"saturation\":%d,"
        "\"sharpness\":%d,\"special_effect\":%d,\"wb_mode\":%d,\"awb\":%d,\"awb_gain\":%d,"
        "\"aec\":%d,\"aec2\":%d,\"ae_level\":%d,\"aec_value\":%d,\"agc\":%d,\"agc_gain\":%d,"
        "\"gainceiling\":%d,\"bpc\":%d,\"wpc\":%d,\"raw_gma\":%d,\"lenc\":%d,\"hmirror\":%d,"
        "\"vflip\":%d,\"dcw\":%d,\"colorbar\":%d}",
        status->framesize,
        status->quality,
        status->brightness,
        status->contrast,
        status->saturation,
        status->sharpness,
        status->special_effect,
        status->wb_mode,
        status->awb,
        status->awb_gain,
        status->aec,
        status->aec2,
        status->ae_level,
        status->aec_value,
        status->agc,
        status->agc_gain,
        status->gainceiling,
        status->bpc,
        status->wpc,
        status->raw_gma,
        status->lenc,
        status->hmirror,
        status->vflip,
        status->dcw,
        status->colorbar);

    if (len <= 0 || (size_t)len >= sizeof(response)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status camera troppo lungo");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}

esp_err_t camera_manager_send_capture(httpd_req_t *req)
{
    if (!s_camera_ready) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera non inizializzata");
        return ESP_FAIL;
    }
    if (s_stream_active) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Stream camera attivo");
        return ESP_ERR_INVALID_STATE;
    }

    if (!camera_manager_take_mutex(pdMS_TO_TICKS(CAMERA_MANAGER_MUTEX_TIMEOUT_MS))) {
        return camera_manager_send_busy(req);
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        camera_manager_give_mutex();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera frame timeout");
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    uint8_t *frame_copy = NULL;
    size_t frame_len = 0;
    if (fb->format == PIXFORMAT_JPEG && camera_is_valid_jpeg(fb)) {
        frame_copy = malloc(fb->len);
        if (frame_copy == NULL) {
            ESP_LOGW(TAG, "Memoria insufficiente per capture (%u bytes)", (unsigned int)fb->len);
            err = ESP_ERR_NO_MEM;
        } else {
            frame_len = fb->len;
            memcpy(frame_copy, fb->buf, frame_len);
        }
    } else {
        camera_manager_log_frame_bytes("Capture JPEG non valida", fb);
        err = ESP_FAIL;
    }

    ESP_LOGI(TAG, "Capture %u bytes", (unsigned int)fb->len);
    esp_camera_fb_return(fb);
    camera_manager_give_mutex();

    if (err == ESP_ERR_NO_MEM) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memoria capture insufficiente");
        return err;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JPEG capture non valido");
        return err;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    err = httpd_resp_send(req, (const char *)frame_copy, frame_len);
    free(frame_copy);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Invio capture fallito: %s", esp_err_to_name(err));
    }

    return err;
}

const char *camera_manager_risk_to_string(camera_manager_param_risk_t risk)
{
    switch (risk) {
    case CAMERA_MANAGER_PARAM_SAFE:
        return "SAFE";
    case CAMERA_MANAGER_PARAM_ADVANCED:
        return "ADVANCED";
    case CAMERA_MANAGER_PARAM_DANGEROUS:
        return "DANGEROUS";
    default:
        return "UNKNOWN";
    }
}

#include "wifi_manager.h"

#include <stdbool.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_manager";

#define WIFI_MANAGER_MAX_STA_RETRIES 5
#define WIFI_MANAGER_SOFTAP_CHANNEL 1
#define WIFI_MANAGER_SOFTAP_MAX_CONN 4
#define WIFI_MANAGER_SOFTAP_PASSWORD "antifrost-setup"
#define WIFI_MANAGER_MIN_PASSWORD_LEN 8
#define WIFI_MANAGER_MAX_PASSWORD_LEN 64

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static wifi_manager_state_t s_state = WIFI_MANAGER_STATE_UNINITIALIZED;
static bool s_initialized;
static bool s_clearing_config;
static int s_retry_count;

static void wifi_manager_set_state(wifi_manager_state_t state)
{
    s_state = state;
}

static bool wifi_manager_sta_has_saved_ssid(void)
{
    wifi_config_t sta_config = {0};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Lettura configurazione STA fallita: %s", esp_err_to_name(err));
        return false;
    }

    return sta_config.sta.ssid[0] != '\0';
}

static void wifi_manager_build_softap_ssid(char *ssid, size_t ssid_len)
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Lettura MAC SoftAP fallita: %s", esp_err_to_name(err));
        strlcpy(ssid, "AntiFrost-Setup", ssid_len);
        return;
    }

    snprintf(ssid, ssid_len, "AntiFrost-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static esp_err_t wifi_manager_start_softap(void)
{
    char ssid[32] = {0};
    wifi_manager_build_softap_ssid(ssid, sizeof(ssid));

    wifi_config_t ap_config = {
        .ap = {
            .channel = WIFI_MANAGER_SOFTAP_CHANNEL,
            .max_connection = WIFI_MANAGER_SOFTAP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    strlcpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(ssid);
    strlcpy((char *)ap_config.ap.password,
            WIFI_MANAGER_SOFTAP_PASSWORD,
            sizeof(ap_config.ap.password));

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Stop Wi-Fi prima del SoftAP: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Impostazione modalita' SoftAP fallita: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configurazione SoftAP fallita: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Avvio SoftAP fallito: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    wifi_manager_set_state(WIFI_MANAGER_STATE_SOFTAP_ACTIVE);
    ESP_LOGI(TAG, "SoftAP attivo: SSID=%s password=%s", ssid, WIFI_MANAGER_SOFTAP_PASSWORD);
    return ESP_OK;
}

static esp_err_t wifi_manager_start_sta(void)
{
    s_retry_count = 0;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Impostazione modalita' STA fallita: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Avvio STA fallito: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    wifi_manager_set_state(WIFI_MANAGER_STATE_STA_CONNECTING);
    ESP_LOGI(TAG, "STA avviata con configurazione Wi-Fi salvata");
    return ESP_OK;
}

esp_err_t wifi_manager_configure_sta(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) >= sizeof(((wifi_sta_config_t *)0)->ssid)) {
        ESP_LOGW(TAG, "SSID STA non valido");
        return ESP_ERR_INVALID_ARG;
    }

    size_t password_len = password == NULL ? 0 : strlen(password);
    if (password_len > 0 &&
        (password_len < WIFI_MANAGER_MIN_PASSWORD_LEN ||
         password_len > WIFI_MANAGER_MAX_PASSWORD_LEN)) {
        ESP_LOGW(TAG, "Password STA non valida");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wifi_manager_init();
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t sta_config = {0};
    strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    if (password != NULL) {
        strlcpy((char *)sta_config.sta.password,
                password,
                sizeof(sta_config.sta.password));
    }
    sta_config.sta.threshold.authmode = password_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "Stop Wi-Fi prima della configurazione STA: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Impostazione storage Wi-Fi flash fallita: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Impostazione modalita' STA fallita: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Salvataggio configurazione STA fallito: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    ESP_LOGI(TAG, "Configurazione STA salvata per SSID=%s", ssid);
    return wifi_manager_start_sta();
}

esp_err_t wifi_manager_clear_config(void)
{
    esp_err_t err = wifi_manager_init();
    if (err != ESP_OK) {
        return err;
    }

    s_clearing_config = true;

    err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "Disconnessione Wi-Fi prima del reset fallita: %s", esp_err_to_name(err));
    }

    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "Stop Wi-Fi prima del reset fallito: %s", esp_err_to_name(err));
    }

    err = esp_wifi_restore();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Reset configurazione Wi-Fi fallito: %s", esp_err_to_name(err));
        s_clearing_config = false;
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    s_retry_count = 0;
    s_clearing_config = false;
    wifi_manager_set_state(WIFI_MANAGER_STATE_IDLE);
    ESP_LOGI(TAG, "Configurazione Wi-Fi cancellata");
    return ESP_OK;
}

static void wifi_manager_event_handler(void *arg,
                                       esp_event_base_t event_base,
                                       int32_t event_id,
                                       void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connessione STA in corso");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_clearing_config) {
            ESP_LOGI(TAG, "Disconnessione STA durante reset Wi-Fi");
            return;
        }

        wifi_manager_set_state(WIFI_MANAGER_STATE_STA_DISCONNECTED);

        if (s_retry_count < WIFI_MANAGER_MAX_STA_RETRIES) {
            s_retry_count++;
            ESP_LOGW(TAG, "STA disconnessa, retry %d/%d",
                     s_retry_count, WIFI_MANAGER_MAX_STA_RETRIES);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
            wifi_manager_set_state(WIFI_MANAGER_STATE_STA_CONNECTING);
            return;
        }

        ESP_LOGW(TAG, "STA non disponibile, fallback SoftAP");
        ESP_ERROR_CHECK_WITHOUT_ABORT(wifi_manager_start_softap());
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        s_retry_count = 0;
        wifi_manager_set_state(WIFI_MANAGER_STATE_STA_CONNECTED);
        ESP_LOGI(TAG, "STA connessa, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        wifi_manager_set_state(WIFI_MANAGER_STATE_SOFTAP_ACTIVE);
        ESP_LOGI(TAG, "Evento SoftAP START");
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event =
            (const wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Client SoftAP connesso, AID=%d", event->aid);
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event =
            (const wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Client SoftAP disconnesso, AID=%d", event->aid);
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Inizializzazione esp_netif fallita: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Creazione event loop default fallita: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        ESP_LOGE(TAG, "Creazione netif Wi-Fi fallita");
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Inizializzazione driver Wi-Fi fallita: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              wifi_manager_event_handler,
                                              NULL,
                                              NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler Wi-Fi fallita: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              wifi_manager_event_handler,
                                              NULL,
                                              NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registrazione handler IP fallita: %s", esp_err_to_name(err));
        wifi_manager_set_state(WIFI_MANAGER_STATE_ERROR);
        return err;
    }

    s_initialized = true;
    wifi_manager_set_state(WIFI_MANAGER_STATE_IDLE);
    ESP_LOGI(TAG, "Wi-Fi manager inizializzato");
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    esp_err_t err = wifi_manager_init();
    if (err != ESP_OK) {
        return err;
    }

    if (wifi_manager_sta_has_saved_ssid()) {
        return wifi_manager_start_sta();
    }

    ESP_LOGI(TAG, "Nessuna configurazione STA salvata, avvio SoftAP");
    return wifi_manager_start_softap();
}

wifi_manager_state_t wifi_manager_get_state(void)
{
    return s_state;
}

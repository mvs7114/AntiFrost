#include "fan_control.h"

#include "board_config.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "fan_control";

#define FAN_CONTROL_LEDC_TIMER LEDC_TIMER_1
#define FAN_CONTROL_LEDC_MODE LEDC_LOW_SPEED_MODE
#define FAN_CONTROL_LEDC_CHANNEL LEDC_CHANNEL_1
#define FAN_CONTROL_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define FAN_CONTROL_LEDC_FREQUENCY_HZ 25000
#define FAN_CONTROL_LEDC_MAX_DUTY ((1U << 10) - 1U)

static bool s_initialized;
static bool s_enabled;
static uint8_t s_duty_percent;

static uint32_t fan_control_percent_to_duty(uint8_t duty_percent)
{
    if (duty_percent > 100) {
        duty_percent = 100;
    }

    uint32_t active_low_duty = ((uint32_t)duty_percent * FAN_CONTROL_LEDC_MAX_DUTY) / 100U;
    return FAN_CONTROL_LEDC_MAX_DUTY - active_low_duty;
}

esp_err_t fan_control_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = FAN_CONTROL_LEDC_MODE,
        .duty_resolution = FAN_CONTROL_LEDC_DUTY_RES,
        .timer_num = FAN_CONTROL_LEDC_TIMER,
        .freq_hz = FAN_CONTROL_LEDC_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configurazione timer PWM ventola fallita: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t channel_config = {
        .gpio_num = BOARD_FAN_PWM_GPIO,
        .speed_mode = FAN_CONTROL_LEDC_MODE,
        .channel = FAN_CONTROL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = FAN_CONTROL_LEDC_TIMER,
        .duty = FAN_CONTROL_LEDC_MAX_DUTY,
        .hpoint = 0,
    };

    err = ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configurazione PWM ventola GPIO%d fallita: %s",
                 BOARD_FAN_PWM_GPIO, esp_err_to_name(err));
        return err;
    }

    s_enabled = false;
    s_duty_percent = 0;
    s_initialized = true;
    ESP_LOGI(TAG, "PWM ventola inizializzato su GPIO%d freq=%dHz",
             BOARD_FAN_PWM_GPIO, FAN_CONTROL_LEDC_FREQUENCY_HZ);
    return ESP_OK;
}

esp_err_t fan_control_set(bool enabled, uint8_t duty_percent)
{
    if (!s_initialized) {
        esp_err_t err = fan_control_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    if (duty_percent > 100) {
        duty_percent = 100;
    }

    if (!enabled) {
        duty_percent = 0;
    } else if (duty_percent == 0) {
        duty_percent = 20;
    }

    uint32_t duty = fan_control_percent_to_duty(duty_percent);
    esp_err_t err = ledc_set_duty(FAN_CONTROL_LEDC_MODE, FAN_CONTROL_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Impostazione duty ventola fallita: %s", esp_err_to_name(err));
        return err;
    }

    err = ledc_update_duty(FAN_CONTROL_LEDC_MODE, FAN_CONTROL_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Aggiornamento duty ventola fallito: %s", esp_err_to_name(err));
        return err;
    }

    s_enabled = enabled;
    s_duty_percent = duty_percent;
    ESP_LOGI(TAG, "Ventola %s duty=%u%%", enabled ? "ON" : "OFF", (unsigned int)duty_percent);
    return ESP_OK;
}

bool fan_control_is_enabled(void)
{
    return s_enabled;
}

uint8_t fan_control_get_duty_percent(void)
{
    return s_duty_percent;
}

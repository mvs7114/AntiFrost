# AntiFrost - Components

## board_config

Responsabilita':

- definire GPIO e pinout;
- inizializzare GPIO generici;
- fornire funzioni base per LED o linee diagnostiche.
- esporre la costante del WS2812 onboard su GPIO48.

API iniziale possibile:

```c
esp_err_t board_gpio_init(void);
```

## system_monitor

Responsabilita':

- leggere chip info;
- leggere dimensione flash;
- leggere heap interna e PSRAM;
- leggere uptime;
- leggere reset reason;
- esporre diagnostica memoria;
- stampare diagnostica di boot e stato periodico.
- predisporre la lettura temperatura interna solo se utile e supportata stabilmente dal target.

API iniziale possibile:

```c
esp_err_t sys_monitor_init(void);
esp_err_t sys_monitor_get_snapshot(sys_monitor_snapshot_t *snapshot);
uint32_t sys_monitor_get_heap_free(void);
uint32_t sys_monitor_get_psram_free(void);
uint64_t sys_monitor_get_uptime_ms(void);
void sys_monitor_log_boot(void);
void sys_monitor_log_snapshot(void);
```

## dht_sensor

Responsabilita':

- inizializzare GPIO del DHT;
- leggere temperatura e umidita';
- validare errori di lettura.

API prevista:

```c
esp_err_t dht_init(void);
esp_err_t dht_read_data(float *temperature_c, float *humidity_percent);
```

## fan_control

Responsabilita':

- configurare PWM tramite LEDC;
- impostare velocita' ventola;
- gestire limiti e spegnimento sicuro.

API prevista:

```c
esp_err_t fan_init(void);
esp_err_t fan_set_speed(uint8_t speed_percent);
```

## ir_led_control

Responsabilita':

- inizializzare controllo LED IR;
- accendere/spegnere LED IR;
- in seguito gestire duty cycle o sicurezza termica.

API prevista:

```c
esp_err_t ir_led_init(void);
esp_err_t ir_led_set_state(bool enabled);
```

## app_logger

Responsabilita':

- scrivere log applicativi persistenti su SD;
- distinguere log seriale di debug da evidenze salvate su file;
- registrare eventi importanti di sensori, attuatori, camera, storage, rete, OTA e logica AntiFrost;
- gestire assenza SD senza bloccare il firmware;
- applicare rotazione o limite dimensione dei file di log.

Dipendenze previste:

- `sd_storage` per mount, path base e stato della SD;
- `app_events` quando sara' disponibile una coda eventi applicativa.

API prevista:

```c
esp_err_t app_logger_init(void);
esp_err_t app_logger_write(const char *level, const char *source, const char *message);
esp_err_t app_logger_flush(void);
```

Formato iniziale suggerito:

```text
timestamp_ms;level;source;message
```

## image_analysis

Responsabilita':

- confrontare immagini catturate in istanti temporali diversi;
- analizzare immagini illuminate da LED IR con condizioni ripetibili;
- calcolare metriche di differenza, opacita', contrasto e variazione locale;
- predisporre analisi in frequenza, analisi sottrattiva e modelli specifici per condensa/ghiaccio;
- produrre un risultato sintetico consumabile da `antifrost_logic`;
- registrare metriche e anomalie tramite `app_logger`.

Dipendenze previste:

- `camera_manager` per frame o file immagine acquisiti;
- `ir_led_control` per sincronizzare acquisizioni con illuminazione IR;
- `sd_storage` per leggere/scrivere immagini campione o snapshot diagnostici;
- `app_logger` per salvare evidenze e metriche;
- `system_monitor` per controllare heap/PSRAM prima di elaborazioni pesanti.

API prevista:

```c
typedef struct {
    float difference_score;
    float opacity_score;
    float contrast_score;
    float frequency_score;
    bool frost_or_condensation_suspected;
} image_analysis_result_t;

esp_err_t image_analysis_init(void);
esp_err_t image_analysis_compare_frames(
    const uint8_t *baseline,
    size_t baseline_len,
    const uint8_t *current,
    size_t current_len,
    image_analysis_result_t *result);
```

Note implementative:

- iniziare con immagini grayscale o luminanza estratta dal JPEG, per ridurre memoria e CPU;
- conservare un frame baseline valido e confrontarlo con frame successivi;
- loggare sempre parametri di acquisizione: timestamp, IR on/off, esposizione se disponibile, dimensione frame;
- rinviare modelli piu' pesanti finche' non sono noti tempi CPU, memoria e qualita' dei frame reali.

## Moduli Successivi

- `camera_manager`
- `image_analysis`
- `sd_storage`
- `app_logger`
- `wifi_manager`
- `web_server`
- `ota_manager`
- `app_events`
- `antifrost_logic`

Questi moduli saranno introdotti dopo i test hardware base.

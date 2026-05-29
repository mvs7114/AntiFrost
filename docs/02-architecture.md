# AntiFrost - Architecture

## Stile Architetturale

Il firmware segue un'architettura a componenti ESP-IDF.

Ogni capacita' significativa viene isolata in un componente con:

- header pubblico;
- implementazione privata;
- `CMakeLists.txt`;
- dipendenze ESP-IDF esplicite;
- API basate su `esp_err_t` dove appropriato.

## Regole

- Non usare Arduino.
- Non includere API Arduino.
- Usare logging ESP-IDF con `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`.
- Usare `ESP_ERROR_CHECK` solo quando un errore deve bloccare il boot.
- Preferire funzioni che restituiscono `esp_err_t` nei moduli.
- Centralizzare GPIO e pinout in `board_config`.
- Tenere `app_main()` snello.

## Struttura Target

```text
AntiFrost/
  components/
    board_config/
    system_monitor/
    dht_sensor/
    fan_control/
    ir_led_control/
    camera_manager/
    image_analysis/
    sd_storage/
    app_logger/
    wifi_manager/
    web_server/
    ota_manager/
    app_events/
    antifrost_logic/
  docs/
  src/
    main.c
```

## Avvio Firmware

Sequenza prevista:

1. Boot ESP-IDF.
2. Diagnostica base chip, flash, PSRAM e heap.
3. Inizializzazione `board_config`.
4. Inizializzazione moduli di sistema.
5. Mount storage, se presente.
6. Avvio `app_logger` per log persistenti su SD.
7. Avvio task applicativi.
8. Loop o task di heartbeat.

## Logging

Il monitor seriale resta il canale primario durante sviluppo, flash e debug immediato.

I log persistenti sono una responsabilita' separata del componente `app_logger`: il modulo scrive eventi applicativi e diagnostici su file nella SD quando `sd_storage` e' montato. In questo modo il firmware puo' conservare evidenze anche senza PC collegato alla seriale.

Regole iniziali:

- `ESP_LOGI`, `ESP_LOGW` ed `ESP_LOGE` restano validi per console seriale e debug runtime.
- `app_logger` registra eventi applicativi rilevanti, errori sensori, comandi attuatori, soglie AntiFrost, eventi camera/storage/rete e riavvii.
- se la SD non e' disponibile, il firmware deve continuare a funzionare e loggare su seriale.
- i file di log devono avere rotazione o limite dimensione per evitare saturazione della SD.

## Analisi Immagini

La cattura immagini e l'analisi immagini sono responsabilita' separate.

`camera_manager` gestisce inizializzazione camera, cattura frame e rilascio buffer. `image_analysis` riceve frame o file immagine e produce metriche utili alla logica AntiFrost: variazioni nel tempo, opacita', contrasto, riflessione IR e possibili indicatori di ghiaccio o condensa.

Regole iniziali:

- acquisire immagini confrontabili tra loro, con condizioni IR controllate e metadati temporali;
- mantenere algoritmi e soglie separati dalla cattura camera;
- salvare metriche e risultati sintetici nei log applicativi;
- evitare elaborazioni pesanti nel task camera se possono bloccare acquisizione o web server;
- preferire pipeline incrementali: prima differenza frame e opacita', poi analisi in frequenza o modelli piu' complessi.

## Strategia di Crescita

La struttura verra' introdotta gradualmente. All'inizio `main.c` puo' restare semplice; i componenti saranno estratti quando servono davvero al test successivo.

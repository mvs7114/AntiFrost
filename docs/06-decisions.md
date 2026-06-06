# AntiFrost - Technical Decisions

Questo registro conserva le decisioni tecniche prese durante lo sviluppo.

## ADR-001 - ESP-IDF senza Arduino

Decisione:

Usare ESP-IDF nativo tramite PlatformIO.

Motivazione:

- maggiore controllo su componenti, memoria, task e periferiche;
- migliore coerenza con architettura modulare;
- integrazione diretta con OTA, Wi-Fi, HTTP server e FreeRTOS.

Conseguenze:

- niente API Arduino;
- componenti ESP-IDF con `CMakeLists.txt`;
- maggiore attenzione a configurazione e dipendenze.

## ADR-002 - Pinout centralizzato

Decisione:

Tutti i GPIO applicativi saranno definiti in `board_config`.

Motivazione:

- riduce errori di cablaggio;
- rende semplice cambiare pin;
- evita duplicazione tra moduli.

Conseguenze:

- i moduli dipendono da `board_config` per i pin;
- il pinout va documentato e validato prima dei test hardware.

## ADR-003 - Sviluppo per test incrementali

Decisione:

Implementare una capacita' alla volta seguendo TEST 01-14.

Motivazione:

- il progetto combina sensori, attuatori, camera, storage, rete e OTA;
- ogni area puo' fallire per ragioni hardware o software;
- test piccoli rendono piu' facile isolare problemi.

Conseguenze:

- prima diagnostica e componenti base;
- poi sensori/attuatori;
- camera, Wi-Fi, web e OTA arrivano dopo.

## ADR-004 - Log applicativi persistenti su SD

Decisione:

Introdurre un componente `app_logger` dedicato alla scrittura di log applicativi su file nella SD.

Motivazione:

- il monitor seriale e' utile per sviluppo e debug live, ma richiede un PC collegato;
- i file di log su SD conservano evidenze di boot, errori, letture sensori, comandi attuatori e decisioni AntiFrost;
- le evidenze persistenti aiutano a diagnosticare problemi avvenuti lontano dal banco di sviluppo;
- separare `app_logger` da `ESP_LOGx` evita di accoppiare il debug seriale con la persistenza.

Conseguenze:

- `app_logger` dipende da `sd_storage`;
- la mancanza della SD non deve bloccare il firmware;
- i log devono prevedere flush controllato, limite dimensione o rotazione;
- gli eventi critici vanno comunque visibili su seriale tramite logging ESP-IDF.

## ADR-005 - Analisi immagini separata dalla camera

Decisione:

Introdurre un componente `image_analysis` dedicato al confronto e all'interpretazione delle immagini catturate.

Motivazione:

- `camera_manager` deve restare responsabile solo di camera, frame buffer e acquisizione;
- l'analisi immagini richiede algoritmi, soglie e metriche che evolveranno separatamente;
- confronti temporali, illuminazione LED, opacita' e analisi sottrattiva devono produrre evidenze riutilizzabili;
- la logica AntiFrost deve consumare risultati sintetici, non dettagli di frame buffer o JPEG.

Conseguenze:

- `image_analysis` dipende da `camera_manager`, `led_control`, `sd_storage`, `app_logger` e `system_monitor`;
- i primi algoritmi devono essere leggeri e misurabili su ESP32-S3;
- modelli piu' complessi saranno introdotti solo dopo aver validato memoria, tempi e qualita' dei frame reali;
- le metriche devono essere loggate per rendere confrontabili i test nel tempo.

## ADR-006 - Accesso camera serializzato e JPEG validato

Decisione:

Proteggere ogni acquisizione camera con un mutex e considerare valido un frame JPEG solo se contiene marker SOI `FF D8` ed EOI `FF D9`.

Motivazione:

- `/capture` e `/stream` possono richiedere frame in momenti ravvicinati;
- la concorrenza su `esp_camera_fb_get()` puo' amplificare timeout e frame corrotti;
- i marker JPEG danno un controllo economico prima di inviare dati incompleti al client.

Conseguenze:

- se la camera e' occupata, la capture fallisce rapidamente invece di bloccare indefinitamente;
- lo stream rilascia sempre il frame buffer prima di liberare il mutex;
- la UI non avvia capture automatica al caricamento e non usa polling `/capture` come fallback live quando lo stream e' attivo.

## ADR-007 - DMA PSRAM camera disattivato

Decisione:

Mantenere `CONFIG_CAMERA_PSRAM_DMA` disattivato per il profilo operativo camera.

Motivazione:

- con DMA PSRAM OFF, QVGA, fb_count=1, XCLK 10 MHz e JPEG quality 15 la camera ha prodotto JPEG validi `FF D8 ... FF D9`;
- con DMA PSRAM ON, a parita' di parametri, `/camera/test` ha restituito frame null e JPEG non valido;
- non conviene inseguire DMA PSRAM finche' capture e stream non sono stabili.

Conseguenze:

- il profilo operativo usa frame buffer in PSRAM senza DMA PSRAM camera;
- capture e stream restano protetti da mutex e validazione JPEG;
- eventuali test futuri su DMA PSRAM vanno trattati come diagnostica separata, non come default.

## ADR-008 - Inversione pin DHT11 e ventola PWM

Decisione:

Assegnare il DHT11 data a GPIO21 e la ventola PWM a GPIO14.

Motivazione:

- il circuito DHT11 ha una resistenza di pull-up esterna da 4.7 kOhm;
- la linea dati DHT11 e' dedicata, senza carichi o altri collegamenti sul segnale;
- GPIO21 e' un GPIO generico libero rispetto a camera, USB, SD, PSRAM e seriale;
- GPIO14 resta adatto all'uscita PWM tramite LEDC per il controllo ventola.

Conseguenze:

- `BOARD_DHT11_GPIO` vale `GPIO_NUM_21`;
- `BOARD_FAN_PWM_GPIO` vale `GPIO_NUM_14`;
- il cablaggio hardware deve rispettare la nuova assegnazione prima dei test.

## ADR-009 - Livello log seriale parametrizzato

Decisione:

Parametrizzare il livello dei log seriali ESP-IDF tramite `APP_SERIAL_LOG_LEVEL` in `main.c`.

Motivazione:

- il monitor seriale e' utile durante sviluppo e test hardware;
- troppi log informativi possono rallentare il firmware e rendere il terminale rumoroso;
- errori e warning devono poter restare visibili anche quando si silenziano gli `INFO`;
- `DEBUG` deve essere disponibile solo quando richiesto esplicitamente.

Conseguenze:

- il livello runtime globale viene impostato con `esp_log_level_set("*", APP_SERIAL_LOG_LEVEL)`;
- i livelli consigliati sono `ESP_LOG_ERROR`, `ESP_LOG_WARN`, `ESP_LOG_INFO` e `ESP_LOG_DEBUG`;
- `sdkconfig.defaults` abilita `CONFIG_LOG_DYNAMIC_LEVEL_CONTROL` e massimo compilato `DEBUG`;
- i log di boot molto precoci possono comparire prima che `app_main` imposti il livello runtime.

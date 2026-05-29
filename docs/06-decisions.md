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
- confronti temporali, illuminazione IR, opacita' e analisi sottrattiva devono produrre evidenze riutilizzabili;
- la logica AntiFrost deve consumare risultati sintetici, non dettagli di frame buffer o JPEG.

Conseguenze:

- `image_analysis` dipende da `camera_manager`, `ir_led_control`, `sd_storage`, `app_logger` e `system_monitor`;
- i primi algoritmi devono essere leggeri e misurabili su ESP32-S3;
- modelli piu' complessi saranno introdotti solo dopo aver validato memoria, tempi e qualita' dei frame reali;
- le metriche devono essere loggate per rendere confrontabili i test nel tempo.

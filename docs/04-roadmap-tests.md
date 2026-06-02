# AntiFrost - Roadmap Tests

## TEST 01 - Boot Diagnostico

Obiettivo:

- verificare chip ESP32-S3;
- verificare flash 16 MB;
- verificare PSRAM 8 MB;
- loggare heap disponibile;
- mantenere heartbeat seriale.

Accettazione:

- build pulita;
- log senza errori critici;
- valori memoria coerenti con hardware target.

## TEST 02 - Sensore DHT

Obiettivo:

- inizializzare il sensore;
- leggere temperatura e umidita';
- gestire errori di lettura senza bloccare il firmware.

## TEST 03 - Controllo Ventola

Obiettivo:

- configurare PWM LEDC;
- testare 0%, 25%, 50%, 75%, 100%;
- verificare che il comando sia stabile.

## TEST 04 - LED

Obiettivo:

- controllare accensione e spegnimento;
- evitare stati indefiniti al boot;
- loggare stato attuale.

Nota pinout:

- LED applicativo su GPIO47;
- WS2812 onboard di stato su GPIO48, da gestire separatamente dal LED applicativo.

## TEST 05 - Camera Init

Obiettivo:

- inizializzare camera;
- verificare PSRAM disponibile per frame buffer.

## TEST 06 - JPEG Capture

Obiettivo:

- catturare un frame JPEG;
- verificare dimensione buffer;
- validare marker JPEG SOI `FF D8` ed EOI `FF D9`;
- proteggere ogni `esp_camera_fb_get()` con mutex e timeout breve;
- rilasciare correttamente il frame.

Matrice camera OV3660 usata per isolare il problema DMA:

- A: DMA OFF, PSRAM 80 MHz, CPU 240 MHz, QVGA, fb_count=1, XCLK 10 MHz.
- B: DMA OFF, PSRAM 80 MHz, CPU 240 MHz, QVGA, fb_count=1, XCLK 20 MHz.
- C: DMA ON, PSRAM 80 MHz, CPU 240 MHz, QVGA, fb_count=1, XCLK 10 MHz. Non stabile nei test: frame null/timeout.
- D: DMA ON, PSRAM 40 MHz, CPU 240 MHz, QVGA, fb_count=1, XCLK 10 MHz. Solo per verifica timing, non profilo operativo.

Profilo operativo corrente:

- DMA PSRAM disattivato;
- PSRAM OPI 80 MHz;
- CPU 240 MHz;
- QVGA, fb_count=1, XCLK 10 MHz, JPEG quality 12;
- `/capture` e `/stream` attivi solo con mutex e validazione JPEG.

## TEST 07 - Storage

Obiettivo:

- montare storage;
- scrivere file;
- leggere file;
- gestire errori mount o spazio.

Nota pinout:

- SD onboard riservata su GPIO38, GPIO39 e GPIO40;
- in alternativa usare FAT su flash senza occupare pin.

## TEST 08 - Log Persistenti su SD

Obiettivo:

- creare directory log su SD;
- scrivere un file di log applicativo;
- verificare flush e rilettura del contenuto;
- gestire SD assente o non montata senza bloccare il firmware;
- mantenere comunque il monitor seriale attivo.

Accettazione:

- file log creato su SD;
- almeno un evento di boot e uno di test salvati;
- errore SD loggato su seriale se la scheda non e' disponibile;
- limite dimensione o rotazione definito prima dell'uso continuativo.

## TEST 09 - Wi-Fi

Obiettivo:

- scan reti;
- connessione a rete configurata;
- stato connessione.

## TEST 10 - Web Status

Obiettivo:

- esporre pagina o endpoint di stato;
- mostrare dati sensore e diagnostica.

## TEST 11 - Camera via Web

Obiettivo:

- catturare immagine da endpoint web;
- inviare JPEG al client.

## TEST 12 - Analisi Immagini LED

Obiettivo:

- catturare immagini in istanti temporali diversi con LED controllato;
- confrontare baseline e immagine corrente;
- calcolare metriche iniziali di differenza, opacita' e contrasto;
- salvare risultati sintetici nei log applicativi;
- verificare consumo heap/PSRAM durante l'elaborazione.

Accettazione:

- confronto completato senza esaurire memoria;
- risultato numerico stabile per frame simili;
- variazione rilevabile su frame volutamente differenti;
- metriche registrate su seriale e, se disponibile, su SD.

Evoluzioni previste:

- analisi sottrattiva;
- analisi in frequenza;
- modelli specifici per condensa, brina, ghiaccio o perdita di trasparenza.

## TEST 13 - Logica AntiFrost Base

Obiettivo:

- valutare temperatura/umidita';
- integrare metriche `image_analysis` quando disponibili;
- attivare ventola o LED secondo soglie;
- applicare isteresi.

## TEST 14 - OTA

Obiettivo:

- scaricare firmware;
- validare update;
- riavviare sul nuovo slot.

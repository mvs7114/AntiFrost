# AntiFrost - Overview

AntiFrost e' un sistema antighiaccio e anticondensa basato su Freenove ESP32-S3 WROOM N16R8 CAM.

Il firmware usa ESP-IDF tramite PlatformIO, senza dipendenze Arduino. Lo sviluppo procede per moduli ESP-IDF separati, con test incrementali, logging seriale chiaro e log applicativi persistenti su SD.

## Obiettivi

- Monitorare temperatura e umidita'.
- Attivare azioni anticondensa e antighiaccio tramite ventola, LED o altri attuatori.
- Analizzare immagini nel tempo, anche illuminate da LED, per stimare opacita', condensa, brina o ghiaccio.
- Rendere disponibile diagnostica di sistema su seriale, su file di log SD e, in seguito, via web.
- Supportare camera, storage SD/FAT, Wi-Fi e OTA in fasi successive.
- Mantenere il codice modulare, verificabile e adatto a crescita progressiva.

## Hardware Target

- Board: Freenove ESP32-S3 WROOM N16R8 CAM.
- MCU: ESP32-S3.
- Flash: 16 MB.
- PSRAM: 8 MB Octal.
- Upload: CH343 USB-UART.
- Framework: ESP-IDF.
- Build system: PlatformIO.

## Stato Iniziale

Il progetto contiene gia':

- configurazione PlatformIO per ESP-IDF;
- board custom Freenove ESP32-S3 N16R8;
- partizioni 16 MB con OTA e storage FAT;
- `src/main.c` con diagnostica chip, flash, PSRAM e heartbeat.

## Regola di Sviluppo

Le modifiche devono essere piccole e verificabili. Prima di interventi che modificano piu' di 5 file o impattano piu' moduli/componenti, serve conferma esplicita.

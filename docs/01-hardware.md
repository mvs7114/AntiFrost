# AntiFrost - Hardware

## Board

Freenove ESP32-S3 WROOM N16R8 CAM.

Caratteristiche di riferimento:

- ESP32-S3 WROOM N16R8.
- 16 MB flash.
- 8 MB PSRAM Octal.
- Connettivita' Wi-Fi e Bluetooth.
- Camera integrata o connettore camera, secondo variante Freenove.
- Interfaccia USB-UART CH343.

## Configurazione PlatformIO

Il target principale e' definito in `platformio.ini`:

- `framework = espidf`
- `board_build.flash_size = 16MB`
- `board_build.psram_type = opi`
- `board_build.partitions = partitions_16mb.csv`
- `upload_protocol = esptool`

## Memoria

La tabella partizioni attuale prevede:

- NVS per configurazioni persistenti;
- OTA data;
- due slot applicativi OTA da 4 MB;
- storage FAT nella parte alta della flash.

La SD onboard, se presente e montata, viene considerata lo storage preferito per log applicativi persistenti. La FAT su flash resta utile per configurazioni, fallback o dati piccoli, ma non deve essere usata come destinazione principale per log continui.

## Pinout

Il pinout applicativo va centralizzato nel componente `board_config`.

Nessun modulo applicativo dovrebbe definire GPIO direttamente. I moduli devono importare costanti o funzioni da `board_config`, cosi' da poter cambiare pin senza riscrivere la logica.

Dal pinout Freenove ESP32-S3 WROOM:

- camera riservata su GPIO4-GPIO13 e GPIO15-GPIO18;
- USB nativo su GPIO19 e GPIO20;
- SD onboard su GPIO38, GPIO39 e GPIO40;
- PSRAM su GPIO35, GPIO36 e GPIO37;
- UART0 debug/programmazione su GPIO43 e GPIO44;
- WS2812 onboard su GPIO48.

## Periferiche Previste

- Sensore temperatura/umidita' DHT.
- Ventola controllata in PWM.
- LED IR controllati via GPIO o driver dedicato.
- LED di stato WS2812 onboard.
- Camera ESP32.
- Storage SD o FAT, secondo cablaggio e necessita'.
- Log applicativi persistenti su SD.
- Wi-Fi.

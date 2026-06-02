# AntiFrost - Pinout

Questo file e' la fonte di lavoro per il pinout applicativo della board Freenove ESP32-S3 WROOM N16R8 CAM.

La mappatura e' stata aggiornata sul pinout Freenove ESP32-S3 WROOM allegato al progetto. Prima di pilotare carichi reali resta obbligatoria la verifica su cablaggio fisico, alimentazione e driver.

## Regole

- Ogni GPIO applicativo deve essere dichiarato in `board_config`.
- Nessun modulo deve usare numeri GPIO hardcoded.
- Le uscite devono avere uno stato sicuro al boot.
- I pin camera, USB, SD, PSRAM, seriale e boot non devono essere riusati.
- I pin strapping, JTAG o riservati vanno evitati salvo decisione esplicita.
- I GPIO 19 e 20 sono USB nativi: non usarli per sensori o attuatori.
- I GPIO 35, 36 e 37 sono marcati PSRAM: non usarli.
- I GPIO 38, 39 e 40 sono assegnati allo slot SD onboard: non usarli se si abilita la SD.

## Tabella Applicativa

| Funzione | GPIO | Direzione | Stato boot | Note |
| --- | --- | --- | --- | --- |
| DHT11 data | GPIO14 | input/output open-drain | pull-up | T14, ADC2_CH3; libero rispetto a camera, USB, SD e PSRAM |
| Fan PWM | GPIO21 | output PWM | off | LEDC previsto nel modulo `fan_control`; pin esposto e PWM capable |
| LED enable | GPIO47 | output | off | verificare driver, corrente e dissipazione |
| GPIO2 test | GPIO2 | output | low | Linea di test manuale per verifiche hardware temporanee |
| Status LED WS2812 | GPIO48 | output RMT | off | LED RGB onboard WS2812 |
| Camera pins | GPIO4-13, GPIO15-18 | mixed | n/a | riservati alla camera Freenove |
| SD card onboard | GPIO38-GPIO40 | mixed | n/a | SD_CMD, SD_CLK, SD_DATA; non disponibile se non montata fisicamente/configurata |
| USB native | GPIO19-GPIO20 | USB | n/a | USB_D+ e USB_D-; riservati |
| UART0 debug | GPIO43-GPIO44 | serial | n/a | U0TXD/U0RXD; lasciare liberi per log e programmazione |

## Pin Applicativi Liberi Consigliati

Questi pin risultano esposti e non occupati da camera, USB, SD, PSRAM o seriale secondo il pinout Freenove:

| GPIO | Funzioni secondarie da pinout | Uso consigliato |
| --- | --- | --- |
| GPIO14 | T14, ADC2_CH3 | DHT11 data |
| GPIO21 | GPIO generico | Fan PWM |
| GPIO47 | GPIO generico | LED enable |
| GPIO2 | ADC1, touch, LED_ON | GPIO2 test |
| GPIO48 | WS2812 | Status LED onboard |

## Pin Da Evitare o Riservare

| GPIO | Motivo |
| --- | --- |
| GPIO0 | Boot strap |
| GPIO1 | ADC1, touch; evitare salvo necessita' |
| GPIO3 | JTAG_EN/log da pinout; evitare salvo decisione esplicita |
| GPIO4-GPIO13 | Camera |
| GPIO15-GPIO18 | Camera |
| GPIO19-GPIO20 | USB_D+ e USB_D- |
| GPIO35-GPIO37 | PSRAM |
| GPIO38-GPIO40 | SD onboard |
| GPIO41-GPIO42 | JTAG MTDI/MTMS |
| GPIO43-GPIO44 | UART0 debug/programmazione |
| GPIO45 | VSPI/strap; evitare per test iniziali |
| GPIO46 | LOG/strap; evitare per test iniziali |

## Pin Camera Freenove

La camera usa la mappatura indicata dal pinout Freenove:

| Segnale | GPIO |
| --- | --- |
| SIOD | GPIO4 |
| SIOC | GPIO5 |
| VSYNC | GPIO6 |
| HREF | GPIO7 |
| Y4 | GPIO8 |
| Y3 | GPIO9 |
| Y5 | GPIO10 |
| Y2 | GPIO11 |
| Y6 | GPIO12 |
| PCLK | GPIO13 |
| XCLK | GPIO15 |
| Y9 | GPIO16 |
| Y8 | GPIO17 |
| Y7 | GPIO18 |

## Pin SD Onboard

| Segnale | GPIO |
| --- | --- |
| SD_CMD | GPIO38 |
| SD_CLK | GPIO39 |
| SD_DATA | GPIO40 |

## Pin USB e Seriale

| Funzione | GPIO |
| --- | --- |
| USB_D+ | GPIO19 |
| USB_D- | GPIO20 |
| U0TXD | GPIO43 |
| U0RXD | GPIO44 |
| LED TX | GPIO43 |
| LED RX | GPIO44 |

## Decisioni Aperte

- Tipo esatto di sensore DHT: prima assegnazione su DHT11.
- Driver ventola: MOSFET, transistor, driver dedicato o ventola PWM 4 fili.
- LED: assorbimento, driver, alimentazione e dissipazione.
- Storage: SD onboard su GPIO38-GPIO40 oppure FAT su flash.
- Uso di GPIO1: disponibile solo dopo verifica di ADC/touch e comportamento al boot.

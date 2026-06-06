#ifndef ANTIFROST_BOARD_CONFIG_H
#define ANTIFROST_BOARD_CONFIG_H

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_NAME "Freenove ESP32-S3 WROOM N16R8 CAM"

/*
 * Camera pinout used by the Freenove CameraWebServer example when
 * CAMERA_MODEL_ESP32S3_EYE is selected.
 */
#define BOARD_CAM_PWDN_GPIO GPIO_NUM_NC
#define BOARD_CAM_RESET_GPIO GPIO_NUM_NC
#define BOARD_CAM_XCLK_GPIO GPIO_NUM_15
#define BOARD_CAM_SIOD_GPIO GPIO_NUM_4
#define BOARD_CAM_SIOC_GPIO GPIO_NUM_5
#define BOARD_CAM_Y2_GPIO GPIO_NUM_11
#define BOARD_CAM_Y3_GPIO GPIO_NUM_9
#define BOARD_CAM_Y4_GPIO GPIO_NUM_8
#define BOARD_CAM_Y5_GPIO GPIO_NUM_10
#define BOARD_CAM_Y6_GPIO GPIO_NUM_12
#define BOARD_CAM_Y7_GPIO GPIO_NUM_18
#define BOARD_CAM_Y8_GPIO GPIO_NUM_17
#define BOARD_CAM_Y9_GPIO GPIO_NUM_16
#define BOARD_CAM_VSYNC_GPIO GPIO_NUM_6
#define BOARD_CAM_HREF_GPIO GPIO_NUM_7
#define BOARD_CAM_PCLK_GPIO GPIO_NUM_13

/*
 * Application GPIOs reserved for the first hardware tests.
 * These pins are exposed on the board and do not overlap the camera pinout.
 */
#define BOARD_DHT11_GPIO GPIO_NUM_21
#define BOARD_FAN_PWM_GPIO GPIO_NUM_14
#define BOARD_LED_GPIO GPIO_NUM_47
#define BOARD_GPIO2_TEST_GPIO GPIO_NUM_2
#define BOARD_WS2812_GPIO GPIO_NUM_48

#define BOARD_SD_CMD_GPIO GPIO_NUM_38
#define BOARD_SD_CLK_GPIO GPIO_NUM_39
#define BOARD_SD_D0_GPIO GPIO_NUM_40

esp_err_t board_gpio_init(void);

#ifdef __cplusplus
}
#endif

#endif

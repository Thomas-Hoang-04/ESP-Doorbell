#ifndef CAMERA_H
#define CAMERA_H

#include "esp_err.h"
#include "esp_camera.h"

#define CAMERA_TAG "CAMERA"

// Camera Control Pins Mapping
#define CAM_PIN_PWDN GPIO_NUM_NC
#define CAM_PIN_RESET GPIO_NUM_NC
#define CAM_PIN_XCLK GPIO_NUM_15
#define CAM_PIN_SIOD GPIO_NUM_4
#define CAM_PIN_SIOC GPIO_NUM_5
#define CAM_PIN_VSYNC GPIO_NUM_6
#define CAM_PIN_HREF GPIO_NUM_7
#define CAM_PIN_PCLK GPIO_NUM_13

// Camera Data Pins Mapping
#define CAM_PIN_D7 GPIO_NUM_16
#define CAM_PIN_D6 GPIO_NUM_17
#define CAM_PIN_D5 GPIO_NUM_18
#define CAM_PIN_D4 GPIO_NUM_12
#define CAM_PIN_D3 GPIO_NUM_10
#define CAM_PIN_D2 GPIO_NUM_8
#define CAM_PIN_D1 GPIO_NUM_9
#define CAM_PIN_D0 GPIO_NUM_11

// Camera Configuration
#define CAMERA_XCLK_FREQ_HZ (20 * 1000 * 1000)
#define CAMERA_PIXEL_FORMAT PIXFORMAT_JPEG
#define CAMERA_FRAME_SIZE FRAMESIZE_HD
#define CAMERA_JPEG_QUALITY 12
#define CAMERA_FB_COUNT 1
#define CAMERA_GRAB_MODE CAMERA_GRAB_WHEN_EMPTY

extern camera_config_t camera_config;

esp_err_t camera_init(void);

#endif
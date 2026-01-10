/**
 * @file video_capture.h
 * @brief Audio/Video capture and MP4 recording with esp_capture framework
 */

#ifndef DOORBELL_VIDEO_SRC_H
#define DOORBELL_VIDEO_SRC_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_capture.h"
#include "esp_capture_types.h"
#include "esp_capture_sink.h"
#include "esp_err.h"
#include "driver/i2c_master.h"

#include "../sd_handler/sd_handler.h"

#define AV_LOG_TAG "AV_CAPTURE"
#define AV_VIDEO_TAG "AV_MP4"

#ifdef __cplusplus
extern "C" {
#endif

#define CAM_PIN_PWDN    GPIO_NUM_NC
#define CAM_PIN_RESET   GPIO_NUM_NC
#define CAM_PIN_XCLK    GPIO_NUM_15
#define CAM_PIN_SIOD    GPIO_NUM_4
#define CAM_PIN_SIOC    GPIO_NUM_5
#define CAM_PIN_VSYNC   GPIO_NUM_6
#define CAM_PIN_HREF    GPIO_NUM_7
#define CAM_PIN_PCLK    GPIO_NUM_13
#define CAM_PIN_D7      GPIO_NUM_16
#define CAM_PIN_D6      GPIO_NUM_17
#define CAM_PIN_D5      GPIO_NUM_18
#define CAM_PIN_D4      GPIO_NUM_12
#define CAM_PIN_D3      GPIO_NUM_10
#define CAM_PIN_D2      GPIO_NUM_8
#define CAM_PIN_D1      GPIO_NUM_9
#define CAM_PIN_D0      GPIO_NUM_11

#define CAMERA_XCLK_FREQ_HZ     (20 * 1000 * 1000)
#define CAMERA_SCCB_I2C_PORT    I2C_NUM_0
#define CAMERA_BUFFER_COUNT     2

#define VIDEO_FORMAT            ESP_CAPTURE_FMT_ID_MJPEG
#define VIDEO_WIDTH             1280
#define VIDEO_HEIGHT            720
#define VIDEO_FPS               24

#define AUDIO_FORMAT            ESP_CAPTURE_FMT_ID_AAC
#define AUDIO_SAMPLE_RATE       16000
#define AUDIO_CHANNELS          2
#define AUDIO_BITS_PER_SAMPLE   16

#define AV_MUXER_TYPE                   ESP_MUXER_TYPE_MP4
#define AV_CAPTURE_MP4_DURATION_MSEC    (30 * 1000)
#define AV_MUXER_CACHE_SIZE             (16 * 1024)
#define AV_CAPTURE_MP4_DIR              MOUNT_POINT"/video"

/**
 * @brief Default DVP video source configuration macro
 */
#define DVP_SRC_DEFAULT(buffer_count) { \
    .buf_count = buffer_count, \
    .pwr_pin = CAM_PIN_PWDN, \
    .reset_pin = CAM_PIN_RESET, \
    .xclk_pin = CAM_PIN_XCLK, \
    .xclk_freq = CAMERA_XCLK_FREQ_HZ, \
    .vsync_pin = CAM_PIN_VSYNC, \
    .href_pin = CAM_PIN_HREF, \
    .pclk_pin = CAM_PIN_PCLK, \
    .i2c_port = CAMERA_SCCB_I2C_PORT, \
    .data = {CAM_PIN_D0, CAM_PIN_D1, CAM_PIN_D2, CAM_PIN_D3, \
             CAM_PIN_D4, CAM_PIN_D5, CAM_PIN_D6, CAM_PIN_D7} \
}

/**
 * @brief Default SCCB (I2C) configuration macro
 */
#define SCCB_DEFAULT() { \
    .clk_source = I2C_CLK_SRC_DEFAULT, \
    .i2c_port = CAMERA_SCCB_I2C_PORT, \
    .scl_io_num = CAM_PIN_SIOC, \
    .sda_io_num = CAM_PIN_SIOD, \
    .glitch_ignore_cnt = 7, \
    .flags.enable_internal_pullup = 1 \
}

/**
 * @brief Struct to hold all AV capture handles
 */
typedef struct {
    esp_capture_audio_src_if_t *audio_src;
    esp_capture_video_src_if_t *video_src;
    esp_capture_handle_t        capture;
    esp_capture_sink_handle_t   video_sink;
    i2c_master_bus_handle_t     sccb_i2c_bus;
    volatile bool capture_initialized;
    volatile bool capture_started;
    volatile bool streaming_enabled;
} av_handles_t;

/**
 * @brief Global AV capture handles
 */
extern av_handles_t av_handles;

/**
 * @brief Global AV capture task handle
 */
extern TaskHandle_t capture_task;

/**
 * @brief Setup audio and video capture
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t capture_setup(void);

/**
 * @brief Start audio and video capture task
 */
void start_capture_task(void);

/**
 * @brief Stop audio and video capture and release resources
 */
void destroy_capture_tasks(void);

/**
 * @brief Stop AV capture task gracefully
 *
 * Signals the task to exit and waits for it to clean up.
 * After this call, start_capture_task() can be called again.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t stop_capture_task(void);

#ifdef __cplusplus
}
#endif

#endif //DOORBELL_VIDEO_SRC_H
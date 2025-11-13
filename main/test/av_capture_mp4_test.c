#include "av_capture_mp4_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "../capture/audio_i2s_src.h"
#include "impl/esp_capture_video_dvp_src.h"
#include "mp4_muxer.h"
#include "esp_audio_enc_default.h"
#include "driver/i2c_master.h"

#include "camera/camera.h"

#define AV_TAG "AV_MP4_TEST"

static const i2c_port_t CAMERA_I2C_PORT = I2C_NUM_0;
static bool camera_i2c_ready = false;

static int mp4_url_pattern(char *file_path, int len, int slice_idx)
{
    if (len <= 0) { return -1; }
    strlcpy(file_path, AV_CAPTURE_MP4_OUTPUT, len);
    return 0;
}

static void av_capture_mp4_task(void *arg)
{
    ESP_LOGI(AV_TAG, "===== ESP_CAPTURE AV -> MP4 TEST START =====");

    static bool encoder_registered = false;
    if (!encoder_registered) {
        esp_audio_err_t reg_ret = esp_audio_enc_register_default();
        if (reg_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(AV_TAG, "Failed to register default audio encoders (%d)", reg_ret);
            vTaskDelete(NULL);
            return;
        }
        encoder_registered = true;
    }

    static bool muxer_registered = false;
    if (!muxer_registered) {
        if (mp4_muxer_register() != ESP_MUXER_ERR_OK) {
            ESP_LOGE(AV_TAG, "Failed to register MP4 muxer");
            vTaskDelete(NULL);
            return;
        }
        muxer_registered = true;
    }

    if (card == NULL) {
        ESP_LOGI(AV_TAG, "SD card not mounted yet, mounting now...");
        if (mount_sd_card() != ESP_OK) {
            ESP_LOGE(AV_TAG, "Failed to mount SD card, aborting test");
            vTaskDelete(NULL);
            return;
        }
    }

    unlink(AV_CAPTURE_MP4_OUTPUT);

    esp_capture_audio_src_if_t *audio_src = NULL;
    esp_capture_video_src_if_t *video_src = NULL;
    esp_capture_handle_t capture = NULL;
    esp_capture_sink_handle_t sink = NULL;
    bool capture_started = false;

    audio_src = esp_capture_new_audio_i2s_src(NULL);
    if (audio_src == NULL) {
        ESP_LOGE(AV_TAG, "Failed to create I2S audio source");
        goto cleanup;
    }

    if (!camera_i2c_ready) {
        i2c_master_bus_config_t i2c_mst_cfg = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = CAMERA_I2C_PORT,
            .scl_io_num = CAM_PIN_SIOC,
            .sda_io_num = CAM_PIN_SIOD,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = 1
        };
        i2c_master_bus_handle_t bus_handle;
        esp_err_t ret = i2c_new_master_bus(&i2c_mst_cfg, &bus_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(AV_TAG, "Failed to install SCCB I2C master bus: %s", esp_err_to_name(ret));
            return;
        }
        camera_i2c_ready = true;
    }

    esp_capture_video_dvp_src_cfg_t vid_cfg = {0};
    vid_cfg.buf_count = AV_CAPTURE_MP4_VIDEO_BUF_COUNT;
    vid_cfg.pwr_pin = CAM_PIN_PWDN;
    vid_cfg.reset_pin = CAM_PIN_RESET;
    vid_cfg.xclk_pin = CAM_PIN_XCLK;
    vid_cfg.xclk_freq = AV_CAPTURE_MP4_VIDEO_XCLK_FREQ;
    vid_cfg.vsync_pin = CAM_PIN_VSYNC;
    vid_cfg.href_pin = CAM_PIN_HREF;
    vid_cfg.pclk_pin = CAM_PIN_PCLK;
    vid_cfg.i2c_port = CAMERA_I2C_PORT;
    vid_cfg.data[0] = CAM_PIN_D0;
    vid_cfg.data[1] = CAM_PIN_D1;
    vid_cfg.data[2] = CAM_PIN_D2;
    vid_cfg.data[3] = CAM_PIN_D3;
    vid_cfg.data[4] = CAM_PIN_D4;
    vid_cfg.data[5] = CAM_PIN_D5;
    vid_cfg.data[6] = CAM_PIN_D6;
    vid_cfg.data[7] = CAM_PIN_D7;

    video_src = esp_capture_new_video_dvp_src(&vid_cfg);
    if (video_src == NULL) {
        ESP_LOGE(AV_TAG, "Failed to create DVP video source");
        goto cleanup;
    }

    esp_capture_cfg_t capture_cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_src = audio_src,
        .video_src = video_src,
    };

    if (esp_capture_open(&capture_cfg, &capture) != ESP_CAPTURE_ERR_OK || capture == NULL) {
        ESP_LOGE(AV_TAG, "Failed to open capture instance");
        goto cleanup;
    }

    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .format_id = ESP_CAPTURE_FMT_ID_AAC,
            .sample_rate = AV_CAPTURE_MP4_AUDIO_SAMPLE_RATE,
            .channel = AV_CAPTURE_MP4_AUDIO_CHANNELS,
            .bits_per_sample = AV_CAPTURE_MP4_AUDIO_BITS,
        },
        .video_info = {
            .format_id = ESP_CAPTURE_FMT_ID_MJPEG,
            .width = AV_CAPTURE_MP4_VIDEO_WIDTH,
            .height = AV_CAPTURE_MP4_VIDEO_HEIGHT,
            .fps = AV_CAPTURE_MP4_VIDEO_FPS,
        },
    };

    if (esp_capture_sink_setup(capture, 0, &sink_cfg, &sink) != ESP_CAPTURE_ERR_OK || sink == NULL) {
        ESP_LOGE(AV_TAG, "Failed to setup capture sink");
        goto cleanup;
    }

    mp4_muxer_config_t mp4_cfg = {
        .base_config = {
            .muxer_type = ESP_MUXER_TYPE_MP4,
            .slice_duration = 0,
            .url_pattern = mp4_url_pattern,
            .ram_cache_size = 16 * 1024,
        },
        .display_in_order = true,
        .moov_before_mdat = true,
    };

    esp_capture_muxer_cfg_t mux_cfg = {
        .base_config = &mp4_cfg.base_config,
        .cfg_size = sizeof(mp4_cfg),
    };

    if (esp_capture_sink_add_muxer(sink, &mux_cfg) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_TAG, "Failed to add MP4 muxer");
        goto cleanup;
    }
    esp_capture_sink_enable_muxer(sink, true);

    esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ALWAYS);

    if (esp_capture_start(capture) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_TAG, "Failed to start AV capture");
        goto cleanup;
    }
    capture_started = true;

    int64_t end_time_us = esp_timer_get_time() + ((int64_t)AV_CAPTURE_MP4_DURATION_SEC * 1000000LL);
    esp_capture_stream_frame_t frame = {0};

    while (esp_timer_get_time() < end_time_us) {
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
        while (esp_capture_sink_acquire_frame(sink, &frame, true) == ESP_CAPTURE_ERR_OK) {
            esp_capture_sink_release_frame(sink, &frame);
        }
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
        while (esp_capture_sink_acquire_frame(sink, &frame, true) == ESP_CAPTURE_ERR_OK) {
            esp_capture_sink_release_frame(sink, &frame);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGI(AV_TAG, "Capture duration reached, stopping...");

cleanup:
    if (capture_started) {
        esp_capture_stop(capture);
    }
    if (capture) {
        esp_capture_close(capture);
    }
    if (audio_src) {
        esp_capture_delete_audio_i2s_src(audio_src);
    }
    if (video_src) {
        free(video_src);
    }

    uint64_t final_size = get_file_size_on_sd(AV_CAPTURE_MP4_OUTPUT);
    if (final_size != (uint64_t)-1) {
        ESP_LOGI(AV_TAG, "MP4 file %s size %llu bytes",
                 AV_CAPTURE_MP4_OUTPUT, (unsigned long long)final_size);
    } else {
        ESP_LOGW(AV_TAG, "MP4 file %s not found", AV_CAPTURE_MP4_OUTPUT);
    }
    get_sd_card_info();
    ESP_LOGI(AV_TAG, "===== ESP_CAPTURE AV -> MP4 TEST END =====");
    vTaskDelete(NULL);
}

void av_capture_mp4_test(void)
{
    const uint32_t stack_words = 12288;
    if (xTaskCreate(av_capture_mp4_task, "av_capture_mp4", stack_words, NULL, 6, NULL) != pdPASS) {
        ESP_LOGE(AV_TAG, "Failed to create AV capture MP4 task");
    }
}

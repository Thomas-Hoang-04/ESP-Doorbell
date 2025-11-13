#include "i2s_capture_esp_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "../capture/audio_i2s_src.h"
#include "esp_audio_enc_default.h"

static const char *ESP_CAP_TAG = "I2S_CAPTURE_ESP";

static void i2s_capture_esp_test_task(void *arg)
{
    ESP_LOGI(ESP_CAP_TAG, "===== ESP_CAPTURE I2S -> AAC TEST START =====");

    static bool encoder_registered = false;
    if (!encoder_registered) {
        esp_audio_err_t reg_ret = esp_audio_enc_register_default();
        if (reg_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(ESP_CAP_TAG, "Failed to register default audio encoders (%d)", reg_ret);
            vTaskDelete(NULL);
            return;
        }
        encoder_registered = true;
    }

    unlink(I2S_CAPTURE_ESP_TEST_OUTPUT);
    FILE *out = fopen(I2S_CAPTURE_ESP_TEST_OUTPUT, "wb");
    if (!out) {
        ESP_LOGE(ESP_CAP_TAG, "Failed to open output file %s", I2S_CAPTURE_ESP_TEST_OUTPUT);
        vTaskDelete(NULL);
        return;
    }

    esp_capture_audio_src_if_t *audio_src = NULL;
    esp_capture_handle_t capture = NULL;
    esp_capture_sink_handle_t sink = NULL;
    bool capture_started = false;

    capture_audio_i2s_src_cfg_t src_cfg = capture_audio_i2s_src_default_config();
    src_cfg.sample_rate_hz = I2S_CAPTURE_ESP_TEST_SAMPLE_RATE;
    src_cfg.channel_count = I2S_CAPTURE_ESP_TEST_CHANNELS;
    src_cfg.bits_per_sample = I2S_CAPTURE_ESP_TEST_BITS_PER_SAMPLE;
    src_cfg.enable_alc = true;
    src_cfg.alc_gain_db = I2S_CAPTURE_ESP_TEST_ALC_GAIN_DB;

    audio_src = esp_capture_new_audio_i2s_src(&src_cfg);
    if (audio_src == NULL) {
        ESP_LOGE(ESP_CAP_TAG, "Failed to create esp_capture I2S source");
        goto cleanup;
    }

    esp_capture_cfg_t cap_cfg = {
        .audio_src = audio_src,
    };
    if (esp_capture_open(&cap_cfg, &capture) != ESP_CAPTURE_ERR_OK || capture == NULL) {
        ESP_LOGE(ESP_CAP_TAG, "Failed to open capture instance");
        goto cleanup;
    }

    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .format_id = ESP_CAPTURE_FMT_ID_AAC,
            .sample_rate = src_cfg.sample_rate_hz,
            .channel = src_cfg.channel_count,
            .bits_per_sample = src_cfg.bits_per_sample,
        },
    };
    if (esp_capture_sink_setup(capture, 0, &sink_cfg, &sink) != ESP_CAPTURE_ERR_OK || sink == NULL) {
        ESP_LOGE(ESP_CAP_TAG, "Failed to setup capture sink");
        goto cleanup;
    }

    esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ALWAYS);

    if (esp_capture_start(capture) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(ESP_CAP_TAG, "Failed to start capture");
        goto cleanup;
    }
    capture_started = true;

    const uint64_t duration_us = (uint64_t)I2S_CAPTURE_ESP_TEST_DURATION_SEC * 1000000ULL;
    const uint64_t end_time_us = esp_timer_get_time() + duration_us;
    size_t total_bytes = 0;
    uint32_t last_pts = 0;

    while (esp_timer_get_time() < end_time_us) {
        esp_capture_stream_frame_t frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
        };
        esp_capture_err_t ret = esp_capture_sink_acquire_frame(sink, &frame, false);
        if (ret == ESP_CAPTURE_ERR_TIMEOUT) {
            continue;
        }
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(ESP_CAP_TAG, "Failed to acquire frame (%d)", ret);
            break;
        }

        if (frame.data && frame.size > 0) {
            size_t written = fwrite(frame.data, 1, frame.size, out);
            if (written != (size_t)frame.size) {
                ESP_LOGW(ESP_CAP_TAG, "Short write: expected %d, wrote %zu", frame.size, written);
            }
            total_bytes += written;
            last_pts = frame.pts;
        }

        esp_capture_sink_release_frame(sink, &frame);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGI(ESP_CAP_TAG, "Capture wrote %zu bytes (last pts %u ms)", total_bytes, last_pts);

cleanup:
    if (capture_started) {
        esp_capture_stop(capture);
    }
    if (capture) {
        esp_capture_close(capture);
    }
    if (audio_src) {
        esp_capture_delete_audio_i2s_src(audio_src);
        audio_src = NULL;
    }
    if (out) {
        fflush(out);
        fclose(out);
    }

    uint64_t final_size = get_file_size_on_sd(I2S_CAPTURE_ESP_TEST_OUTPUT);
    ESP_LOGI(ESP_CAP_TAG, "Output file %s size %llu bytes",
             I2S_CAPTURE_ESP_TEST_OUTPUT, (unsigned long long)final_size);
    get_sd_card_info();
    ESP_LOGI(ESP_CAP_TAG, "===== ESP_CAPTURE I2S -> AAC TEST END =====");
    vTaskDelete(NULL);
}

void i2s_capture_esp_test(void)
{
    const uint32_t stack_words = 8192;
    if (xTaskCreate(i2s_capture_esp_test_task, "i2s_capture_esp", stack_words, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(ESP_CAP_TAG, "Failed to create esp_capture I2S test task");
    }
}

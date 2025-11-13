#include "i2s_aac_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/i2s_std.h"
#include "driver/i2s_common.h"

#include "esp_audio_enc.h"
#include "esp_audio_enc_default.h"
#include "encoder/impl/esp_aac_enc.h"
#include "esp_ae_alc.h"

static const char *I2S_AAC_TAG = "I2S_AAC_TEST";

static void i2s_aac_record_test_body(void)
{
    ESP_LOGI(I2S_AAC_TAG, "===== I2S -> AAC TEST START =====");

    static bool encoder_registered = false;
    if (!encoder_registered) {
        esp_audio_err_t reg_ret = esp_audio_enc_register_default();
        if (reg_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(I2S_AAC_TAG, "Failed to register default audio encoders (%d)", reg_ret);
            return;
        }
        encoder_registered = true;
    }

    i2s_chan_handle_t rx_chan = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_AAC_TEST_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 256;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_AAC_TEST_PIN_MCLK,
        .bclk = I2S_AAC_TEST_PIN_BCLK,
        .ws = I2S_AAC_TEST_PIN_WS,
        .dout = I2S_GPIO_UNUSED,
        .din = I2S_AAC_TEST_PIN_DIN,
        .invert_flags = {0},
    };
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_AAC_TEST_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_AAC_TEST_BITS_PER_SAMPLE, I2S_AAC_TEST_CHANNELS),
        .gpio_cfg = gpio_cfg,
    };
    std_cfg.slot_cfg.slot_mask = I2S_AAC_SLOT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    esp_aac_enc_config_t aac_cfg = ESP_AAC_ENC_CONFIG_DEFAULT();
    aac_cfg.sample_rate = I2S_AAC_TEST_SAMPLE_RATE;
    aac_cfg.channel = I2S_AAC_TEST_CHANNELS;
    aac_cfg.bits_per_sample = I2S_AAC_TEST_BITS_PER_SAMPLE;
    aac_cfg.bitrate = I2S_AAC_TEST_BITRATE;
    aac_cfg.adts_used = true;

    esp_audio_enc_config_t enc_cfg = {
        .type = ESP_AUDIO_TYPE_AAC,
        .cfg = &aac_cfg,
        .cfg_sz = sizeof(aac_cfg),
    };
    esp_audio_enc_handle_t encoder = NULL;
    if (esp_audio_enc_open(&enc_cfg, &encoder) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(I2S_AAC_TAG, "Failed to open AAC encoder");
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan);
        return;
    }

    int in_frame_size = 0;
    int out_frame_size = 0;
    ESP_ERROR_CHECK(esp_audio_enc_get_frame_size(encoder, &in_frame_size, &out_frame_size));

    uint8_t *pcm_buffer = malloc(in_frame_size);
    uint8_t *aac_buffer = malloc(out_frame_size);
    if (!pcm_buffer || !aac_buffer) {
        ESP_LOGE(I2S_AAC_TAG, "Failed to allocate buffers (pcm=%d, aac=%d)", in_frame_size, out_frame_size);
        free(pcm_buffer);
        free(aac_buffer);
        esp_audio_enc_close(encoder);
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan);
        return;
    }

    unlink(I2S_AAC_TEST_OUTPUT);
    FILE *out = fopen(I2S_AAC_TEST_OUTPUT, "wb");
    if (!out) {
        ESP_LOGE(I2S_AAC_TAG, "Failed to open %s", I2S_AAC_TEST_OUTPUT);
        free(pcm_buffer);
        free(aac_buffer);
        esp_audio_enc_close(encoder);
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan);
        return;
    }

    const int bytes_per_sample = (I2S_AAC_TEST_BITS_PER_SAMPLE / 8) * I2S_AAC_TEST_CHANNELS;
    const int samples_per_frame = in_frame_size / bytes_per_sample;
    const int total_samples = I2S_AAC_TEST_SAMPLE_RATE * I2S_AAC_TEST_DURATION_SEC;
    const int total_frames = (total_samples + samples_per_frame - 1) / samples_per_frame;

    ESP_LOGI(I2S_AAC_TAG, "Recording %d s (%d frames, %d samples/frame)", I2S_AAC_TEST_DURATION_SEC,
             total_frames, samples_per_frame);

#if I2S_AAC_TEST_ENABLE_ALC
    esp_ae_alc_handle_t alc_handle = NULL;
    esp_ae_alc_cfg_t alc_cfg = {
        .sample_rate = I2S_AAC_TEST_SAMPLE_RATE,
        .channel = I2S_AAC_TEST_CHANNELS,
        .bits_per_sample = I2S_AAC_TEST_BITS_PER_SAMPLE,
    };
    if (esp_ae_alc_open(&alc_cfg, &alc_handle) != ESP_AE_ERR_OK) {
        ESP_LOGW(I2S_AAC_TAG, "ALC initialization failed; continuing without it");
    } else {
        for (uint8_t ch = 0; ch < I2S_AAC_TEST_CHANNELS; ++ch) {
            esp_ae_err_t gain_ret = esp_ae_alc_set_gain(alc_handle, ch, I2S_AAC_TEST_ALC_GAIN_DB);
            if (gain_ret != ESP_AE_ERR_OK) {
                ESP_LOGW(I2S_AAC_TAG, "ALC gain set failed on ch %u (%d)", ch, gain_ret);
            }
        }
        ESP_LOGI(I2S_AAC_TAG, "ALC enabled with +%d dB static gain per channel", I2S_AAC_TEST_ALC_GAIN_DB);
    }
#endif

    size_t total_output_bytes = 0;
    int64_t start_us = esp_timer_get_time();

    for (int frame_idx = 0; frame_idx < total_frames; ++frame_idx) {
        size_t bytes_read = 0;
        esp_err_t read_ret = i2s_channel_read(rx_chan, pcm_buffer, in_frame_size, &bytes_read, pdMS_TO_TICKS(1000));
        if (read_ret != ESP_OK || bytes_read != in_frame_size) {
            ESP_LOGW(I2S_AAC_TAG, "i2s_channel_read returned %d (bytes=%u)", read_ret, (unsigned)bytes_read);
            memset(pcm_buffer, 0, in_frame_size);
        }

#if I2S_AAC_TEST_ENABLE_ALC
        if (alc_handle) {
            esp_ae_err_t alc_ret = esp_ae_alc_process(alc_handle, samples_per_frame,
                                                      (esp_ae_sample_t)pcm_buffer, (esp_ae_sample_t)pcm_buffer);
            if (alc_ret != ESP_AE_ERR_OK) {
                ESP_LOGW(I2S_AAC_TAG, "ALC process error (%d)", alc_ret);
            }
        }
#endif

        esp_audio_enc_in_frame_t in_frame = {
            .buffer = pcm_buffer,
            .len = in_frame_size,
        };
        esp_audio_enc_out_frame_t out_frame = {
            .buffer = aac_buffer,
            .len = out_frame_size,
        };

        esp_audio_err_t enc_ret = esp_audio_enc_process(encoder, &in_frame, &out_frame);
        if (enc_ret == ESP_AUDIO_ERR_OK) {
            if (out_frame.encoded_bytes > 0) {
                fwrite(aac_buffer, 1, out_frame.encoded_bytes, out);
                total_output_bytes += out_frame.encoded_bytes;
            }
        } else if (enc_ret == ESP_AUDIO_ERR_DATA_LACK) {
            ESP_LOGW(I2S_AAC_TAG, "Encoder requested more data; feeding another chunk");
            frame_idx--;
        } else {
            ESP_LOGE(I2S_AAC_TAG, "Encoder error %d", enc_ret);
            break;
        }
    }

    fflush(out);
    fclose(out);

    double elapsed_s = (double)(esp_timer_get_time() - start_us) / 1e6;
    ESP_LOGI(I2S_AAC_TAG, "Capture complete: %.2f s, %u bytes => %s",
             elapsed_s, (unsigned)total_output_bytes, I2S_AAC_TEST_OUTPUT);

    free(pcm_buffer);
    free(aac_buffer);
    esp_audio_enc_close(encoder);
    i2s_channel_disable(rx_chan);
    i2s_del_channel(rx_chan);

#if I2S_AAC_TEST_ENABLE_ALC
    if (alc_handle) {
        esp_ae_alc_close(alc_handle);
    }
#endif

    get_sd_card_info();
    uint64_t file_size = get_file_size_on_sd(I2S_AAC_TEST_OUTPUT);
    ESP_LOGI(I2S_AAC_TAG, "AAC file size: %lu", file_size);

    ESP_LOGI(I2S_AAC_TAG, "===== I2S -> AAC TEST END =====");
}

static void i2s_aac_record_test_task(void *arg)
{
    i2s_aac_record_test_body();
    vTaskDelete(NULL);
}

void i2s_aac_record_test(void) {
    const uint32_t stack_words = 8192;
    if (xTaskCreate(i2s_aac_record_test_task, "i2s_aac_test", stack_words, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(I2S_AAC_TAG, "Failed to create I2S AAC test task");
    }
}

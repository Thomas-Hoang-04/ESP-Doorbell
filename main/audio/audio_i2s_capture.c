/*
 * I2S audio capture implementation using ESP Capture framework.
 */

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "driver/i2s_common.h"
#include "driver/i2s_std.h"

#include "esp_err.h"
#include "esp_log.h"

#include "audio_i2s_capture.h"
#include "audio_i2s_common.h"
#include "esp_capture_types.h"

static size_t bytes_per_sample(const esp_capture_audio_info_t *info)
{
    return (info->bits_per_sample / 8) * info->channel;
}

static void destroy_alc(audio_i2s_capture_t *ctx)
{
    if (ctx->alc) {
        esp_ae_alc_close(ctx->alc);
        ctx->alc = NULL;
    }
}

static bool caps_supported(const esp_capture_audio_info_t *caps)
{
    const bool format_ok = (caps->format_id == ESP_CAPTURE_FMT_ID_PCM);
    const bool bits_ok = (caps->bits_per_sample == 16);
    const bool channel_ok = (caps->channel == 1 || caps->channel == 2);
    const bool rate_ok = (caps->sample_rate >= 8000 && caps->sample_rate <= 48000);
    return format_ok && bits_ok && channel_ok && rate_ok;
}

audio_i2s_capture_cfg_t audio_i2s_capture_default_config(void)
{
    audio_i2s_capture_cfg_t cfg = {
        .read_timeout_ms = AUDIO_AAC_READ_TIMEOUT_MS,
        .enable_alc = AUDIO_ALC_ENABLE,
        .alc_gain_db = AUDIO_ALC_GAIN_DB,
    };
    return cfg;
}

static esp_capture_err_t i2s_capture_open(esp_capture_audio_src_if_t *h)
{
    audio_i2s_capture_t *ctx = (audio_i2s_capture_t *)h;
    if (ctx == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (ctx->rx) {
        return ESP_CAPTURE_ERR_OK;
    }

    ctx->rx = audio_i2s_common_get_rx_handle();
    if (ctx->rx == NULL) {
        ESP_LOGE(AUDIO_TAG, "I2S common not initialized. Call audio_i2s_common_init() first");
        return ESP_CAPTURE_ERR_NO_RESOURCES;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t i2s_capture_get_codecs(esp_capture_audio_src_if_t *h, const esp_capture_format_id_t **codecs, uint8_t *num)
{
    static const esp_capture_format_id_t supported[] = {ESP_CAPTURE_FMT_ID_PCM};
    *codecs = supported;
    *num = 1;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t i2s_capture_set_fixed_caps(esp_capture_audio_src_if_t *h, const esp_capture_audio_info_t *caps)
{
    audio_i2s_capture_t *ctx = (audio_i2s_capture_t *)h;
    if (!ctx || !caps) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (!caps_supported(caps)) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    ctx->fixed_caps = *caps;
    ctx->fixed_caps.format_id = ESP_CAPTURE_FMT_ID_PCM;
    ctx->fixed_caps_valid = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t i2s_capture_negotiate_caps(esp_capture_audio_src_if_t *h, esp_capture_audio_info_t *wanted, esp_capture_audio_info_t *out)
{
    audio_i2s_capture_t *ctx = (audio_i2s_capture_t *)h;
    if (!ctx || !wanted || !out) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }

    esp_capture_audio_info_t result = ctx->caps;

    if (wanted->format_id && wanted->format_id != ESP_CAPTURE_FMT_ID_PCM && wanted->format_id != ESP_CAPTURE_FMT_ID_ANY) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (wanted->sample_rate) {
        result.sample_rate = wanted->sample_rate;
    }
    if (wanted->channel) {
        result.channel = wanted->channel;
    }

    result.bits_per_sample = AUDIO_I2S_BITS_PER_SAMPLE;
    result.format_id = ESP_CAPTURE_FMT_ID_PCM;

    if (!caps_supported(&result)) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }

    if (ctx->fixed_caps_valid) {
        if (result.sample_rate != ctx->fixed_caps.sample_rate ||
            result.channel != ctx->fixed_caps.channel ||
            result.bits_per_sample != ctx->fixed_caps.bits_per_sample) {
            return ESP_CAPTURE_ERR_NOT_SUPPORTED;
        }
        result = ctx->fixed_caps;
    }

    ctx->caps = result;
    *out = result;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t i2s_capture_start(esp_capture_audio_src_if_t *h)
{
    audio_i2s_capture_t *ctx = (audio_i2s_capture_t *)h;
    if (!ctx) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (ctx->started) {
        return ESP_CAPTURE_ERR_OK;
    }

    esp_capture_err_t err = i2s_capture_open(h);
    if (err != ESP_CAPTURE_ERR_OK) {
        return err;
    }

    destroy_alc(ctx);
    if (ctx->cfg.enable_alc) {
        esp_ae_alc_cfg_t alc_cfg = {
            .sample_rate = ctx->caps.sample_rate,
            .channel = ctx->caps.channel,
            .bits_per_sample = ctx->caps.bits_per_sample,
        };
        if (esp_ae_alc_open(&alc_cfg, &ctx->alc) != ESP_AE_ERR_OK) {
            ESP_LOGW(AUDIO_TAG, "ALC initialization failed; continuing without ALC");
            ctx->alc = NULL;
        } else {
            for (uint8_t ch = 0; ch < ctx->caps.channel; ++ch) {
                esp_ae_err_t gret = esp_ae_alc_set_gain(ctx->alc, ch, ctx->cfg.alc_gain_db);
                if (gret != ESP_AE_ERR_OK) {
                    ESP_LOGW(AUDIO_TAG, "ALC gain set failed on channel %u (%d)", ch, gret);
                }
            }
        }
    }

    ctx->samples = 0;
    ctx->started = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t i2s_capture_read_frame(esp_capture_audio_src_if_t *h, esp_capture_stream_frame_t *frame)
{
    audio_i2s_capture_t *ctx = (audio_i2s_capture_t *)h;
    if (!ctx || !frame || !frame->data) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (!ctx->started) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    if (frame->size == 0) {
        frame->stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
        frame->pts = (uint32_t)((ctx->samples * 1000ULL) / ctx->caps.sample_rate);
        return ESP_CAPTURE_ERR_OK;
    }
    if ((frame->size % bytes_per_sample(&ctx->caps)) != 0) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }

    size_t remaining = frame->size;
    uint8_t *cursor = frame->data;
    const TickType_t timeout_ticks = (ctx->cfg.read_timeout_ms == 0)
                                         ? portMAX_DELAY
                                         : pdMS_TO_TICKS(ctx->cfg.read_timeout_ms);

    while (remaining > 0) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(ctx->rx, cursor, remaining, &bytes_read, timeout_ticks);
        if (ret == ESP_ERR_TIMEOUT) {
            return ESP_CAPTURE_ERR_TIMEOUT;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(AUDIO_TAG, "I2S read error: %s", esp_err_to_name(ret));
            return ESP_CAPTURE_ERR_INTERNAL;
        }
        cursor += bytes_read;
        remaining -= bytes_read;
    }

    const size_t samples_read = frame->size / bytes_per_sample(&ctx->caps);

    if (ctx->alc) {
        esp_ae_err_t alc_ret = esp_ae_alc_process(ctx->alc, samples_read,
                                                  (esp_ae_sample_t)frame->data,
                                                  (esp_ae_sample_t)frame->data);
        if (alc_ret != ESP_AE_ERR_OK) {
            ESP_LOGW(AUDIO_TAG, "ALC process error (%d)", alc_ret);
        }
    }

    const uint64_t pts_samples = ctx->samples;
    ctx->samples += samples_read;

    frame->stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
    frame->pts = (uint32_t)((pts_samples * 1000ULL) / ctx->caps.sample_rate);

    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t i2s_capture_stop(esp_capture_audio_src_if_t *h)
{
    audio_i2s_capture_t *ctx = (audio_i2s_capture_t *)h;
    if (!ctx) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (!ctx->started) {
        return ESP_CAPTURE_ERR_OK;
    }
    ctx->started = false;
    ctx->samples = 0;

    if (ctx->rx) {
        esp_err_t ret = i2s_channel_disable(ctx->rx);
        if (ret != ESP_OK) {
            ESP_LOGW(AUDIO_TAG, "I2S channel disable failed: %s", esp_err_to_name(ret));
        }
    }
    destroy_alc(ctx);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t i2s_capture_close(esp_capture_audio_src_if_t *h)
{
    audio_i2s_capture_t *ctx = (audio_i2s_capture_t *)h;
    if (!ctx) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (ctx->started) {
        i2s_capture_stop(h);
    }
    if (ctx->rx) {
        i2s_del_channel(ctx->rx);
        ctx->rx = NULL;
    }
    destroy_alc(ctx);
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_audio_src_if_t *audio_i2s_capture_new(const audio_i2s_capture_cfg_t *cfg)
{
    // ReSharper disable once CppDFAMemoryLeak
    audio_i2s_capture_t *ctx = calloc(1, sizeof(audio_i2s_capture_t));
    if (ctx == NULL) {
        ESP_LOGE(AUDIO_TAG, "Failed to allocate I2S capture context");
        return NULL;
    }

    audio_i2s_capture_cfg_t defaults = audio_i2s_capture_default_config();
    ctx->cfg = defaults;
    if (cfg) {
        memcpy(&ctx->cfg, cfg, sizeof(audio_i2s_capture_cfg_t));
    }

    if (ctx->cfg.read_timeout_ms == 0) {
        ctx->cfg.read_timeout_ms = defaults.read_timeout_ms;
    }

    if (ctx->cfg.enable_alc && ctx->cfg.alc_gain_db == 0) {
        ctx->cfg.alc_gain_db = defaults.alc_gain_db;
    }

    ctx->caps.format_id = ESP_CAPTURE_FMT_ID_PCM;
    ctx->caps.sample_rate = AUDIO_I2S_SAMPLE_RATE;
    ctx->caps.channel = AUDIO_I2S_CHANNELS;
    ctx->caps.bits_per_sample = AUDIO_I2S_BITS_PER_SAMPLE;

    if (!caps_supported(&ctx->caps)) {
        ESP_LOGE(AUDIO_TAG, "Unsupported I2S capture configuration: %u Hz, %u ch",
                 ctx->caps.sample_rate, ctx->caps.channel);
        free(ctx);
        return NULL;
    }

    ctx->base.open = i2s_capture_open;
    ctx->base.get_support_codecs = i2s_capture_get_codecs;
    ctx->base.set_fixed_caps = i2s_capture_set_fixed_caps;
    ctx->base.negotiate_caps = i2s_capture_negotiate_caps;
    ctx->base.start = i2s_capture_start;
    ctx->base.read_frame = i2s_capture_read_frame;
    ctx->base.stop = i2s_capture_stop;
    ctx->base.close = i2s_capture_close;

    // ReSharper disable once CppDFAMemoryLeak
    return &ctx->base;
}

void audio_i2s_capture_delete(esp_capture_audio_src_if_t *src)
{
    if (src == NULL) {
        return;
    }
    audio_i2s_capture_t *ctx = (audio_i2s_capture_t *)src;
    i2s_capture_close(src);
    free(ctx);
}

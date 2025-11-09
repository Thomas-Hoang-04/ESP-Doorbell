/*
 * Minimal esp_capture audio source that pulls PCM frames straight from an I2S microphone.
 */

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "driver/i2s_common.h"
#include "driver/i2s_std.h"

#include "esp_err.h"
#include "esp_log.h"

#include "audio_i2s_src.h"
#include "esp_capture_types.h"

static size_t bytes_per_sample(const esp_capture_audio_info_t *info)
{
    return (info->bits_per_sample / 8) * info->channel;
}

static void destroy_alc(capture_audio_i2s_src_t *ctx)
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

capture_audio_i2s_src_cfg_t capture_audio_i2s_src_default_config(void)
{
    capture_audio_i2s_src_cfg_t cfg = {
        .port = AUDIO_I2S_PORT,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_PIN_BCK,
            .ws = AUDIO_I2S_PIN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = AUDIO_I2S_PIN_DIN,
            .invert_flags = {
                .bclk_inv = false,
                .mclk_inv = false,
                .ws_inv = false
            }
        },
        .sample_rate_hz = AUDIO_AAC_SAMPLE_RATE_HZ,
        .channel_count = AUDIO_AAC_CHANNELS,
        .bits_per_sample = AUDIO_AAC_BITS,
        .read_timeout_ms = AUDIO_AAC_READ_TIMEOUT_MS,
        .enable_alc = AUDIO_ALC_ENABLE,
        .alc_gain_db = AUDIO_ALC_GAIN_DB,
    };
    return cfg;
}

static esp_capture_err_t i2s_src_open(esp_capture_audio_src_if_t *h)
{
    capture_audio_i2s_src_t *ctx = (capture_audio_i2s_src_t *)h;
    if (ctx == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (ctx->rx) {
        return ESP_CAPTURE_ERR_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ctx->cfg.port, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &ctx->rx);
    if (err != ESP_OK || ctx->rx == NULL) {
        ESP_LOGE(AUDIO_TAG, "New I2S channel failed: %s", esp_err_to_name(err));
        ctx->rx = NULL;
        return ESP_CAPTURE_ERR_NO_RESOURCES;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t i2s_src_get_codecs(esp_capture_audio_src_if_t *h, const esp_capture_format_id_t **codecs, uint8_t *num)
{
    static const esp_capture_format_id_t supported[] = {ESP_CAPTURE_FMT_ID_PCM};
    *codecs = supported;
    *num = 1;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t i2s_src_set_fixed_caps(esp_capture_audio_src_if_t *h, const esp_capture_audio_info_t *caps)
{
    capture_audio_i2s_src_t *ctx = (capture_audio_i2s_src_t *)h;
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

static esp_capture_err_t i2s_src_negotiate_caps(esp_capture_audio_src_if_t *h, esp_capture_audio_info_t *wanted, esp_capture_audio_info_t *out)
{
    capture_audio_i2s_src_t *ctx = (capture_audio_i2s_src_t *)h;
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

    result.bits_per_sample = AUDIO_AAC_BITS;
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
    ctx->cfg.sample_rate_hz = ctx->caps.sample_rate;
    ctx->cfg.channel_count = ctx->caps.channel;
    ctx->cfg.bits_per_sample = ctx->caps.bits_per_sample;
    *out = result;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t i2s_src_start(esp_capture_audio_src_if_t *h)
{
    capture_audio_i2s_src_t *ctx = (capture_audio_i2s_src_t *)h;
    if (!ctx) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (ctx->started) {
        return ESP_CAPTURE_ERR_OK;
    }

    esp_capture_err_t err = i2s_src_open(h);
    if (err != ESP_CAPTURE_ERR_OK) {
        return err;
    }

    const i2s_slot_mode_t slot_mode = (ctx->caps.channel == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ctx->caps.sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, slot_mode),
        .gpio_cfg = ctx->cfg.gpio_cfg,
    };
    std_cfg.slot_cfg.slot_mask = (slot_mode == I2S_SLOT_MODE_MONO) ? I2S_STD_SLOT_LEFT : I2S_STD_SLOT_BOTH;

    esp_err_t ret = i2s_channel_init_std_mode(ctx->rx, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(AUDIO_TAG, "Standard I2S init failed: %s", esp_err_to_name(ret));
        return ESP_CAPTURE_ERR_INTERNAL;
    }

    ret = i2s_channel_enable(ctx->rx);
    if (ret != ESP_OK) {
        ESP_LOGE(AUDIO_TAG, "I2S channel enable failed: %s", esp_err_to_name(ret));
        return ESP_CAPTURE_ERR_INTERNAL;
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

static esp_capture_err_t i2s_src_read_frame(esp_capture_audio_src_if_t *h, esp_capture_stream_frame_t *frame)
{
    capture_audio_i2s_src_t *ctx = (capture_audio_i2s_src_t *)h;
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

static esp_capture_err_t i2s_src_stop(esp_capture_audio_src_if_t *h)
{
    capture_audio_i2s_src_t *ctx = (capture_audio_i2s_src_t *)h;
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

static esp_capture_err_t i2s_src_close(esp_capture_audio_src_if_t *h)
{
    capture_audio_i2s_src_t *ctx = (capture_audio_i2s_src_t *)h;
    if (!ctx) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (ctx->started) {
        i2s_src_stop(h);
    }
    if (ctx->rx) {
        i2s_del_channel(ctx->rx);
        ctx->rx = NULL;
    }
    destroy_alc(ctx);
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_audio_src_if_t *esp_capture_new_audio_i2s_src(const capture_audio_i2s_src_cfg_t *cfg)
{
    // ReSharper disable once CppDFAMemoryLeak
    capture_audio_i2s_src_t *ctx = calloc(1, sizeof(capture_audio_i2s_src_t));
    if (ctx == NULL) {
        ESP_LOGE(AUDIO_TAG, "Failed to allocate I2S capture context");
        return NULL;
    }

    capture_audio_i2s_src_cfg_t defaults = capture_audio_i2s_src_default_config();
    ctx->cfg = defaults;
    if (cfg) {
        memcpy(&ctx->cfg, cfg, sizeof(capture_audio_i2s_src_cfg_t));
    }

    if (ctx->cfg.sample_rate_hz == 0) {
        ctx->cfg.sample_rate_hz = defaults.sample_rate_hz;
    }
    if (ctx->cfg.channel_count == 0) {
        ctx->cfg.channel_count = defaults.channel_count;
    }
    if (ctx->cfg.bits_per_sample == 0) {
        ctx->cfg.bits_per_sample = defaults.bits_per_sample;
    }
    if (ctx->cfg.read_timeout_ms == 0) {
        ctx->cfg.read_timeout_ms = defaults.read_timeout_ms;
    }

    if (ctx->cfg.enable_alc && ctx->cfg.alc_gain_db == 0) {
        ctx->cfg.alc_gain_db = defaults.alc_gain_db;
    }

    ctx->caps.format_id = ESP_CAPTURE_FMT_ID_PCM;
    ctx->caps.sample_rate = ctx->cfg.sample_rate_hz;
    ctx->caps.channel = ctx->cfg.channel_count;
    ctx->caps.bits_per_sample = ctx->cfg.bits_per_sample;

    if (!caps_supported(&ctx->caps)) {
        ESP_LOGE(AUDIO_TAG, "Unsupported I2S capture configuration: %u Hz, %u ch",
                 ctx->caps.sample_rate, ctx->caps.channel);
        free(ctx);
        return NULL;
    }

    ctx->base.open = i2s_src_open;
    ctx->base.get_support_codecs = i2s_src_get_codecs;
    ctx->base.set_fixed_caps = i2s_src_set_fixed_caps;
    ctx->base.negotiate_caps = i2s_src_negotiate_caps;
    ctx->base.start = i2s_src_start;
    ctx->base.read_frame = i2s_src_read_frame;
    ctx->base.stop = i2s_src_stop;
    ctx->base.close = i2s_src_close;

    // ReSharper disable once CppDFAMemoryLeak
    return &ctx->base;
}

void esp_capture_delete_audio_i2s_src(esp_capture_audio_src_if_t *src)
{
    if (src == NULL) {
        return;
    }
    capture_audio_i2s_src_t *ctx = (capture_audio_i2s_src_t *)src;
    i2s_src_close(src);
    free(ctx);
}

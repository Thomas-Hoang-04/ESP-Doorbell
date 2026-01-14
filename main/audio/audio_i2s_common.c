#include "audio_i2s_common.h"
#include "esp_log.h"
#include "driver/i2s_common.h"

typedef struct {
    i2s_chan_handle_t rx;
    i2s_chan_handle_t tx;
    bool rx_initialized;
    bool tx_initialized;
} audio_i2s_common_ctx_t;

static audio_i2s_common_ctx_t s_i2s_ctx = {0};

static esp_err_t init_capture_channel(void)
{
    if (s_i2s_ctx.rx_initialized) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_CAPTURE_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_i2s_ctx.rx);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_I2S_TAG, "Failed to create I2S capture channel: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_CAPTURE_PIN_BCK,
            .ws = AUDIO_I2S_CAPTURE_PIN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = AUDIO_I2S_CAPTURE_PIN_DIN,
            .invert_flags = {
                .bclk_inv = false,
                .mclk_inv = false,
                .ws_inv = false
            }
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    err = i2s_channel_init_std_mode(s_i2s_ctx.rx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_I2S_TAG, "Failed to init RX channel: %s", esp_err_to_name(err));
        i2s_del_channel(s_i2s_ctx.rx);
        s_i2s_ctx.rx = NULL;
        return err;
    }

    err = i2s_channel_enable(s_i2s_ctx.rx);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_I2S_TAG, "Failed to enable RX channel: %s", esp_err_to_name(err));
        i2s_del_channel(s_i2s_ctx.rx);
        s_i2s_ctx.rx = NULL;
        return err;
    }

    s_i2s_ctx.rx_initialized = true;
    ESP_LOGI(AUDIO_I2S_TAG, "I2S capture initialized: %d Hz, %d ch, %d bit (port %d)", 
             AUDIO_I2S_SAMPLE_RATE, AUDIO_I2S_CHANNELS, AUDIO_I2S_BITS_PER_SAMPLE, AUDIO_I2S_CAPTURE_PORT);
    
    return ESP_OK;
}

static esp_err_t init_playback_channel(void)
{
    if (s_i2s_ctx.tx_initialized) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PLAYBACK_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;

    esp_err_t err = i2s_new_channel(&chan_cfg, &s_i2s_ctx.tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_I2S_TAG, "Failed to create I2S playback channel: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_PLAYBACK_PIN_BCK,
            .ws = AUDIO_I2S_PLAYBACK_PIN_WS,
            .dout = AUDIO_I2S_PLAYBACK_PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .bclk_inv = false,
                .mclk_inv = false,
                .ws_inv = false
            }
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    err = i2s_channel_init_std_mode(s_i2s_ctx.tx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_I2S_TAG, "Failed to init TX channel: %s", esp_err_to_name(err));
        i2s_del_channel(s_i2s_ctx.tx);
        s_i2s_ctx.tx = NULL;
        return err;
    }

    err = i2s_channel_enable(s_i2s_ctx.tx);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_I2S_TAG, "Failed to enable TX channel: %s", esp_err_to_name(err));
        i2s_del_channel(s_i2s_ctx.tx);
        s_i2s_ctx.tx = NULL;
        return err;
    }

    s_i2s_ctx.tx_initialized = true;
    ESP_LOGI(AUDIO_I2S_TAG, "I2S playback initialized: %d Hz, %d ch, %d bit (port %d)", 
             AUDIO_I2S_SAMPLE_RATE, AUDIO_I2S_CHANNELS, AUDIO_I2S_BITS_PER_SAMPLE, AUDIO_I2S_PLAYBACK_PORT);
    
    return ESP_OK;
}

esp_err_t audio_i2s_common_init(void)
{
    esp_err_t err = init_capture_channel();
    if (err != ESP_OK) {
        return err;
    }

    err = init_playback_channel();
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

i2s_chan_handle_t audio_i2s_common_get_rx_handle(void)
{
    return s_i2s_ctx.rx;
}

i2s_chan_handle_t audio_i2s_common_get_tx_handle(void)
{
    return s_i2s_ctx.tx;
}

esp_err_t audio_i2s_common_deinit(void)
{
    if (s_i2s_ctx.tx) {
        i2s_channel_disable(s_i2s_ctx.tx);
        i2s_del_channel(s_i2s_ctx.tx);
        s_i2s_ctx.tx = NULL;
        s_i2s_ctx.tx_initialized = false;
    }

    if (s_i2s_ctx.rx) {
        i2s_channel_disable(s_i2s_ctx.rx);
        i2s_del_channel(s_i2s_ctx.rx);
        s_i2s_ctx.rx = NULL;
        s_i2s_ctx.rx_initialized = false;
    }

    ESP_LOGI(AUDIO_I2S_TAG, "I2S deinitialized");
    
    return ESP_OK;
}

bool audio_i2s_common_is_initialized(void)
{
    return s_i2s_ctx.rx_initialized && s_i2s_ctx.tx_initialized;
}


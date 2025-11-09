#pragma once

#include <stdint.h>

#include "esp_ae_alc.h"
#include "driver/i2s_std.h"
#include "esp_capture_audio_src_if.h"

// Default I2S port & pin mapping for audio input
#define AUDIO_I2S_PORT     I2S_NUM_0
#define AUDIO_I2S_PIN_BCK  GPIO_NUM_2
#define AUDIO_I2S_PIN_WS   GPIO_NUM_42
#define AUDIO_I2S_PIN_DIN  GPIO_NUM_41

// AAC config
#define AUDIO_AAC_SAMPLE_RATE_HZ  16000
#define AUDIO_AAC_CHANNELS        I2S_SLOT_MODE_STEREO
#define AUDIO_AAC_BITS            16
#define AUDIO_AAC_READ_TIMEOUT_MS 1000  // ms

// Enable Automatic Level Control (ALC) by default
#define AUDIO_ALC_ENABLE true
#define AUDIO_ALC_GAIN_DB 48  // dB

// Logging tag
#define AUDIO_TAG "CAP_I2S"

/**
 * @brief Configuration for the custom ESP Capture I2S microphone source.
 */
typedef struct {
    i2s_port_t            port;            /*!< I2S peripheral instance */
    i2s_std_gpio_config_t gpio_cfg;        /*!< GPIO assignment (set pins or leave NC) */
    uint32_t              sample_rate_hz;  /*!< Sample rate (Hz) */
    uint8_t               channel_count;   /*!< Channels (1=mono, 2=stereo) */
    uint8_t               bits_per_sample; /*!< Bits per sample (16 supported) */
    uint32_t              read_timeout_ms; /*!< Read timeout for each call */
    bool                  enable_alc;      /*!< Enable Automatic Level Control */
    int8_t                alc_gain_db;     /*!< Static gain (dB) applied via ALC when enabled */
} capture_audio_i2s_src_cfg_t;

/**
 * @brief Internal context for the ESP Capture I2S microphone source.
 */
typedef struct {
    esp_capture_audio_src_if_t  base;
    capture_audio_i2s_src_cfg_t cfg;
    esp_capture_audio_info_t    caps;
    esp_capture_audio_info_t    fixed_caps;
    i2s_chan_handle_t           rx;
    esp_ae_alc_handle_t         alc;
    uint64_t                    samples;
    bool                        fixed_caps_valid;
    bool                        started;
} capture_audio_i2s_src_t;

/**
 * @brief Returns a default configuration tuned for the INMP441 microphone.
 *
 * Default values:
 *   - Port: I2S_NUM_0
 *   - 16 kHz, 16-bit, stereo
 *   - GPIOs left unassigned (GPIO_NUM_NC)
 *   - 20 ms read timeout
 *   - ALC enabled with +24 dB gain
 */
capture_audio_i2s_src_cfg_t capture_audio_i2s_src_default_config(void);

/**
 * @brief Create a new ESP Capture audio source that streams PCM data from an I2S microphone.
 *
 * @param[in] cfg  Optional configuration. Pass NULL to use capture_audio_i2s_src_default_config().
 *
 * @return Pointer to the audio source interface on success, NULL otherwise.
 */
esp_capture_audio_src_if_t *esp_capture_new_audio_i2s_src(const capture_audio_i2s_src_cfg_t *cfg);

/**
 * @brief Destroy a previously created I2S audio source.
 *
 * Calls the internal close routine (if needed) and releases the allocation done
 * by esp_capture_new_audio_i2s_src().
 *
 * @param[in] src  Audio source handle returned by esp_capture_new_audio_i2s_src()
 */
void esp_capture_delete_audio_i2s_src(esp_capture_audio_src_if_t *src);


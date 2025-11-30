#pragma once

#include <stdint.h>
#include "esp_ae_alc.h"
#include "driver/i2s_std.h"
#include "esp_capture_audio_src_if.h"
#include "audio_i2s_common.h"

#define AUDIO_AAC_READ_TIMEOUT_MS 1000

#define AUDIO_ALC_ENABLE true
#define AUDIO_ALC_GAIN_DB 48

#define AUDIO_TAG "CAP_I2S"

/**
 * @brief Configuration for I2S audio capture
 * 
 * @note I2S configuration (port, GPIOs, sample rate) is managed by audio_i2s_common
 */
typedef struct {
    uint32_t read_timeout_ms;
    bool     enable_alc;
    int8_t   alc_gain_db;
} audio_i2s_capture_cfg_t;

/**
 * @brief Internal context for I2S audio capture
 */
typedef struct {
    esp_capture_audio_src_if_t base;
    audio_i2s_capture_cfg_t    cfg;
    esp_capture_audio_info_t   caps;
    esp_capture_audio_info_t   fixed_caps;
    i2s_chan_handle_t          rx;
    esp_ae_alc_handle_t        alc;
    uint64_t                   samples;
    bool                       fixed_caps_valid;
    bool                       started;
} audio_i2s_capture_t;

/**
 * @brief Returns default configuration for audio capture
 *
 * Default values:
 *   - 1000 ms read timeout
 *   - ALC enabled with +48 dB gain
 * 
 * @note I2S configured at 48kHz, 16-bit, stereo via audio_i2s_common
 */
audio_i2s_capture_cfg_t audio_i2s_capture_default_config(void);

/**
 * @brief Create a new I2S audio capture instance
 *
 * @param[in] cfg  Optional configuration. Pass NULL to use audio_i2s_capture_default_config().
 *
 * @return Pointer to the audio source interface on success, NULL otherwise.
 */
esp_capture_audio_src_if_t *audio_i2s_capture_new(const audio_i2s_capture_cfg_t *cfg);

/**
 * @brief Destroy I2S audio capture instance
 *
 * Calls the internal close routine (if needed) and releases allocated resources.
 *
 * @param[in] src  Audio source handle returned by audio_i2s_capture_new()
 */
void audio_i2s_capture_delete(esp_capture_audio_src_if_t *src);


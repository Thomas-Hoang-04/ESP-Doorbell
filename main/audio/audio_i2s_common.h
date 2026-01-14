#ifndef DOORBELL_AUDIO_I2S_COMMON_H
#define DOORBELL_AUDIO_I2S_COMMON_H

#include "driver/i2s_std.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_I2S_CAPTURE_PORT I2S_NUM_0
#define AUDIO_I2S_PLAYBACK_PORT I2S_NUM_1

#define AUDIO_I2S_CAPTURE_PIN_BCK GPIO_NUM_2
#define AUDIO_I2S_CAPTURE_PIN_WS GPIO_NUM_42
#define AUDIO_I2S_CAPTURE_PIN_DIN GPIO_NUM_41

#define AUDIO_I2S_PLAYBACK_PIN_BCK GPIO_NUM_48
#define AUDIO_I2S_PLAYBACK_PIN_WS GPIO_NUM_47
#define AUDIO_I2S_PLAYBACK_PIN_DOUT GPIO_NUM_21

#define AUDIO_I2S_SAMPLE_RATE 48000
#define AUDIO_I2S_BITS_PER_SAMPLE 16
#define AUDIO_I2S_CHANNELS 2

#define AUDIO_I2S_TAG "AUDIO_I2S"

/**
 * @brief Initialize shared I2S peripheral for both capture and playback
 * 
 * Creates both RX and TX channels on I2S_NUM_0 with shared BCLK/WS pins.
 * This function must be called before using audio capture or playback.
 * Subsequent calls are safe (will return ESP_OK if already initialized).
 * 
 * @return 
 *       - ESP_OK on success or if already initialized
 *       - ESP_ERR_NO_MEM if allocation fails
 *       - ESP_FAIL if I2S channel creation or configuration fails
 */
esp_err_t audio_i2s_common_init(void);

/**
 * @brief Get the I2S RX channel handle for audio capture
 * 
 * @note audio_i2s_common_init() must be called first
 * 
 * @return 
 *       - I2S RX channel handle, or NULL if not initialized
 */
i2s_chan_handle_t audio_i2s_common_get_rx_handle(void);

/**
 * @brief Get the I2S TX channel handle for audio playback
 * 
 * @note audio_i2s_common_init() must be called first
 * 
 * @return 
 *       - I2S TX channel handle, or NULL if not initialized
 */
i2s_chan_handle_t audio_i2s_common_get_tx_handle(void);

/**
 * @brief Deinitialize shared I2S peripheral
 * 
 * Disables and deletes both RX and TX channels.
 * After calling this, audio_i2s_common_init() must be called again before use.
 * 
 * @return 
 *       - ESP_OK on success
 *       - ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t audio_i2s_common_deinit(void);

/**
 * @brief Check if I2S common module is initialized
 * 
 * @return 
 *       - true if initialized
 *       - false if not initialized
 */
bool audio_i2s_common_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif


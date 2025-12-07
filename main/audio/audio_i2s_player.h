#ifndef DOORBELL_I2S_PLAYER_H
#define DOORBELL_I2S_PLAYER_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_audio_types.h"
#include "audio_i2s_common.h"
#include "esp_audio_simple_dec.h"
#include "../sd_handler/sd_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_PLAYER_DEFAULT_TIMEOUT_MS 1000
#define AUDIO_PLAYER_TAG "AUDIO_PLAYER"

#define AUDIO_PLAYER_DIR MOUNT_POINT"/audio"

/**
 * @brief Audio player configuration structure
 * 
 * @note I2S configuration (port, GPIOs, sample rate) is managed by audio_i2s_common
 */
typedef struct {
    uint32_t write_timeout_ms;
} audio_i2s_player_cfg_t;

/**
 * @brief Audio player context structure
 */
typedef struct {
    audio_i2s_player_cfg_t cfg;
    i2s_chan_handle_t tx;
    esp_audio_simple_dec_handle_t simple_dec;
    void *opus_dec;
    esp_audio_type_t current_type;
    volatile bool playing;
    SemaphoreHandle_t mutex;
    QueueHandle_t cmd_queue;
    TaskHandle_t task_handle;
} audio_i2s_player_t;

/**
 * @brief Audio player handle (opaque)
 */
typedef audio_i2s_player_t* audio_i2s_player_handle_t;

extern audio_i2s_player_handle_t audio_i2s_player;

/**
 * @brief Get default configuration for audio player
 * 
 * @return audio_i2s_player_cfg_t Default configuration with 1000ms timeout
 */
audio_i2s_player_cfg_t audio_i2s_player_default_config(void);

/**
 * @brief Initialize audio player
 * 
 * Registers audio decoders (AAC and Opus). The I2S peripheral must be
 * initialized separately by calling audio_i2s_common_init() first.
 * 
 * @param[in] cfg Configuration for audio player, or NULL to use defaults
 *
 * @return 
 *       - ESP_OK on success
 *       - ESP_ERR_INVALID_ARG if handle is NULL
 *       - ESP_ERR_NO_MEM if allocation fails
 *       - ESP_FAIL if decoder registration fails
 */
esp_err_t audio_i2s_player_init(const audio_i2s_player_cfg_t *cfg);

/**
 * @brief Play AAC audio file from SD card
 * 
 * @param[in] file_path Path to AAC or M4A file on SD card
 * 
 * @return 
 *       - ESP_OK on success
 *       - ESP_ERR_INVALID_ARG if handle or file_path is NULL
 *       - ESP_ERR_NOT_FOUND if file cannot be opened
 *       - ESP_ERR_NOT_SUPPORTED if file format is not AAC/M4A
 *       - ESP_FAIL on decoding or playback errors
 */
esp_err_t audio_i2s_player_play_file(const char *file_path);

/**
 * @brief Play encoded audio from memory buffer
 * 
 * @param[in] buffer Pointer to encoded audio data
 * @param[in] length Length of encoded audio data in bytes
 * @param[in] audio_type Audio format type (ESP_AUDIO_TYPE_AAC or ESP_AUDIO_TYPE_OPUS)
 * 
 * @return 
 *       - ESP_OK on success
 *       - ESP_ERR_INVALID_ARG if handle or buffer is NULL
 *       - ESP_ERR_NOT_SUPPORTED if audio_type is not supported
 *       - ESP_FAIL on decoding or playback errors
 */
esp_err_t audio_i2s_player_play_buffer(const uint8_t *buffer,
                                        size_t length,
                                        esp_audio_type_t audio_type);

/**
 * @brief Stop current playback
 *
 * @return 
 *       - ESP_OK on success
 *       - ESP_ERR_INVALID_ARG if handle is NULL
 */
esp_err_t audio_i2s_player_stop(void);

/**
 * @brief Request playback by file index via the audio player task.
 *
 * @param[in] file_index Index passed to select_file_to_play().
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the player is not initialized
 *      - ESP_ERR_TIMEOUT if the command queue is full
 */
esp_err_t audio_i2s_player_request_play(int file_index);

/**
 * @brief Request the current playback stop via the audio player task.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the player is not initialized
 *      - ESP_ERR_TIMEOUT if the command queue is full
 */
esp_err_t audio_i2s_player_request_stop(void);

/**
 * @brief Deinitialize audio player and free resources
 *
 * @return 
 *       - ESP_OK on success
 *       - ESP_ERR_INVALID_ARG if handle is NULL
 */
esp_err_t audio_i2s_player_deinit(void);

/**
 * @brief Select a file from SD card to play by index
 *
 * This function retrieves the list of audio files in the AUDIO_PLAYER_DIR
 * directory on the SD card, selects the file at the specified index,
 * and returns its path.
 *
 * @param[in] index Index of the file to select (0-based, max 4)
 *
 * @return
 *       - Pointer to the selected file path string on success
 *       - NULL if index is out of range or an error occurs
 */
char* select_file_to_play(int index);

#ifdef __cplusplus
}
#endif

#endif


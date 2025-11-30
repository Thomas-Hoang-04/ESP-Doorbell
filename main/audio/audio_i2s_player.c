#include "audio_i2s_player.h"
#include "audio_i2s_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2s_common.h"
#include "esp_log.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec.h"
#include "esp_check.h"
#include "esp_opus_dec.h"

#define DECODE_BUFFER_SIZE 4096
#define FILE_READ_CHUNK_SIZE 2048

audio_i2s_player_handle_t audio_i2s_player = NULL;

audio_i2s_player_cfg_t audio_i2s_player_default_config(void)
{
    audio_i2s_player_cfg_t cfg = {
        .write_timeout_ms = AUDIO_PLAYER_DEFAULT_TIMEOUT_MS,
    };
    return cfg;
}

static esp_audio_simple_dec_type_t get_simple_dec_type_from_extension(const char *file_path)
{
    const char *ext = strrchr(file_path, '.');
    if (!ext) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    }
    ext++;
    
    if (strcasecmp(ext, "aac") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    } else if (strcasecmp(ext, "m4a") == 0 || strcasecmp(ext, "mp4") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    }
    
    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}

esp_err_t audio_i2s_player_init(const audio_i2s_player_cfg_t *cfg)
{
    audio_i2s_player_t *player = calloc(1, sizeof(audio_i2s_player_t));
    ESP_RETURN_ON_FALSE(player, ESP_ERR_NO_MEM, AUDIO_PLAYER_TAG, "Failed to allocate player");

    if (cfg) {
        memcpy(&player->cfg, cfg, sizeof(audio_i2s_player_cfg_t));
    } else {
        player->cfg = audio_i2s_player_default_config();
    }

    player->mutex = xSemaphoreCreateMutex();
    if (!player->mutex) {
        ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to create mutex");
        free(player);
        return ESP_FAIL;
    }

    player->tx = audio_i2s_common_get_tx_handle();
    if (!player->tx) {
        ESP_LOGE(AUDIO_PLAYER_TAG, "I2S common not initialized. Call audio_i2s_common_init() first");
        free(player);
        return ESP_FAIL;
    }

    esp_audio_err_t ret = esp_audio_simple_dec_register_default();
    if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_ALREADY_EXIST) {
        ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to register simple decoders: %d", ret);
        free(player);
        return ESP_FAIL;
    }

    ret = esp_opus_dec_register();
    if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_ALREADY_EXIST) {
        ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to register Opus decoder: %d", ret);
        free(player);
        return ESP_FAIL;
    }

    audio_i2s_player = player;
    
    return ESP_OK;
}

esp_err_t audio_i2s_player_play_file(const char *file_path)
{
    ESP_RETURN_ON_FALSE(audio_i2s_player, ESP_ERR_INVALID_ARG, AUDIO_PLAYER_TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(file_path, ESP_ERR_INVALID_ARG, AUDIO_PLAYER_TAG, "Invalid file path");

    audio_i2s_player_t *player = audio_i2s_player;

    if (xSemaphoreTake(player->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(AUDIO_PLAYER_TAG, "Another playback in progress");
        return ESP_ERR_TIMEOUT;
    }

    esp_audio_simple_dec_type_t dec_type = get_simple_dec_type_from_extension(file_path);
    ESP_RETURN_ON_FALSE(dec_type != ESP_AUDIO_SIMPLE_DEC_TYPE_NONE,
                        ESP_ERR_NOT_SUPPORTED, AUDIO_PLAYER_TAG, "Unsupported file format");

    FILE *fp = fopen(file_path, "rb");
    ESP_RETURN_ON_FALSE(fp, ESP_ERR_NOT_FOUND, AUDIO_PLAYER_TAG, "Failed to open file: %s", file_path);

    esp_audio_simple_dec_cfg_t dec_cfg = {
        .dec_type = dec_type,
        .dec_cfg = NULL,
        .cfg_size = 0,
    };

    esp_audio_err_t ret = esp_audio_simple_dec_open(&dec_cfg, &player->simple_dec);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to open simple decoder: %d", ret);
        fclose(fp);
        return ESP_FAIL;
    }

    player->current_type = ESP_AUDIO_TYPE_AAC;
    player->playing = true;

    uint8_t *read_buf = malloc(FILE_READ_CHUNK_SIZE);
    uint8_t *decode_buf = malloc(DECODE_BUFFER_SIZE);
    if (!read_buf || !decode_buf) {
        ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to allocate buffers");
        if (read_buf) free(read_buf);
        if (decode_buf) free(decode_buf);
        esp_audio_simple_dec_close(player->simple_dec);
        player->simple_dec = NULL;
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = ESP_OK;
    bool info_logged = false;

    while (player->playing) {
        size_t bytes_read = fread(read_buf, 1, FILE_READ_CHUNK_SIZE, fp);
        if (bytes_read == 0) {
            break;
        }

        esp_audio_simple_dec_raw_t raw = {
            .buffer = read_buf,
            .len = bytes_read,
            .eos = feof(fp),
            .consumed = 0,
        };

        while (raw.len > 0 && player->playing) {
            esp_audio_simple_dec_out_t out_frame = {
                .buffer = decode_buf,
                .len = DECODE_BUFFER_SIZE,
                .decoded_size = 0,
            };

            ret = esp_audio_simple_dec_process(player->simple_dec, &raw, &out_frame);
            
            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                uint8_t *new_buf = realloc(decode_buf, out_frame.needed_size);
                if (!new_buf) {
                    ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to reallocate decode buffer");
                    result = ESP_ERR_NO_MEM;
                    break;
                }
                decode_buf = new_buf;
                out_frame.buffer = decode_buf;
                out_frame.len = out_frame.needed_size;
                continue;
            }

            if (ret != ESP_AUDIO_ERR_OK) {
                ESP_LOGE(AUDIO_PLAYER_TAG, "Decode failed: %d", ret);
                result = ESP_FAIL;
                break;
            }

            raw.len -= raw.consumed;
            raw.buffer += raw.consumed;

            if (out_frame.decoded_size > 0) {
                if (!info_logged) {
                    esp_audio_simple_dec_info_t info;
                    ret = esp_audio_simple_dec_get_info(player->simple_dec, &info);
                    if (ret == ESP_AUDIO_ERR_OK) {
                        ESP_LOGI(AUDIO_PLAYER_TAG, "Decoded audio: %lu Hz, %d ch, %d bit", 
                                 info.sample_rate, info.channel, info.bits_per_sample);
                        info_logged = true;
                    }
                }

                size_t bytes_written = 0;
                TickType_t timeout = pdMS_TO_TICKS(player->cfg.write_timeout_ms);
                esp_err_t err = i2s_channel_write(player->tx, out_frame.buffer, 
                                                   out_frame.decoded_size, &bytes_written, timeout);
                if (err != ESP_OK) {
                    ESP_LOGE(AUDIO_PLAYER_TAG, "I2S write failed: %s", esp_err_to_name(err));
                    result = err;
                    break;
                }
            }
        }

        if (result != ESP_OK) {
            break;
        }
    }

    free(read_buf);
    free(decode_buf);
    esp_audio_simple_dec_close(player->simple_dec);
    player->simple_dec = NULL;
    fclose(fp);
    player->playing = false;

    xSemaphoreGive(player->mutex);

    return result;
}

esp_err_t audio_i2s_player_play_buffer(const uint8_t *buffer,
                                        size_t length,
                                        esp_audio_type_t audio_type)
{
    ESP_RETURN_ON_FALSE(audio_i2s_player, ESP_ERR_INVALID_ARG, AUDIO_PLAYER_TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(buffer, ESP_ERR_INVALID_ARG, AUDIO_PLAYER_TAG, "Invalid buffer");
    ESP_RETURN_ON_FALSE(length > 0, ESP_ERR_INVALID_ARG, AUDIO_PLAYER_TAG, "Invalid length");

    audio_i2s_player_t *player = audio_i2s_player;
    esp_err_t result = ESP_OK;

    if (xSemaphoreTake(player->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(AUDIO_PLAYER_TAG, "Another playback in progress");
        return ESP_ERR_TIMEOUT;
    }

    if (audio_type == ESP_AUDIO_TYPE_OPUS) {
        esp_opus_dec_cfg_t opus_cfg = {
            .sample_rate = AUDIO_I2S_SAMPLE_RATE,
            .channel = AUDIO_I2S_CHANNELS,
            .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_20_MS,
            .self_delimited = false,
        };

        esp_audio_err_t ret = esp_opus_dec_open(&opus_cfg, sizeof(opus_cfg), &player->opus_dec);
        if (ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to open Opus decoder: %d", ret);
            return ESP_FAIL;
        }

        player->current_type = ESP_AUDIO_TYPE_OPUS;
        player->playing = true;

        uint8_t *decode_buf = malloc(DECODE_BUFFER_SIZE);
        if (!decode_buf) {
            ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to allocate decode buffer");
            esp_opus_dec_close(player->opus_dec);
            player->opus_dec = NULL;
            return ESP_ERR_NO_MEM;
        }

        esp_audio_dec_in_raw_t raw = {
            .buffer = (uint8_t *)buffer,
            .len = length,
            .consumed = 0,
        };

        while (raw.len > 0 && player->playing) {
            esp_audio_dec_out_frame_t out_frame = {
                .buffer = decode_buf,
                .len = DECODE_BUFFER_SIZE,
                .decoded_size = 0,
            };

            esp_audio_dec_info_t dec_info;
            ret = esp_opus_dec_decode(player->opus_dec, &raw, &out_frame, &dec_info);

            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                uint8_t *new_buf = realloc(decode_buf, out_frame.needed_size);
                if (!new_buf) {
                    ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to reallocate decode buffer");
                    result = ESP_ERR_NO_MEM;
                    break;
                }
                decode_buf = new_buf;
                out_frame.buffer = decode_buf;
                out_frame.len = out_frame.needed_size;
                continue;
            }

            if (ret != ESP_AUDIO_ERR_OK) {
                ESP_LOGE(AUDIO_PLAYER_TAG, "Opus decode failed: %d", ret);
                result = ESP_FAIL;
                break;
            }

            raw.len -= raw.consumed;
            raw.buffer += raw.consumed;

            if (out_frame.decoded_size > 0) {
                size_t bytes_written = 0;
                TickType_t timeout = pdMS_TO_TICKS(player->cfg.write_timeout_ms);
                esp_err_t err = i2s_channel_write(player->tx, out_frame.buffer, 
                                                   out_frame.decoded_size, &bytes_written, timeout);
                if (err != ESP_OK) {
                    ESP_LOGE(AUDIO_PLAYER_TAG, "I2S write failed: %s", esp_err_to_name(err));
                    result = err;
                    break;
                }
            }
        }

        free(decode_buf);
        esp_opus_dec_close(player->opus_dec);
        player->opus_dec = NULL;
        player->playing = false;

    } else if (audio_type == ESP_AUDIO_TYPE_AAC) {
        esp_audio_simple_dec_cfg_t dec_cfg = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC,
            .dec_cfg = NULL,
            .cfg_size = 0,
        };

        esp_audio_err_t ret = esp_audio_simple_dec_open(&dec_cfg, &player->simple_dec);
        if (ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to open AAC simple decoder: %d", ret);
            return ESP_FAIL;
        }

        player->current_type = ESP_AUDIO_TYPE_AAC;
        player->playing = true;

        uint8_t *decode_buf = malloc(DECODE_BUFFER_SIZE);
        if (!decode_buf) {
            ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to allocate decode buffer");
            esp_audio_simple_dec_close(player->simple_dec);
            player->simple_dec = NULL;
            return ESP_ERR_NO_MEM;
        }

        bool info_logged = false;
        esp_audio_simple_dec_raw_t raw = {
            .buffer = (uint8_t *)buffer,
            .len = length,
            .eos = true,
            .consumed = 0,
        };

        while (raw.len > 0 && player->playing) {
            esp_audio_simple_dec_out_t out_frame = {
                .buffer = decode_buf,
                .len = DECODE_BUFFER_SIZE,
                .decoded_size = 0,
            };

            ret = esp_audio_simple_dec_process(player->simple_dec, &raw, &out_frame);

            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                uint8_t *new_buf = realloc(decode_buf, out_frame.needed_size);
                if (!new_buf) {
                    ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to reallocate decode buffer");
                    result = ESP_ERR_NO_MEM;
                    break;
                }
                decode_buf = new_buf;
                out_frame.buffer = decode_buf;
                out_frame.len = out_frame.needed_size;
                continue;
            }

            if (ret != ESP_AUDIO_ERR_OK) {
                ESP_LOGE(AUDIO_PLAYER_TAG, "AAC decode failed: %d", ret);
                result = ESP_FAIL;
                break;
            }

            raw.len -= raw.consumed;
            raw.buffer += raw.consumed;

            if (out_frame.decoded_size > 0) {
                if (!info_logged) {
                    esp_audio_simple_dec_info_t info;
                    ret = esp_audio_simple_dec_get_info(player->simple_dec, &info);
                    if (ret == ESP_AUDIO_ERR_OK) {
                        ESP_LOGI(AUDIO_PLAYER_TAG, "Decoded audio: %lu Hz, %d ch, %d bit", 
                                 info.sample_rate, info.channel, info.bits_per_sample);
                        info_logged = true;
                    }
                }

                size_t bytes_written = 0;
                TickType_t timeout = pdMS_TO_TICKS(player->cfg.write_timeout_ms);
                esp_err_t err = i2s_channel_write(player->tx, out_frame.buffer, 
                                                   out_frame.decoded_size, &bytes_written, timeout);
                if (err != ESP_OK) {
                    ESP_LOGE(AUDIO_PLAYER_TAG, "I2S write failed: %s", esp_err_to_name(err));
                    result = err;
                    break;
                }
            }
        }

        free(decode_buf);
        esp_audio_simple_dec_close(player->simple_dec);
        player->simple_dec = NULL;
        player->playing = false;

    } else {
        ESP_LOGE(AUDIO_PLAYER_TAG, "Unsupported audio type: %d", audio_type);
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreGive(player->mutex);

    return result;
}

esp_err_t audio_i2s_player_stop(void)
{
    ESP_RETURN_ON_FALSE(audio_i2s_player, ESP_ERR_INVALID_ARG, AUDIO_PLAYER_TAG, "Invalid handle");

    audio_i2s_player_t *player = audio_i2s_player;
    player->playing = false;

    return ESP_OK;
}

esp_err_t audio_i2s_player_deinit(void)
{
    ESP_RETURN_ON_FALSE(audio_i2s_player, ESP_ERR_INVALID_ARG, AUDIO_PLAYER_TAG, "Invalid handle");

    audio_i2s_player_t *player = audio_i2s_player;

    player->playing = false;

    if (player->simple_dec) {
        esp_audio_simple_dec_close(player->simple_dec);
        player->simple_dec = NULL;
    }

    if (player->opus_dec) {
        esp_opus_dec_close(player->opus_dec);
        player->opus_dec = NULL;
    }

    free(player);

    return ESP_OK;
}

char* select_file_to_play(int index) {
    if (index > 4 || index < 0) {
        ESP_LOGE(AUDIO_PLAYER_TAG, "Index out of range: %d", index);
        return NULL;
    }

    // ReSharper disable once CppReplaceMemsetWithZeroInitialization
    static char _filename[255];
    memset(_filename, 0, sizeof(_filename));
    snprintf(_filename, sizeof(_filename), "%s/bell_%d.aac", AUDIO_PLAYER_DIR, index + 1);

    char* file_path = strdup(_filename);
    if (!file_path) {
        ESP_LOGE(AUDIO_PLAYER_TAG, "Failed to allocate memory for file path");
        return NULL;
    }

    return file_path;
}


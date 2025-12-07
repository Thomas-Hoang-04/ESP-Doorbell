/**
 * @file ws_stream.h
 * @brief WebSocket streaming module for real-time AV transmission
 */

#ifndef DOORBELL_WS_STREAM_H
#define DOORBELL_WS_STREAM_H

#include "esp_err.h"
#include "esp_capture_types.h"
#include <stdint.h>
#include <stdbool.h>

/** @brief Magic number for WebSocket frame header (0x4156 = "AV") */
#define WS_STREAM_MAGIC         0x4156
/** @brief Frame type indicator for video frames */
#define WS_STREAM_TYPE_VIDEO    0x01
/** @brief Frame type indicator for audio frames */
#define WS_STREAM_TYPE_AUDIO    0x02

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WebSocket streaming configuration
 */
typedef struct {
    const char *uri;              /*!< WebSocket server URI (ws:// or wss://) */
    uint16_t video_queue_size;    /*!< Video frame queue depth (default 24) */
    uint16_t audio_queue_size;    /*!< Audio frame queue depth (default 50) */
    uint32_t reconnect_timeout_ms; /*!< Reconnection timeout (default 5000ms) */
    size_t max_frame_size;        /*!< Maximum frame size in bytes (default 128KB) */
} ws_stream_config_t;

/**
 * @brief Initialize WebSocket streaming module
 *
 * @param config Configuration parameters, NULL to use Kconfig defaults
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ws_stream_init(const ws_stream_config_t *config);

/**
 * @brief Enable or disable WebSocket streaming
 *
 * @param enable true to connect and start streaming, false to disconnect
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ws_stream_enable(bool enable);

/**
 * @brief Queue a frame for WebSocket transmission
 *
 * @param type Frame type (audio or video)
 * @param data Pointer to frame data
 * @param size Frame size in bytes
 * @param pts Presentation timestamp in milliseconds
 * @return ESP_OK if queued, ESP_ERR_NO_MEM if queue full (oldest frame dropped)
 */
esp_err_t ws_stream_queue_frame(esp_capture_stream_type_t type, 
                                 const uint8_t *data, size_t size, uint32_t pts);

/**
 * @brief Check WebSocket connection status
 *
 * @return true if connected, false otherwise
 */
bool ws_stream_is_connected(void);

/**
 * @brief Destroy WebSocket streaming module and release resources
 */
void ws_stream_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // DOORBELL_WS_STREAM_H

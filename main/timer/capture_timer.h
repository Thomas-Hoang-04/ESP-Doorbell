#ifndef DOORBELL_CAPTURE_TIMER_H
#define DOORBELL_CAPTURE_TIMER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAPTURE_TIMER_TAG "CAPTURE_TIMER"

/**
 * @brief Initialize the capture timeout timer
 * 
 * Creates a GPTimer configured for one-shot timeout based on 
 * CONFIG_CAPTURE_TIMEOUT_SEC. Timer is created but not started.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t capture_timer_init(void);

/**
 * @brief Start or restart the capture timeout timer
 * 
 * Resets the timer counter and starts counting. When the timeout
 * expires, suspend_capture_task() will be called (deferred to task context).
 * 
 * If streaming is enabled (av_handles.streaming_enabled), this function
 * does nothing as capture runs indefinitely in streaming mode.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t capture_timer_start(void);

/**
 * @brief Stop the capture timeout timer
 * 
 * Stops the timer without triggering the timeout callback.
 * Use this when streaming mode is enabled or capture is stopped manually.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t capture_timer_stop(void);

/**
 * @brief Check if the capture timer is running
 * 
 * @return true if timer is actively counting, false otherwise
 */
bool capture_timer_is_running(void);

/**
 * @brief Deinitialize the capture timer and release resources
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t capture_timer_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // DOORBELL_CAPTURE_TIMER_H

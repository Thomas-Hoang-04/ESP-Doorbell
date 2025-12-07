/**
 * @file heartbeat.h
 * @brief MQTT heartbeat timer using ESP Timer for periodic device health reporting
 */

#ifndef DOORBELL_HEARTBEAT_H
#define DOORBELL_HEARTBEAT_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HEARTBEAT_TAG "HEARTBEAT"

/** @brief Heartbeat interval in microseconds */
#define HEARTBEAT_INTERVAL_US ((uint64_t)CONFIG_MQTT_HEARTBEAT_INTERVAL_SEC * 1000000ULL)

/** @brief Firmware version (hardcoded for now) */
#define FIRMWARE_VERSION "1.0.0"

/**
 * @brief Initialize the heartbeat timer
 * 
 * Creates an ESP Timer configured for periodic heartbeat publishing.
 * Timer is created but not started.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t heartbeat_timer_init(void);

/**
 * @brief Start the heartbeat timer
 * 
 * Begins periodic heartbeat publishing at CONFIG_MQTT_HEARTBEAT_INTERVAL_SEC.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t heartbeat_timer_start(void);

/**
 * @brief Stop the heartbeat timer
 * 
 * Stops periodic heartbeat publishing.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t heartbeat_timer_stop(void);

/**
 * @brief Check if the heartbeat timer is running
 * 
 * @return true if timer is active, false otherwise
 */
bool heartbeat_timer_is_running(void);

/**
 * @brief Deinitialize the heartbeat timer and release resources
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t heartbeat_timer_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // DOORBELL_HEARTBEAT_H

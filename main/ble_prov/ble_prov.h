/**
 * @file ble_prov.h
 * @brief BLE Provisioning public API for ESP32 doorbell
 *
 * This module provides BLE-based WiFi provisioning using NimBLE stack.
 * It allows a mobile app to configure WiFi credentials and device identity
 * via a custom GATT service with secure pairing.
 */

#ifndef BLE_PROV_H
#define BLE_PROV_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Provisioning status values sent via Status characteristic
 */
typedef enum {
    BLE_PROV_STATUS_IDLE = 0,       /**< Waiting for credentials */
    BLE_PROV_STATUS_CONNECTING,     /**< Connecting to WiFi */
    BLE_PROV_STATUS_CONNECTED,      /**< WiFi connected successfully */
    BLE_PROV_STATUS_FAILED,         /**< WiFi connection failed */
    BLE_PROV_STATUS_TIMEOUT,        /**< Provisioning timeout */
} ble_prov_status_t;

/**
 * @brief Callback invoked when WiFi connection is established during provisioning
 */
typedef void (*ble_prov_wifi_connected_cb_t)(void);

/**
 * @brief Initialize BLE provisioning module
 *
 * Initializes NimBLE stack, configures GATT service, and sets up security.
 * Must be called before ble_prov_start().
 *
 * @param on_connected Callback invoked when WiFi connects successfully
 * @return ESP_OK on success
 */
esp_err_t ble_prov_init(ble_prov_wifi_connected_cb_t on_connected);

/**
 * @brief Start BLE advertising for provisioning
 *
 * Starts the NimBLE host task and begins advertising.
 * Device will be discoverable as "DOORBELL_XXXXXX".
 *
 * @return ESP_OK on success
 */
esp_err_t ble_prov_start(void);

/**
 * @brief Stop BLE provisioning and free resources
 *
 * Stops NimBLE stack to free approximately 60KB of RAM.
 * Should be called after successful WiFi connection.
 *
 * @return ESP_OK on success
 */
esp_err_t ble_prov_stop(void);

/**
 * @brief Check if device has been provisioned
 *
 * @return true if WiFi credentials exist in NVS
 */
bool ble_prov_is_provisioned(void);

/**
 * @brief Erase all stored credentials (factory reset)
 *
 * @return ESP_OK on success
 */
esp_err_t ble_prov_reset_credentials(void);

/**
 * @brief Get current provisioning status
 *
 * @return Current status value
 */
ble_prov_status_t ble_prov_get_status(void);

#ifdef __cplusplus
}
#endif

#endif

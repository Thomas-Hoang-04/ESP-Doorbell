/**
 * @file ble_prov_gatt.h
 * @brief GATT service for BLE provisioning
 *
 * Defines the custom provisioning GATT service with characteristics:
 * - SSID (0xFFE1): Write - WiFi network name
 * - Password (0xFFE2): Write, Encrypted - WiFi password
 * - Command (0xFFE3): Write - Trigger actions (0x01=connect, 0x02=reset)
 * - Status (0xFFE4): Read, Notify - Connection status
 * - DeviceID (0xFFE5): Read - ESP32-generated device UUID
 * - DeviceKey (0xFFE6): Read, Notify, Encrypted - Device security key
 * - Model (0xFFE7): Read - Device model (from Kconfig)
 * - FirmwareVersion (0xFFE8): Read - Firmware version (from Kconfig)
 *
 * Provisioning Flow:
 * 1. App reads DeviceID, DeviceKey, Model, FirmwareVersion
 * 2. App sends to backend for verification
 * 3. App writes SSID, Password
 * 4. App writes Command 0x01 to trigger WiFi connection
 * 5. App monitors Status for result
 */

#ifndef BLE_PROV_GATT_H
#define BLE_PROV_GATT_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize GATT server with provisioning service
 *
 * Registers the custom provisioning service and all characteristics.
 *
 * @return ESP_OK on success
 */
esp_err_t ble_prov_gatt_init(void);

/**
 * @brief Update the provisioning status value
 *
 * @param status New status (see ble_prov_status_t)
 */
void ble_prov_gatt_set_status(uint8_t status);

/**
 * @brief Get the current provisioning status value
 *
 * @return Current status (see ble_prov_status_t)
 */
uint8_t ble_prov_gatt_get_status(void);

/**
 * @brief Send status notification to connected client
 *
 * @param conn_handle BLE connection handle
 */
void ble_prov_gatt_notify_status(uint16_t conn_handle);

/**
 * @brief Send device key notification to connected client
 *
 * @param conn_handle BLE connection handle
 */
void ble_prov_gatt_notify_device_key(uint16_t conn_handle);

/**
 * @brief Get current BLE connection handle
 *
 * @return Connection handle or BLE_HS_CONN_HANDLE_NONE if not connected
 */
uint16_t ble_prov_gatt_get_conn_handle(void);

/**
 * @brief Reset GATT state on disconnect
 *
 * Clears pending credentials and subscription flags.
 */
void ble_prov_gatt_reset_state(void);

#ifdef __cplusplus
}
#endif

#endif

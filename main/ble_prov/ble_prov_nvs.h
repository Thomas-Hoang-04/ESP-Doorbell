/**
 * @file ble_prov_nvs.h
 * @brief NVS storage functions for BLE provisioning credentials
 *
 * Handles persistent storage of WiFi credentials (SSID/password),
 * device identity (deviceID), and security key (deviceKey) in
 * ESP32's Non-Volatile Storage.
 */

#ifndef BLE_PROV_NVS_H
#define BLE_PROV_NVS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief NVS namespace for provisioning data */
#define NVS_NAMESPACE "wifi_creds"

/** @brief NVS key names */
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"
#define NVS_KEY_DEVICE_ID "device_id"
#define NVS_KEY_DEVICE_KEY "device_key"
#define NVS_KEY_PROVISIONED "provisioned"

/** @brief Length of device key in bytes (256-bit) */
#define DEVICE_KEY_LENGTH 32

/**
 * @brief Save WiFi credentials to NVS
 *
 * @param ssid Network SSID (max 32 chars)
 * @param password Network password (max 64 chars)
 * @return ESP_OK on success
 */
esp_err_t ble_prov_nvs_save_wifi(const char *ssid, const char *password);

/**
 * @brief Load WiFi credentials from NVS
 *
 * @param ssid Buffer for SSID
 * @param ssid_len Size of SSID buffer
 * @param password Buffer for password
 * @param pass_len Size of password buffer
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not provisioned
 */
esp_err_t ble_prov_nvs_load_wifi(char *ssid, size_t ssid_len, char *password, size_t pass_len);

/**
 * @brief Save device credentials to NVS
 *
 * @param device_id Backend-assigned device ID
 * @param device_key Random device key (DEVICE_KEY_LENGTH bytes)
 * @param key_len Length of device key
 * @return ESP_OK on success
 */
esp_err_t ble_prov_nvs_save_device(const char *device_id, const uint8_t *device_key, size_t key_len);

/**
 * @brief Load device key from NVS
 *
 * @param device_key Buffer for key
 * @param key_len Size of buffer
 * @return ESP_OK on success
 */
esp_err_t ble_prov_nvs_load_device_key(uint8_t *device_key, size_t key_len);

/**
 * @brief Erase all provisioning data from NVS
 *
 * @return ESP_OK on success
 */
esp_err_t ble_prov_nvs_erase(void);

/**
 * @brief Check if device is provisioned
 *
 * @return true if provisioned flag is set in NVS
 */
bool ble_prov_nvs_is_provisioned(void);

#ifdef __cplusplus
}
#endif

#endif

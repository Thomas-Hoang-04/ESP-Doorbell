/**
 * @file wifi.h
 * @brief WiFi Station mode driver for ESP32 doorbell
 */

#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"

#define WIFI_TAG "WIFI"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief WiFi SSID from Kconfig */
#define WIFI_SSID CONFIG_WIFI_SSID
/** @brief WiFi password from Kconfig */
#define WIFI_PASSWORD CONFIG_WIFI_PASSWORD
/** @brief Maximum connection retry count */
#define WIFI_MAXIMUM_RETRY CONFIG_WIFI_MAXIMUM_RETRY

/** @brief WPA3 SAE mode configuration */
#define WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
/** @brief SAE H2E identifier */
#define WIFI_SAE_H2E_IDENTIFIER CONFIG_WIFI_SSID
/** @brief Minimum authentication mode threshold */
#define WIFI_SCAN_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK

/** @brief Event group bit indicating successful WiFi connection */
#define WIFI_CONNECTED BIT0
/** @brief Event group bit indicating WiFi connection failure */
#define WIFI_FAILED BIT1

/**
 * @brief Initialize WiFi in Station mode and connect to configured AP
 * 
 * Configures WiFi with credentials from Kconfig (WIFI_SSID, WIFI_PASSWORD),
 * starts the connection process, and blocks until connected or max retries 
 * exceeded. If connection fails after max retries, the device will restart.
 */
void init_wifi_sta(void);

/**
 * @brief Get current WiFi signal strength (RSSI)
 * 
 * Returns the Received Signal Strength Indicator in dBm.
 * Typical values range from -30 dBm (excellent) to -90 dBm (weak).
 * 
 * @return RSSI value in dBm, or 0 if not connected
 */
int8_t wifi_get_rssi(void);

#ifdef __cplusplus
}
#endif

#endif
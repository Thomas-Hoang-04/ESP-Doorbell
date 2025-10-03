#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"

#define WIFI_TAG "WIFI"

#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASSWORD CONFIG_WIFI_PASSWORD
#define WIFI_MAXIMUM_RETRY CONFIG_WIFI_MAXIMUM_RETRY

#define WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define WIFI_SAE_H2E_IDENTIFIER CONFIG_WIFI_SSID
#define WIFI_SCAN_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK

#define WIFI_CONNECTED BIT0
#define WIFI_FAILED BIT1

void init_wifi_sta(void);

#endif
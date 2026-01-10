#include <stdint.h>
#include <string.h>

#include "wifi.h"
#include "esp_event_base.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"

static EventGroupHandle_t wifi_event_group;

static uint8_t retry_cnt = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_cnt < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            retry_cnt++;
            ESP_LOGI(WIFI_TAG, "WiFi connection failed, retry %d of %d", retry_cnt, WIFI_MAXIMUM_RETRY);
        } else xEventGroupSetBits(wifi_event_group, WIFI_FAILED);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = event_data;
        char ip_buf[IP4ADDR_STRLEN_MAX] = {0};
        ESP_LOGI(WIFI_TAG, "WiFi connected, IP: %s", esp_ip4addr_ntoa(&event->ip_info.ip, ip_buf, IP4ADDR_STRLEN_MAX));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED);
        retry_cnt = 0;
    }
}

void init_wifi_sta(void) {
    wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    esp_event_handler_instance_t wifi_inst_any_id, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifi_inst_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t sta_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .threshold.authmode = WIFI_SCAN_THRESHOLD,
            .failure_retry_cnt = WIFI_MAXIMUM_RETRY,
            .sae_pwe_h2e = WIFI_SAE_MODE,
            .sae_h2e_identifier = WIFI_SAE_H2E_IDENTIFIER,
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW40));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_TAG, "WiFi Station initialized");

    ESP_LOGI(WIFI_TAG, "Waiting for WiFi connection...");
    ESP_LOGI(WIFI_TAG, "Connecting to AP: %s", WIFI_SSID);

    EventBits_t wifi_bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED | WIFI_FAILED, 
        pdFALSE, pdFALSE, portMAX_DELAY);

    if (wifi_bits & WIFI_CONNECTED) {
        ESP_LOGI(WIFI_TAG, "Connected to AP: %s", WIFI_SSID);
    } else if (wifi_bits & WIFI_FAILED) {
        ESP_LOGI(WIFI_TAG, "Failed to connect to AP: %s", WIFI_SSID);
        ESP_LOGI(WIFI_TAG, "Please check your WiFi credentials");
        ESP_LOGI(WIFI_TAG, "Restarting...");
        esp_restart();
    } else {
        ESP_LOGI(WIFI_TAG, "WiFi connection failed. Unknown error");
        ESP_LOGI(WIFI_TAG, "Restarting...");
        esp_restart();
    }

}

int8_t wifi_get_rssi(void) {
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);

    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Failed to get AP info: %s", esp_err_to_name(err));
        return 0;
    }

    return ap_info.rssi;
}

static bool wifi_initialized = false;

static void init_wifi_driver(void) {
    if (wifi_initialized) return;

    wifi_event_group = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    esp_event_handler_instance_t wifi_inst_any_id, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifi_inst_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_got_ip));

    wifi_initialized = true;
}

esp_err_t wifi_connect_with_credentials(const char *ssid, const char *password) {
    init_wifi_driver();

    wifi_config_t sta_cfg = {
        .sta = {
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .failure_retry_cnt = WIFI_MAXIMUM_RETRY,
        }
    };

    // ReSharper disable CppRedundantCastExpression
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_TAG, "Connecting to AP: %s", ssid);

    EventBits_t wifi_bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED | WIFI_FAILED, 
        pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (wifi_bits & WIFI_CONNECTED) {
        ESP_LOGI(WIFI_TAG, "Connected to AP: %s", ssid);
        return ESP_OK;
    }
    if (wifi_bits & WIFI_FAILED) {
        ESP_LOGE(WIFI_TAG, "Failed to connect to AP: %s", ssid);
        return ESP_FAIL;
    }

    ESP_LOGE(WIFI_TAG, "WiFi connection timeout");
    return ESP_ERR_TIMEOUT;
}

extern esp_err_t ble_prov_nvs_load_wifi(char *ssid, size_t ssid_len, char *password, size_t pass_len);

esp_err_t wifi_connect_from_nvs(void) {
    char ssid[33] = {0};
    char password[65] = {0};

    esp_err_t err = ble_prov_nvs_load_wifi(ssid, sizeof(ssid), password, sizeof(password));
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Failed to load WiFi credentials from NVS");
        return err;
    }

    return wifi_connect_with_credentials(ssid, password);
}

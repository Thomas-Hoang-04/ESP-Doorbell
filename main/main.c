#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_interface.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "network/wifi.h"
#include "camera/camera.h"
#include "sd_handler/sd_handler.h"
#include "test/i2s_capture_esp_test.h"
#include "test/av_capture_mp4_test.h"
#include "time/time_sync.h"

#define TAG "MAIN"

void app_main(void)
{
    // Ensure SD is mounted
    ESP_LOGI(TAG, "Mounting SD card...");
    ESP_ERROR_CHECK(mount_sd_card());

    // Initialize NVS
    ESP_LOGI(TAG, "Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize network interface and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(WIFI_TAG, "Initializing WiFi connection...");
    init_wifi_sta();
    ESP_LOGI(WIFI_TAG, "Wi-Fi connection initialized");

    time_sync_init();
    time_set_timezone("UTC-7");
    ESP_ERROR_CHECK(time_sync_wait(30));
}

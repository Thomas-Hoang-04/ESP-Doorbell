#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_interface.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "video/video_capture.h"

#include "network/wifi.h"
#include "sd_handler/sd_handler.h"
#include "time/time_sync.h"
#include "websocket/ws_stream.h"
#include "audio/audio_i2s_common.h"
#include "audio/audio_i2s_player.h"

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

    ESP_LOGI(TAG, "Initializing I2S channel...");
    ESP_ERROR_CHECK(audio_i2s_common_init());
    ESP_LOGI(TAG, "I2S channel initialized");

    ESP_LOGI(TAG, "Initializing Audio Player...");
    ESP_ERROR_CHECK(audio_i2s_player_init(NULL));
    ESP_LOGI(TAG, "Audio Player initialized");

    int64_t suspend_time_us = esp_timer_get_time() + ((int64_t)60 * 1000000LL);
    ESP_LOGI(TAG, "Initializing AV capture...");
    capture_setup();
    
    ESP_LOGI(TAG, "Initializing WebSocket streaming...");
    ESP_ERROR_CHECK(ws_stream_init(NULL));
    
    start_capture_task();
    while (esp_timer_get_time() < suspend_time_us) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    suspend_capture_task();
    vTaskDelay(pdMS_TO_TICKS(100));
    int64_t end_time_us = esp_timer_get_time() + ((int64_t)60 * 1000000LL);
    resume_capture_task();
    while (esp_timer_get_time() < end_time_us) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "Cleaning up WebSocket streaming...");
    ws_stream_destroy();
    
    destroy_capture_tasks();
}

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
#include "network/mqtt.h"
#include "sd_handler/sd_handler.h"
#include "time/time_sync.h"
#include "timer/capture_timer.h"
#include "timer/heartbeat.h"
#include "websocket/ws_stream.h"
#include "audio/audio_i2s_common.h"
#include "audio/audio_i2s_player.h"
#include "gpio/bell_button.h"

#define TAG "MAIN"
#define DEFAULT_BELL_FILE_INDEX 1

static void bell_button_capture_callback(btn_event_t event, void *ctx)
{
    (void)ctx;

    if (event != BELL_PRESS) {
        return;
    }

    ESP_LOGI(TAG, "Bell pressed - starting capture");
    
    // Start capture task FIRST (as per user requirement)
    start_capture_task();
    
    // Start capture timeout timer
    capture_timer_start();
    
    // Play doorbell chime
    esp_err_t err = audio_i2s_player_request_play(DEFAULT_BELL_FILE_INDEX);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to queue bell chime: %s", esp_err_to_name(err));
    }
}

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

    // Initialize MQTT client for stream control
    ESP_LOGI(TAG, "Initializing MQTT...");
    init_mqtt();
    ESP_LOGI(TAG, "MQTT initialized");

    // Initialize heartbeat timer for periodic device health reporting
    ESP_LOGI(TAG, "Initializing heartbeat timer...");
    ESP_ERROR_CHECK(heartbeat_timer_init());
    ESP_ERROR_CHECK(heartbeat_timer_start());
    ESP_LOGI(TAG, "Heartbeat timer started");

    ESP_LOGI(TAG, "Initializing I2S channel...");
    ESP_ERROR_CHECK(audio_i2s_common_init());
    ESP_LOGI(TAG, "I2S channel initialized");

    ESP_LOGI(TAG, "Initializing Audio Player...");
    ESP_ERROR_CHECK(audio_i2s_player_init(NULL));
    ESP_LOGI(TAG, "Audio Player initialized");

    // Initialize capture timeout timer
    ESP_LOGI(TAG, "Initializing capture timer...");
    ESP_ERROR_CHECK(capture_timer_init());

    ESP_LOGI(TAG, "Initializing bell button...");
    ESP_ERROR_CHECK(bell_button_init());
    ESP_ERROR_CHECK(bell_button_register_callback(bell_button_capture_callback, NULL));
    create_bell_button_task();
    ESP_LOGI(TAG, "Bell button ready");

    ESP_LOGI(TAG, "Initializing AV capture...");
    capture_setup();

    ESP_LOGI(TAG, "Initializing WebSocket streaming...");
    ESP_ERROR_CHECK(ws_stream_init(NULL));

    ESP_LOGI(TAG, "System initialization complete");
}



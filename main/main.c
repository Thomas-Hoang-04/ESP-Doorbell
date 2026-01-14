#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
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
#include "timer/heartbeat.h"
#include "websocket/ws_stream.h"
#include "audio/audio_i2s_common.h"
#include "audio/audio_i2s_player.h"
#include "gpio/bell_button.h"
#include "ble_prov/ble_prov.h"
#include "ble_prov/ble_prov_gatt.h"
#include "settings/chime_settings.h"

#define TAG "MAIN"

static void on_wifi_status_change(wifi_connect_status_t status) {
    uint16_t conn_handle = ble_prov_gatt_get_conn_handle();
    
    switch (status) {
        case WIFI_STATUS_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected - notifying BLE client");
            ble_prov_gatt_set_status(BLE_PROV_STATUS_CONNECTED);
            ble_prov_gatt_notify_status(conn_handle);
            vTaskDelay(pdMS_TO_TICKS(500));
            ble_prov_stop();
            break;
        case WIFI_STATUS_WRONG_PASSWORD:
            ESP_LOGW(TAG, "WiFi wrong password - notifying BLE client");
            ble_prov_gatt_set_status(BLE_PROV_STATUS_WRONG_PASSWORD);
            ble_prov_gatt_notify_status(conn_handle);
            break;
        case WIFI_STATUS_FAILED:
            ESP_LOGW(TAG, "WiFi connection failed - notifying BLE client");
            ble_prov_gatt_set_status(BLE_PROV_STATUS_FAILED);
            ble_prov_gatt_notify_status(conn_handle);
            break;
        case WIFI_STATUS_TIMEOUT:
            ESP_LOGW(TAG, "WiFi connection timeout - notifying BLE client");
            ble_prov_gatt_set_status(BLE_PROV_STATUS_TIMEOUT);
            ble_prov_gatt_notify_status(conn_handle);
            break;
    }
}

static void on_wifi_connected(void) {
    ESP_LOGI(TAG, "WiFi connected via provisioning");
}

static void bell_button_capture_callback(btn_event_t event, void *ctx)
{
    (void)ctx;

    if (event != BELL_PRESS) {
        return;
    }

    ESP_LOGI(TAG, "Bell pressed - notifying backend and starting capture");

    mqtt_publish_bell_event();

    start_capture_task();

    int chime_index = chime_settings_get_index() - 1;
    esp_err_t err = audio_i2s_player_request_play(chime_index);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to queue bell chime: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Mounting SD card...");
    ESP_ERROR_CHECK(mount_sd_card());

    ESP_LOGI(TAG, "Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    ESP_LOGI(TAG, "Initializing chime settings...");
    ESP_ERROR_CHECK(chime_settings_init());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (ble_prov_is_provisioned()) {
        ESP_LOGI(TAG, "Device already provisioned, connecting to WiFi from NVS...");
        esp_err_t wifi_err = wifi_connect_from_nvs();
        if (wifi_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to connect from NVS, falling back to Kconfig credentials");
            init_wifi_sta();
        }
    } else {
        ESP_LOGI(TAG, "Device not provisioned, starting BLE provisioning...");
        wifi_set_status_callback(on_wifi_status_change);
        ESP_ERROR_CHECK(ble_prov_init(on_wifi_connected));
        ESP_ERROR_CHECK(ble_prov_start());

        while (!ble_prov_is_provisioned()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        ESP_LOGI(TAG, "Provisioning complete, connecting to WiFi...");
        wifi_connect_from_nvs();
    }
    ESP_LOGI(TAG, "WiFi connection established");

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

    ESP_LOGI(TAG, "Initializing bell button...");
    ESP_ERROR_CHECK(bell_button_init());
    ESP_ERROR_CHECK(bell_button_register_callback(bell_button_capture_callback, NULL));
    create_bell_button_task();
    ESP_LOGI(TAG, "Bell button ready");

    ESP_LOGI(TAG, "Initializing AV capture...");
    capture_setup();

    ESP_LOGI(TAG, "Initializing WebSocket streaming...");
    ESP_ERROR_CHECK(ws_stream_init(NULL));

    ESP_LOGI(TAG, "Starting Always-on Capture...");
    start_capture_task();
    start_file_cleanup_task(AV_CAPTURE_MP4_DIR);

    ESP_LOGI(TAG, "System initialization complete");
}



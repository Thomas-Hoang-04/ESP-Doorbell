#include "esp_err.h"
// #include "esp_http_server.h"
// #include "esp_interface.h"
// #include "nvs_flash.h"
// #include "esp_check.h"
// #include "esp_netif.h"
// #include "esp_log.h"
//
// #include "network/wifi.h"
// #include "camera/camera.h"
#include "sd_handler/sd_handler.h"
#include "test/i2s_capture_esp_test.h"
#include "capture/av_capture_mp4_test.h"

void app_main(void)
{
    // Ensure SD is mounted (helpers also guard internally, but do it once here).
    ESP_ERROR_CHECK(mount_sd_card());
    get_sd_card_info();

    // Kick off the AV MP4 capture test (audio + video via esp_capture).
    av_capture_mp4_test();
    // i2s_capture_esp_test();
    // i2s_aac_record_test();
    // esp_err_t ret = nvs_flash_init();
    // if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    //     ESP_ERROR_CHECK(nvs_flash_erase());
    //     ret = nvs_flash_init();
    // }
    // ESP_ERROR_CHECK(ret);
    //
    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    //
    // ESP_LOGI(WIFI_TAG, "Initializing WiFi connection...");
    // init_wifi_sta();
    // ESP_LOGI(WIFI_TAG, "Wi-Fi connection initialized");
    //
    // ESP_LOGI(CAMERA_TAG, "Initializing camera...");
    // ESP_ERROR_CHECK(camera_init());
    // ESP_LOGI(CAMERA_TAG, "Camera initialized");
    //
    // ESP_LOGI("HTTP", "Starting camera server...");
    // httpd_handle_t server = start_cam_server();
    // if (server == NULL) {
    //     ESP_LOGE("HTTP", "Failed to start camera server");
    //     esp_restart();
    // }
}

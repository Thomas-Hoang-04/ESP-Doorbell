/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_interface.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "network/wifi.h"
#include "camera/camera.h"

static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t ret = ESP_OK;

    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(CAMERA_TAG, "Failed to get frame");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

    ret = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    
    esp_camera_fb_return(fb);
    return ret;
}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t ret = ESP_OK;
    char part_buf[128];

    ret = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    if (ret != ESP_OK) {
        ESP_LOGE(CAMERA_TAG, "Failed to set streaming response type");
        return ret;
    }

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(CAMERA_TAG, "Failed to get frame");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        
        size_t hlen = snprintf(part_buf, sizeof(part_buf), 
            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", 
            fb->len);

        ret = httpd_resp_send_chunk(req, part_buf, hlen);
        if (ret != ESP_OK) {
            ESP_LOGE(CAMERA_TAG, "Failed to send chunk");
            esp_camera_fb_return(fb);
            return ret;
        }

        ret = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        if (ret != ESP_OK) {
            ESP_LOGE(CAMERA_TAG, "Failed to send chunk");
            esp_camera_fb_return(fb);
            return ret;
        }

        ret = httpd_resp_send_chunk(req, "\r\n", 2);
        if (ret != ESP_OK) {
            ESP_LOGE(CAMERA_TAG, "Failed to send chunk");
            esp_camera_fb_return(fb);
            return ret;
        }

        esp_camera_fb_return(fb);

        vTaskDelay(pdMS_TO_TICKS(33)); // 33ms = 30fps
    }

    return ret;
}

httpd_handle_t start_cam_server(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = 8001;
    config.max_open_sockets = 7;
    config.stack_size = 8192;

    ESP_LOGI("HTTP", "Starting HTTP server on port %d", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI("HTTP", "HTTP server started");
        httpd_uri_t cam_uri = {
            .uri = "/capture",
            .method = HTTP_GET,
            .handler = capture_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &cam_uri);

        httpd_uri_t stream_uri = {
            .uri = "/stream",
            .method = HTTP_GET,
            .handler = stream_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &stream_uri);
        
        ESP_LOGI("HTTP", "Camera server started successfully");
    } else {
        ESP_LOGE("HTTP", "Failed to start HTTP server");
        return NULL;
    }

    return server;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(WIFI_TAG, "Initializing WiFi connection...");
    init_wifi_sta();
    ESP_LOGI(WIFI_TAG, "WiFi connection initialized");

    ESP_LOGI(CAMERA_TAG, "Initializing camera...");
    ESP_ERROR_CHECK(camera_init());
    ESP_LOGI(CAMERA_TAG, "Camera initialized");

    ESP_LOGI("HTTP", "Starting camera server...");
    httpd_handle_t server = start_cam_server();
    if (server == NULL) {
        ESP_LOGE("HTTP", "Failed to start camera server");
        esp_restart();
    }
    ESP_LOGI("HTTP", "Camera server started successfully");
}

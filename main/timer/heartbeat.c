/**
 * @file heartbeat.c
 * @brief MQTT heartbeat timer implementation using ESP Timer
 * 
 * Publishes periodic heartbeat messages to the backend containing
 * device health information (battery, signal strength, uptime, etc.)
 */

#include "heartbeat.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cJSON.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "../ble_prov/ble_prov_nvs.h"

#include "../network/mqtt.h"
#include "../network/wifi.h"
#include "../video/video_capture.h"

// ---------------------------------------------------------------------------
// Static Variables
// ---------------------------------------------------------------------------
static esp_timer_handle_t s_heartbeat_timer = NULL;
static volatile bool s_timer_running = false;
static int s_battery_level = 100;

// ---------------------------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------------------------

/**
 * @brief Get mock battery level (random 0-100)
 *
 * TODO: Replace with actual battery monitoring when hardware is available
 */
static int get_battery_level(void) {
    if (random() % 3 == 0) {
        if (s_battery_level > 0) {
            s_battery_level--;
        }
    }

    // Simulate charging/reset when critical
    if (s_battery_level <= 5) {
        s_battery_level = 100;
    }

    return s_battery_level;
}

/**
 * @brief Get device uptime in seconds
 */
static int64_t get_uptime_seconds(void) {
    return esp_timer_get_time() / 1000000;  // Convert microseconds to seconds
}

/**
 * @brief Build heartbeat JSON payload
 * 
 * Creates a JSON string with device status information.
 * Caller is responsible for freeing the returned string.
 * 
 * @return Dynamically allocated JSON string, or NULL on error
 */
static char* build_heartbeat_json(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(HEARTBEAT_TAG, "Failed to create JSON object");
        return NULL;
    }

    // Load device identity from NVS
    char device_id[64] = MQTT_CLIENT_ID;
    uint8_t device_key[DEVICE_KEY_LENGTH] = {0};
    char device_key_hex[DEVICE_KEY_LENGTH * 2 + 1] = {0};
    size_t id_len = sizeof(device_id);
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_str(handle, NVS_KEY_DEVICE_ID, device_id, &id_len);
        if (ble_prov_nvs_load_device_key(device_key, DEVICE_KEY_LENGTH) == ESP_OK) {
            for (int i = 0; i < DEVICE_KEY_LENGTH; i++) {
                snprintf(&device_key_hex[i * 2], 3, "%02x", device_key[i]);
            }
        }
        nvs_close(handle);
    }

    // Device identification
    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddStringToObject(root, "device_key", device_key_hex);
    
    // Timestamp (milliseconds since epoch, or just uptime-based for now)
    cJSON_AddNumberToObject(root, "timestamp", (double)esp_timer_get_time() / 1000);
    
    // Device metrics
    cJSON_AddNumberToObject(root, "battery_level", get_battery_level());
    cJSON_AddNumberToObject(root, "signal_strength", wifi_get_rssi());
    cJSON_AddNumberToObject(root, "uptime", (double)get_uptime_seconds());
    
    // Firmware version
    cJSON_AddStringToObject(root, "fw_ver", FIRMWARE_VERSION);
    
    // Device active status (based on streaming state)
    cJSON_AddBoolToObject(root, "is_active", av_handles.streaming_enabled);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return json_str;
}

// ---------------------------------------------------------------------------
// Timer Callback
// ---------------------------------------------------------------------------

static void heartbeat_timer_callback(void *arg) {
    (void)arg;
    
    ESP_LOGD(HEARTBEAT_TAG, "Heartbeat timer triggered");
    
    char *json_payload = build_heartbeat_json();
    if (!json_payload) {
        ESP_LOGE(HEARTBEAT_TAG, "Failed to build heartbeat JSON");
        return;
    }
    
    esp_err_t err = mqtt_publish_heartbeat(json_payload);
    if (err != ESP_OK) {
        ESP_LOGW(HEARTBEAT_TAG, "Failed to publish heartbeat: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(HEARTBEAT_TAG, "Heartbeat published successfully");
    }
    
    free(json_payload);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t heartbeat_timer_init(void) {
    if (s_heartbeat_timer) {
        ESP_LOGW(HEARTBEAT_TAG, "Heartbeat timer already initialized");
        return ESP_OK;
    }
    
    // Seed random number generator for mock battery level
    srand((unsigned int)esp_timer_get_time());
    
    // Configure ESP Timer
    esp_timer_create_args_t timer_args = {
        .callback = heartbeat_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "heartbeat_timer",
        .skip_unhandled_events = true,
    };
    
    esp_err_t err = esp_timer_create(&timer_args, &s_heartbeat_timer);
    if (err != ESP_OK) {
        ESP_LOGE(HEARTBEAT_TAG, "Failed to create heartbeat timer: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(HEARTBEAT_TAG, "Heartbeat timer initialized (%d second interval)", 
             CONFIG_MQTT_HEARTBEAT_INTERVAL_SEC);
    
    return ESP_OK;
}

esp_err_t heartbeat_timer_start(void) {
    if (!s_heartbeat_timer) {
        ESP_LOGE(HEARTBEAT_TAG, "Heartbeat timer not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_timer_running) {
        ESP_LOGW(HEARTBEAT_TAG, "Heartbeat timer already running");
        return ESP_OK;
    }
    
    esp_err_t err = esp_timer_start_periodic(s_heartbeat_timer, HEARTBEAT_INTERVAL_US);
    if (err != ESP_OK) {
        ESP_LOGE(HEARTBEAT_TAG, "Failed to start heartbeat timer: %s", esp_err_to_name(err));
        return err;
    }
    
    s_timer_running = true;
    ESP_LOGI(HEARTBEAT_TAG, "Heartbeat timer started");
    
    // Send initial heartbeat immediately
    heartbeat_timer_callback(NULL);
    
    return ESP_OK;
}

esp_err_t heartbeat_timer_stop(void) {
    if (!s_heartbeat_timer) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_timer_running) {
        esp_timer_stop(s_heartbeat_timer);
        s_timer_running = false;
        ESP_LOGI(HEARTBEAT_TAG, "Heartbeat timer stopped");
    }
    
    return ESP_OK;
}

bool heartbeat_timer_is_running(void) {
    return s_timer_running;
}

esp_err_t heartbeat_timer_deinit(void) {
    if (!s_heartbeat_timer) {
        return ESP_ERR_INVALID_STATE;
    }
    
    heartbeat_timer_stop();
    
    esp_err_t err = esp_timer_delete(s_heartbeat_timer);
    if (err != ESP_OK) {
        ESP_LOGE(HEARTBEAT_TAG, "Failed to delete heartbeat timer: %s", esp_err_to_name(err));
        return err;
    }
    
    s_heartbeat_timer = NULL;
    ESP_LOGI(HEARTBEAT_TAG, "Heartbeat timer deinitialized");
    
    return ESP_OK;
}

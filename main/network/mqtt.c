#include "mqtt.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include <cJSON.h>

#include "../video/video_capture.h"
#include "../websocket/ws_stream.h"
#include "../ble_prov/ble_prov_nvs.h"
#include "../settings/chime_settings.h"

extern const uint8_t ca_pem_start[]     asm("_binary_ca_pem_start");
extern const uint8_t ca_pem_end[]       asm("_binary_ca_pem_end");
extern const uint8_t esp32_client_pem_start[] asm("_binary_esp32_client_pem_start");
extern const uint8_t esp32_client_pem_end[]   asm("_binary_esp32_client_pem_end");
extern const uint8_t esp32_client_key_start[] asm("_binary_esp32_client_key_start");
extern const uint8_t esp32_client_key_end[]   asm("_binary_esp32_client_key_end");

// Stream Control Handler
static void handle_stream_control(const char *action);

// Settings Command Handler
static void handle_settings_command(const char *json_data, int data_len);

// JSON Parser
static const char* parse_stream_action(const char *json_data, int data_len);

// MQTT Client Handle
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static char s_device_id[64] = {0}; // Default to Kconfig ID

// ---------------------------------------------------------------------------
// Logging Utility
// ---------------------------------------------------------------------------
static void log_nonzero_err(const char* msg, int err_code) {
    if (err_code != ESP_OK)
        ESP_LOGE(MQTT_TAG, "Last error - %s: %d", msg, err_code);
}

// ---------------------------------------------------------------------------
// JSON Parsing - Stream Control Messages
// ---------------------------------------------------------------------------

/**
 * @brief Parse stream control action from JSON payload
 * 
 * Extracts the "action" field from a JSON message.
 * Expected format: {"action": "start_stream"} or {"action": "stop_stream"}
 * 
 * @param json_data Raw JSON string
 * @param data_len Length of the JSON string
 * @return Action string ("start_stream", "stop_stream") or NULL if invalid
 */
static const char* parse_stream_action(const char *json_data, int data_len) {
    if (!json_data || data_len <= 0) {
        ESP_LOGW(MQTT_TAG, "Empty or NULL JSON data");
        return NULL;
    }

    // Create null-terminated copy for parsing
    char *json_copy = strndup(json_data, data_len);
    if (!json_copy) {
        ESP_LOGE(MQTT_TAG, "Failed to allocate memory for JSON parsing");
        return NULL;
    }

    cJSON *root = cJSON_Parse(json_copy);
    free(json_copy);

    if (!root) {
        ESP_LOGW(MQTT_TAG, "Failed to parse JSON: %s", cJSON_GetErrorPtr());
        return NULL;
    }

    // Extract "action" field
    const cJSON *action_item = cJSON_GetObjectItemCaseSensitive(root, "action");
    const char *action = NULL;

    if (cJSON_IsString(action_item) && action_item->valuestring) {
        // Return static strings to avoid memory management issues
        if (strcmp(action_item->valuestring, "start_stream") == 0) {
            action = "start_stream";
        } else if (strcmp(action_item->valuestring, "stop_stream") == 0) {
            action = "stop_stream";
        } else {
            ESP_LOGW(MQTT_TAG, "Unknown action: %s", action_item->valuestring);
        }
    } else {
        ESP_LOGW(MQTT_TAG, "Missing or invalid 'action' field in JSON");
    }

    cJSON_Delete(root);
    return action;
}

// ---------------------------------------------------------------------------
// MQTT Event Handler
// ---------------------------------------------------------------------------
static void mqtt_event_handler(void* args, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGD(MQTT_TAG, "Event dispatched from event loop base %s, event_id %lu", event_base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t) event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(MQTT_TAG, "Establishing connection to MQTT host: %s", MQTT_HOST);
            break;

        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(MQTT_TAG, "Connected to MQTT host: %s", MQTT_HOST);
            char topic[128];
            snprintf(topic, sizeof(topic), CONFIG_MQTT_STREAM_CONTROL_TOPIC, s_device_id);
            msg_id = esp_mqtt_client_subscribe(client, topic, 1);
            ESP_LOGI(MQTT_TAG, "Subscribed to stream control topic: %s, msg_id: %d", topic, msg_id);
            snprintf(topic, sizeof(topic), CONFIG_MQTT_SETTINGS_TOPIC, s_device_id);
            msg_id = esp_mqtt_client_subscribe(client, topic, 1);
            ESP_LOGI(MQTT_TAG, "Subscribed to settings topic: %s, msg_id: %d", topic, msg_id);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(MQTT_TAG, "Disconnected from MQTT host: %s", MQTT_HOST);
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(MQTT_TAG, "Subscribed to topic, msg_id: %d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(MQTT_TAG, "Unsubscribed from topic, msg_id: %d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(MQTT_TAG, "Published to topic, msg_id: %d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(MQTT_TAG, "Received data, msg_id: %d", event->msg_id);
            ESP_LOGI(MQTT_TAG, "Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGD(MQTT_TAG, "Data: %.*s", event->data_len, event->data);
            
            char settings_topic[128];
            snprintf(settings_topic, sizeof(settings_topic), CONFIG_MQTT_SETTINGS_TOPIC, s_device_id);
            if (event->topic_len == (int)strlen(settings_topic) && 
                strncmp(event->topic, settings_topic, event->topic_len) == 0) {
                handle_settings_command(event->data, event->data_len);
            } else {
                const char *action = parse_stream_action(event->data, event->data_len);
                if (action) {
                    handle_stream_control(action);
                }
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_nonzero_err("Last report from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_nonzero_err("Last report from TLS stack", event->error_handle->esp_tls_stack_err);
                log_nonzero_err("Report from transport socket", event->error_handle->esp_transport_sock_errno);
                ESP_LOGE(MQTT_TAG, "Last error msg: %s", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;

        default:
            ESP_LOGI(MQTT_TAG, "Other event detected - event_id: %lu", event_id);
            break;
    }
}

// ---------------------------------------------------------------------------
// MQTT Client Initialization
// ---------------------------------------------------------------------------
esp_mqtt_client_handle_t init_mqtt(void) {
    // Attempt to load device ID from NVS
    char nvs_device_id[64] = {0};
    size_t len = sizeof(nvs_device_id);
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_str(handle, NVS_KEY_DEVICE_ID, nvs_device_id, &len) == ESP_OK) {
            strncpy(s_device_id, nvs_device_id, sizeof(s_device_id) - 1);
            ESP_LOGI(MQTT_TAG, "Loaded device ID from NVS: %s", s_device_id);
        }
        nvs_close(handle);
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = MQTT_HOST,
                .port = MQTT_PORT,
            },
            .verification = {
                .certificate = (const char *)ca_pem_start,
                .skip_cert_common_name_check = false,
            }
        },
        .credentials = {
            .client_id = s_device_id,
            .set_null_client_id = false,
#ifdef CONFIG_MQTT_AUTH_ACCESS_TOKEN
            .username = CONFIG_MQTT_ACCESS_TOKEN,
#else
            .username = CONFIG_MQTT_USERNAME,
            .authentication = {
                .password = CONFIG_MQTT_PASSWORD,
                .certificate = (const char *)esp32_client_pem_start,
                .key = (const char *)esp32_client_key_start,
            }
#endif
        },
        .session = {
            .message_retransmit_timeout = 1000,
            .protocol_ver = MQTT_PROTOCOL_V_5,
        },
        .network = {
            .reconnect_timeout_ms = 5000,
            .timeout_ms = 15000,
            .refresh_connection_after_ms = 5 * 60 * 1000,
        },
        .task = {
            .priority = 7,
            .stack_size = 4096,
        }
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));
    return s_mqtt_client;
}

// ---------------------------------------------------------------------------
// MQTT Client Accessor
// ---------------------------------------------------------------------------
esp_mqtt_client_handle_t get_mqtt_client(void) {
    return s_mqtt_client;
}

// ---------------------------------------------------------------------------
// Heartbeat Publishing
// ---------------------------------------------------------------------------
esp_err_t mqtt_publish_heartbeat(const char *json_payload) {
    if (!s_mqtt_client) {
        ESP_LOGE(MQTT_TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Format topic with device ID
    char topic[128];
    snprintf(topic, sizeof(topic), CONFIG_MQTT_HEARTBEAT_TOPIC, s_device_id);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, json_payload, 0, 1, false);
    if (msg_id < 0) {
        ESP_LOGE(MQTT_TAG, "Failed to publish heartbeat");
        return ESP_FAIL;
    }

    ESP_LOGD(MQTT_TAG, "Heartbeat published to %s, msg_id: %d", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_publish_bell_event(void) {
    if (!s_mqtt_client) {
        ESP_LOGE(MQTT_TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Load device key from NVS
    uint8_t device_key[DEVICE_KEY_LENGTH] = {0};
    char device_key_hex[DEVICE_KEY_LENGTH * 2 + 1] = {0};
    if (ble_prov_nvs_load_device_key(device_key, DEVICE_KEY_LENGTH) == ESP_OK) {
        for (int i = 0; i < DEVICE_KEY_LENGTH; i++) {
            snprintf(&device_key_hex[i * 2], 3, "%02x", device_key[i]);
        }
    }

    // Build JSON payload
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(MQTT_TAG, "Failed to create bell event JSON");
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "device_id", s_device_id);
    cJSON_AddStringToObject(root, "device_key", device_key_hex);
    cJSON_AddNumberToObject(root, "timestamp", (double)(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(root, "event", "bell_pressed");

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!payload) {
        ESP_LOGE(MQTT_TAG, "Failed to serialize bell event JSON");
        return ESP_ERR_NO_MEM;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), CONFIG_MQTT_BELL_EVENT_TOPIC, s_device_id);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, false);
    free(payload);

    if (msg_id < 0) {
        ESP_LOGE(MQTT_TAG, "Failed to publish bell event");
        return ESP_FAIL;
    }

    ESP_LOGI(MQTT_TAG, "Bell event published to %s, msg_id: %d", topic, msg_id);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Stream Control Handler
// ---------------------------------------------------------------------------
static void handle_stream_control(const char *action) {
    if (strcmp(action, "start_stream") == 0) {
        ESP_LOGI(MQTT_TAG, "Stream control: Starting stream");
        
        av_handles.streaming_enabled = true;
        esp_err_t err = ws_stream_enable(true);
        if (err != ESP_OK) {
            ESP_LOGE(MQTT_TAG, "Failed to enable WebSocket: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(MQTT_TAG, "WebSocket streaming enabled");
        }
    } else if (strcmp(action, "stop_stream") == 0) {
        ESP_LOGI(MQTT_TAG, "Stream control: Stopping stream");
        
        av_handles.streaming_enabled = false;
        ws_stream_enable(false);
        
        ESP_LOGI(MQTT_TAG, "Streaming pushed to background (recording continues)");
    }
}

static void handle_settings_command(const char *json_data, int data_len) {
    if (!json_data || data_len <= 0) {
        ESP_LOGW(MQTT_TAG, "Empty settings command");
        return;
    }

    char *json_copy = strndup(json_data, data_len);
    if (!json_copy) {
        ESP_LOGE(MQTT_TAG, "Failed to allocate memory for settings parsing");
        return;
    }

    cJSON *root = cJSON_Parse(json_copy);
    free(json_copy);

    if (!root) {
        ESP_LOGW(MQTT_TAG, "Failed to parse settings JSON");
        return;
    }

    const cJSON *action_item = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (!cJSON_IsString(action_item) || !action_item->valuestring) {
        ESP_LOGW(MQTT_TAG, "Missing 'action' in settings command");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(action_item->valuestring, "set_chime") == 0) {
        const cJSON *chime_item = cJSON_GetObjectItemCaseSensitive(root, "chime_index");
        if (cJSON_IsNumber(chime_item)) {
            int chime_index = chime_item->valueint;
            esp_err_t err = chime_settings_set_index(chime_index);
            if (err == ESP_OK) {
                ESP_LOGI(MQTT_TAG, "Chime index set to: %d", chime_index);
            } else {
                ESP_LOGW(MQTT_TAG, "Failed to set chime index: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGW(MQTT_TAG, "Missing or invalid 'chime_index' in set_chime command");
        }
    } else {
        ESP_LOGW(MQTT_TAG, "Unknown settings action: %s", action_item->valuestring);
    }

    cJSON_Delete(root);
}
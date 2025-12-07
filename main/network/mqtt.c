#include "mqtt.h"
#include "esp_log.h"
#include <string.h>
#include <cJSON.h>

#include "../video/video_capture.h"
#include "../websocket/ws_stream.h"

// Stream Control Handler
static void handle_stream_control(const char *action);

// JSON Parser
static const char* parse_stream_action(const char *json_data, int data_len);

// MQTT Client Handle
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

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
            // Subscribe to stream control topic
            msg_id = esp_mqtt_client_subscribe(client, CONFIG_MQTT_STREAM_CONTROL_TOPIC, 1);
            ESP_LOGI(MQTT_TAG, "Subscribed to stream control topic: %s, msg_id: %d", 
                     CONFIG_MQTT_STREAM_CONTROL_TOPIC, msg_id);
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
            
            // Parse and handle stream control commands
            const char *action = parse_stream_action(event->data, event->data_len);
            if (action) {
                handle_stream_control(action);
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
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = MQTT_HOST,
                .port = MQTT_PORT,
            }
        },
        .credentials = {
            .client_id = MQTT_CLIENT_ID,
            .set_null_client_id = false,
#ifdef CONFIG_MQTT_AUTH_ACCESS_TOKEN
            .username = CONFIG_MQTT_ACCESS_TOKEN,
#else
            .username = CONFIG_MQTT_USERNAME,
            .authentication = {
                .password = CONFIG_MQTT_PASSWORD,
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
    snprintf(topic, sizeof(topic), CONFIG_MQTT_HEARTBEAT_TOPIC, MQTT_CLIENT_ID);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, json_payload, 0, 1, false);
    if (msg_id < 0) {
        ESP_LOGE(MQTT_TAG, "Failed to publish heartbeat");
        return ESP_FAIL;
    }

    ESP_LOGD(MQTT_TAG, "Heartbeat published to %s, msg_id: %d", topic, msg_id);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Stream Control Handler
// ---------------------------------------------------------------------------
static void handle_stream_control(const char *action) {
    if (strcmp(action, "start_stream") == 0) {
        ESP_LOGI(MQTT_TAG, "Stream control: Starting stream");
        
        // Start capture if not already running
        start_capture_task();
        
        // Enable WebSocket streaming
        av_handles.streaming_enabled = true;
        esp_err_t err = ws_stream_enable(true);
        if (err != ESP_OK) {
            ESP_LOGE(MQTT_TAG, "Failed to enable WebSocket: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(MQTT_TAG, "WebSocket streaming enabled");
        }
    } else if (strcmp(action, "stop_stream") == 0) {
        ESP_LOGI(MQTT_TAG, "Stream control: Stopping stream");
        
        // Disable WebSocket streaming
        av_handles.streaming_enabled = false;
        ws_stream_enable(false);
        
        // Stop capture
        suspend_capture_task();
        
        ESP_LOGI(MQTT_TAG, "Streaming stopped");
    }
}
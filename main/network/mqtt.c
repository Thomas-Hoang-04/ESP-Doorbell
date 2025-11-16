#include "mqtt.h"
#include "esp_log.h"

static void log_nonzero_err(const char* msg, int err_code) {
    if (err_code != ESP_OK)
        ESP_LOGE(MQTT_TAG, "Last error - %s: %d", msg, err_code);
}

static void mqtt_event_handler(void* args, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGD(MQTT_TAG, "Event dispatched from event loop base %s, event_id %lu", event_base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t) event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(MQTT_TAG, "Establishing connection to MQTT host: %s", MQTT_HOST);
            break;
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(MQTT_TAG, "Connected to MQTT host: %s", MQTT_HOST);

            // msg_id = esp_mqtt_client_subscribe(client, MQTT_ATTRIBUTE_SUBSCRIBE_TOPIC, 0);
            // ESP_LOGI(MQTT_TAG, "Subscribed to topic: %s, msg_id: %d", MQTT_ATTRIBUTE_SUBSCRIBE_TOPIC, msg_id);

            break;
        }
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
            ESP_LOGI(MQTT_TAG, "Originated topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(MQTT_TAG, "Received data: %.*s", event->data_len, event->data);
            mqtt_recv_msg_t msg = {
                .payload_len = event->data_len,
                .topic_len = event->topic_len,
            };
            asprintf(&msg.topic, "%.*s", event->topic_len, event->topic);
            asprintf(&msg.payload, "%.*s", event->data_len, event->data);
            // xQueueSend(mqtt_recv_msg_queue, &msg, portMAX_DELAY);
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

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));
    return client;
}
/**
 * @file mqtt.h
 * @brief MQTT client for doorbell stream control and heartbeat publishing
 */

#ifndef DOORBELL_MQTT_H
#define DOORBELL_MQTT_H

#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#define MQTT_TAG "MQTT"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Configuration (from Kconfig)
// ---------------------------------------------------------------------------

/** @brief MQTT broker URL from Kconfig */
#define MQTT_HOST CONFIG_MQTT_BROKER_URL
/** @brief MQTT broker port from Kconfig */
#define MQTT_PORT CONFIG_MQTT_BROKER_PORT

// ---------------------------------------------------------------------------
// Message Queue (for received messages)
// ---------------------------------------------------------------------------

/** @brief Queue for received MQTT messages */
extern QueueHandle_t mqtt_recv_msg_queue;

/**
 * @brief Structure for received MQTT messages
 */
typedef struct {
    char *topic;       /**< Topic string (dynamically allocated) */
    char *payload;     /**< Payload string (dynamically allocated) */
    int topic_len;     /**< Length of topic string */
    int payload_len;   /**< Length of payload string */
} mqtt_recv_msg_t;

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

/**
 * @brief Initialize and configure the MQTT client
 *
 * Sets up the MQTT client with broker URL, port, and client ID from Kconfig.
 * Subscribes to stream control topic and handles start_stream/stop_stream commands.
 *
 * @return Handle to the initialized MQTT client
 */
esp_mqtt_client_handle_t init_mqtt(void);

/**
 * @brief Get the MQTT client handle
 * 
 * Returns the initialized MQTT client handle for use by other modules
 * (e.g., heartbeat timer for publishing).
 * 
 * @return MQTT client handle, or NULL if not initialized
 */
esp_mqtt_client_handle_t get_mqtt_client(void);

// ---------------------------------------------------------------------------
// Publishing
// ---------------------------------------------------------------------------

/**
 * @brief Publish a heartbeat message to the backend
 * 
 * Publishes a JSON-formatted heartbeat message to the configured 
 * heartbeat topic (CONFIG_MQTT_HEARTBEAT_TOPIC with device ID).
 * 
 * @param json_payload JSON string containing heartbeat data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_heartbeat(const char *json_payload);

/**
 * @brief Publish a bell press event to the backend
 *
 * Publishes a JSON-formatted bell event to CONFIG_MQTT_BELL_EVENT_TOPIC.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_publish_bell_event(void);

#ifdef __cplusplus
}
#endif

#endif //DOORBELL_MQTT_H
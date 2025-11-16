//
// Created by thomas on 11/16/25.
//

#ifndef DOORBELL_MQTT_H
#define DOORBELL_MQTT_H

#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define MQTT_TAG "MQTT"

#define MQTT_HOST CONFIG_MQTT_BROKER_URL
#define MQTT_PORT CONFIG_MQTT_BROKER_PORT

#define MQTT_CLIENT_ID CONFIG_MQTT_CLIENT_ID

extern QueueHandle_t mqtt_recv_msg_queue;

typedef struct {
    char *topic, *payload;
    int topic_len, payload_len;
} mqtt_recv_msg_t;

/**
 * @brief Initialize and configure the MQTT client.
 *
 * This function sets up the MQTT client with the specified broker URL,
 * port, and client ID. It also configures authentication if required.
 *
 * @return [esp_mqtt_client_handle_t] Handle to the initialized MQTT client.
 */
esp_mqtt_client_handle_t init_mqtt(void);

#endif //DOORBELL_MQTT_H
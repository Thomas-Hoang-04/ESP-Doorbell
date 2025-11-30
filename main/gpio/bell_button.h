#ifndef DOORBELL_BELL_BUTTON_H
#define DOORBELL_BELL_BUTTON_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "driver/gpio.h"

#define BELL_BUTTON_TAG "BELL_BUTTON"

extern gpio_num_t bell_button;

extern QueueHandle_t btn_event_queue;

typedef enum {
    BELL_PRESS
} btn_event_t;

/**
 * @brief Initialize the bell button GPIO and related configurations.
 *
 * This function sets up the GPIO pin for the bell button, configures
 * it as an input, and sets up any necessary interrupts or event handlers.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t bell_button_init(void);

/**
 * @brief Deinitialize the bell button GPIO and related configurations.
 *
 * This function removes the GPIO pin configuration, disables interrupts,
 * and cleans up any associated resources.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t bell_button_deinit(void);

/**
 * @brief Create a FreeRTOS task to handle bell button events.
 *
 * This function creates a task that listens for bell button presses
 * and processes them accordingly.
 */
void create_bell_button_task(void);

#endif //DOORBELL_BELL_BUTTON_H
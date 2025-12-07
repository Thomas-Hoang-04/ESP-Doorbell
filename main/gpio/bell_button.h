/**
 * @file bell_button.h
 * @brief Doorbell button GPIO driver with interrupt handling
 */

#ifndef DOORBELL_BELL_BUTTON_H
#define DOORBELL_BELL_BUTTON_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "driver/gpio.h"

#define BELL_BUTTON_TAG "BELL_BUTTON"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief GPIO pin number for the bell button */
extern gpio_num_t bell_button;

/** @brief Event queue for button press events */
extern QueueHandle_t btn_event_queue;

/**
 * @brief Button event types
 */
typedef enum {
    BELL_PRESS  /**< Button was pressed (momentary) */
} btn_event_t;

/**
 * @brief Callback function type for button events
 * @param event The button event that occurred
 * @param ctx User context pointer
 */
typedef void (*bell_button_callback_t)(btn_event_t event, void *ctx);

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

/**
 * @brief Register a callback to receive button events.
 *
 * The callback is invoked from the internal bell button task context.
 *
 * @param[in] callback Function to call when a button event occurs.
 * @param[in] ctx User context pointer passed back to the callback.
 *
 * @return esp_err_t ESP_OK on success, or ESP_ERR_INVALID_ARG if callback is NULL.
 */
esp_err_t bell_button_register_callback(bell_button_callback_t callback, void *ctx);

#ifdef __cplusplus
}
#endif

#endif //DOORBELL_BELL_BUTTON_H
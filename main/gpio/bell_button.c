#include "bell_button.h"

#include "esp_log.h"
#include "esp_check.h"

gpio_num_t bell_button = GPIO_NUM_1;

QueueHandle_t btn_event_queue = NULL;

static void bell_button_isr_handler(void* arg) {
    // TODO: Implement bell button ISR handler
}

static void bell_button_task(void* arg) {
    btn_event_t event;
    // ReSharper disable once CppDFAEndlessLoop
    while (1) {
        if (xQueueReceive(btn_event_queue, &event, portMAX_DELAY)) {
            switch (event) {
                case BELL_PRESS:
                    ESP_LOGI(BELL_BUTTON_TAG, "Bell button pressed");
                    break;
                default:
                    ESP_LOGI(BELL_BUTTON_TAG, "Bell button unpressed");
                    break;
            }
        }
    }
}

esp_err_t bell_button_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << bell_button),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    ESP_RETURN_ON_ERROR(ret, BELL_BUTTON_TAG, "Failed to configure bell button GPIO: %s", esp_err_to_name(ret));
    ESP_LOGI(BELL_BUTTON_TAG, "Bell button GPIO configured");

    ret = gpio_install_isr_service(0);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(BELL_BUTTON_TAG, "ISR service already installed");
    } else 
        ESP_RETURN_ON_ERROR(ret, BELL_BUTTON_TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
    ESP_LOGI(BELL_BUTTON_TAG, "ISR service installed");

    ret = gpio_isr_handler_add(bell_button, bell_button_isr_handler, NULL);
    ESP_RETURN_ON_ERROR(ret, BELL_BUTTON_TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
    ESP_LOGI(BELL_BUTTON_TAG, "ISR handler added");

    ESP_LOGI(BELL_BUTTON_TAG, "Bell button initialized");

    btn_event_queue = xQueueCreate(10, sizeof(btn_event_t));
    ESP_RETURN_ON_FALSE(btn_event_queue != NULL, ESP_ERR_NO_MEM, BELL_BUTTON_TAG, "Failed to create button event queue");
    ESP_LOGI(BELL_BUTTON_TAG, "Button event queue created");

    return ret;
}

esp_err_t bell_button_deinit(void) {
    esp_err_t ret = gpio_isr_handler_remove(bell_button);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(BELL_BUTTON_TAG, "ISR handler already removed");
    } else 
        ESP_RETURN_ON_ERROR(ret, BELL_BUTTON_TAG, "Failed to remove ISR handler: %s", esp_err_to_name(ret));
    ESP_LOGI(BELL_BUTTON_TAG, "ISR handler removed");

    gpio_reset_pin(bell_button);
    ESP_LOGI(BELL_BUTTON_TAG, "Bell button GPIO reset");

    gpio_uninstall_isr_service();
    ESP_LOGI(BELL_BUTTON_TAG, "ISR service uninstalled");

    ESP_LOGI(BELL_BUTTON_TAG, "Bell button deinitialized");
    return ret;
}

void create_bell_button_task(void) {
    xTaskCreate(bell_button_task, "bell_button_task", 4096, NULL, 10, NULL);
    ESP_LOGI(BELL_BUTTON_TAG, "Bell button task created");
}
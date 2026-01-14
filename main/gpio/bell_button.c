#include "bell_button.h"

#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

gpio_num_t bell_button = GPIO_NUM_1;

QueueHandle_t btn_event_queue = NULL;

static TaskHandle_t s_button_task_handle = NULL;
static bell_button_callback_t s_button_callback = NULL;
static void *s_button_callback_ctx = NULL;

static void bell_button_isr_handler(void* arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    btn_event_t event = BELL_PRESS;
    if (btn_event_queue) {
        xQueueSendFromISR(btn_event_queue, &event, &higher_priority_task_woken);
    }
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void bell_button_task(void* arg)
{
    const TickType_t debounce_ticks = pdMS_TO_TICKS(50);
    TickType_t last_press_tick = 0;
    btn_event_t event;

    // ReSharper disable once CppDFAEndlessLoop
    while (1) {
        if (xQueueReceive(btn_event_queue, &event, portMAX_DELAY)) {
            if (event != BELL_PRESS) {
                continue;
            }

            TickType_t now = xTaskGetTickCount();
            if (now - last_press_tick < debounce_ticks) {
                continue;
            }
            last_press_tick = now;

            ESP_LOGI(BELL_BUTTON_TAG, "Bell button pressed");

            if (s_button_callback) {
                s_button_callback(event, s_button_callback_ctx);
            }
        }
    }
}

esp_err_t bell_button_init(void)
{
    if (!btn_event_queue) {
        btn_event_queue = xQueueCreate(10, sizeof(btn_event_t));
        ESP_RETURN_ON_FALSE(btn_event_queue != NULL, ESP_ERR_NO_MEM, BELL_BUTTON_TAG,
                            "Failed to create button event queue");
        ESP_LOGI(BELL_BUTTON_TAG, "Button event queue created");
    }

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
    } else {
        ESP_RETURN_ON_ERROR(ret, BELL_BUTTON_TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
        ESP_LOGI(BELL_BUTTON_TAG, "ISR service installed");
    }

    ret = gpio_isr_handler_add(bell_button, bell_button_isr_handler, NULL);
    ESP_RETURN_ON_ERROR(ret, BELL_BUTTON_TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
    ESP_LOGI(BELL_BUTTON_TAG, "ISR handler added");

    ESP_LOGI(BELL_BUTTON_TAG, "Bell button initialized");
    return ret;
}

esp_err_t bell_button_deinit(void)
{
    if (s_button_task_handle) {
        vTaskDelete(s_button_task_handle);
        s_button_task_handle = NULL;
    }

    esp_err_t ret = gpio_isr_handler_remove(bell_button);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(BELL_BUTTON_TAG, "ISR handler already removed");
    } else {
        ESP_RETURN_ON_ERROR(ret, BELL_BUTTON_TAG, "Failed to remove ISR handler: %s", esp_err_to_name(ret));
        ESP_LOGI(BELL_BUTTON_TAG, "ISR handler removed");
    }

    gpio_reset_pin(bell_button);
    ESP_LOGI(BELL_BUTTON_TAG, "Bell button GPIO reset");

    gpio_uninstall_isr_service();
    ESP_LOGI(BELL_BUTTON_TAG, "ISR service uninstalled");

    if (btn_event_queue) {
        vQueueDelete(btn_event_queue);
        btn_event_queue = NULL;
    }

    s_button_callback = NULL;
    s_button_callback_ctx = NULL;

    ESP_LOGI(BELL_BUTTON_TAG, "Bell button deinitialized");
    return ret;
}

void create_bell_button_task(void)
{
    if (!btn_event_queue) {
        ESP_LOGE(BELL_BUTTON_TAG, "Button event queue not created");
        return;
    }

    if (s_button_task_handle) {
        ESP_LOGW(BELL_BUTTON_TAG, "Bell button task already running");
        return;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(bell_button_task, "bell_button_task", 4096, NULL, 10, &s_button_task_handle, 1);
    if (ret != pdPASS) {
        ESP_LOGE(BELL_BUTTON_TAG, "Failed to create bell button task");
        s_button_task_handle = NULL;
    } else {
        ESP_LOGI(BELL_BUTTON_TAG, "Bell button task created");
    }
}

esp_err_t bell_button_register_callback(bell_button_callback_t callback, void *ctx)
{
    ESP_RETURN_ON_FALSE(callback, ESP_ERR_INVALID_ARG, BELL_BUTTON_TAG, "Callback cannot be NULL");
    s_button_callback = callback;
    s_button_callback_ctx = ctx;
    return ESP_OK;
}
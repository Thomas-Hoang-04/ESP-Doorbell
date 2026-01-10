#include "capture_timer.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../video/video_capture.h"

#define CAPTURE_TIMEOUT_US ((uint64_t)CONFIG_CAPTURE_TIMEOUT_SEC * 1000000ULL)

static gptimer_handle_t s_capture_timer = NULL;
static TaskHandle_t s_timeout_task = NULL;
static volatile bool s_timer_running = false;

// Task to handle capture timeout (deferred from ISR context)
static void capture_timeout_task(void *arg)
{
    // ReSharper disable once CppDFAEndlessLoop
    while (1) {
        // Wait for notification from timer ISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        // In Always-On mode, we don't need to stop capture.
        // If streaming is enabled (user watching), we don't want to stop that either.
        // So this timer is effectively just for logging or other event triggers.
        ESP_LOGI(CAPTURE_TIMER_TAG, "Capture timer expired (Always-on mode: no action taken)");
        
        // if (!av_handles.streaming_enabled && !always_on) { stop... } <-- Old logic
        
        s_timer_running = false;
    }
}

static bool IRAM_ATTR capture_timer_alarm_callback(gptimer_handle_t timer, 
                                                    const gptimer_alarm_event_data_t *edata, 
                                                    void *user_ctx)
{
    (void)edata;
    (void)user_ctx;

    gptimer_stop(timer);
    BaseType_t high_task_wakeup = pdFALSE;
    
    // Don't trigger if streaming is enabled
    if (av_handles.streaming_enabled) {
        return false;
    }
    
    // Notify the timeout task to handle capture suspension
    if (s_timeout_task) {
        vTaskNotifyGiveFromISR(s_timeout_task, &high_task_wakeup);
    }
    
    return high_task_wakeup == pdTRUE;
}

esp_err_t capture_timer_init(void)
{
    if (s_capture_timer) {
        ESP_LOGW(CAPTURE_TIMER_TAG, "Timer already initialized");
        return ESP_OK;
    }
    
    // Create timeout handler task
    if (xTaskCreate(capture_timeout_task, "cap_timeout", 2048, NULL, 5, &s_timeout_task) != pdPASS) {
        ESP_LOGE(CAPTURE_TIMER_TAG, "Failed to create timeout task");
        return ESP_FAIL;
    }
    
    // Configure GPTimer
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick = 1us
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&timer_config, &s_capture_timer),
                        CAPTURE_TIMER_TAG, "Failed to create GPTimer");
    
    // Register callback
    gptimer_event_callbacks_t timer_cbs = {
        .on_alarm = capture_timer_alarm_callback,
    };
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(s_capture_timer, &timer_cbs, NULL),
                        CAPTURE_TIMER_TAG, "Failed to register callbacks");

    // Configure alarm
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = CAPTURE_TIMEOUT_US,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(s_capture_timer, &alarm_config),
                        CAPTURE_TIMER_TAG, "Failed to set alarm action");
    
    // Enable timer (but don't start yet)
    ESP_RETURN_ON_ERROR(gptimer_enable(s_capture_timer),
                        CAPTURE_TIMER_TAG, "Failed to enable timer");
    
    ESP_LOGI(CAPTURE_TIMER_TAG, "Capture timer initialized (%d second timeout)", 
             CONFIG_CAPTURE_TIMEOUT_SEC);
    
    return ESP_OK;
}

esp_err_t capture_timer_start(void)
{
    if (!s_capture_timer) {
        ESP_LOGE(CAPTURE_TIMER_TAG, "Timer not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Skip timer if streaming is enabled (indefinite mode)
    if (av_handles.streaming_enabled) {
        ESP_LOGI(CAPTURE_TIMER_TAG, "Streaming enabled - timer not started");
        return ESP_OK;
    }
    
    // Reset counter and start
    ESP_RETURN_ON_ERROR(gptimer_set_raw_count(s_capture_timer, 0),
                        CAPTURE_TIMER_TAG, "Failed to reset counter");
    ESP_RETURN_ON_ERROR(gptimer_start(s_capture_timer),
                        CAPTURE_TIMER_TAG, "Failed to start timer");
    
    s_timer_running = true;
    ESP_LOGI(CAPTURE_TIMER_TAG, "Capture timer started (%d seconds)", 
             CONFIG_CAPTURE_TIMEOUT_SEC);
    
    return ESP_OK;
}

esp_err_t capture_timer_stop(void)
{
    if (!s_capture_timer) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_timer_running) {
        gptimer_stop(s_capture_timer);
        s_timer_running = false;
        ESP_LOGI(CAPTURE_TIMER_TAG, "Capture timer stopped");
    }
    
    return ESP_OK;
}

bool capture_timer_is_running(void)
{
    return s_timer_running;
}

esp_err_t capture_timer_deinit(void)
{
    if (!s_capture_timer) {
        return ESP_ERR_INVALID_STATE;
    }
    
    capture_timer_stop();
    
    gptimer_disable(s_capture_timer);
    gptimer_del_timer(s_capture_timer);
    s_capture_timer = NULL;
    
    if (s_timeout_task) {
        vTaskDelete(s_timeout_task);
        s_timeout_task = NULL;
    }
    
    ESP_LOGI(CAPTURE_TIMER_TAG, "Capture timer deinitialized");
    return ESP_OK;
}

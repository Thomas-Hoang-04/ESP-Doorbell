#include "ws_stream.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define TAG "WS_STREAM"

#define WS_TASK_STACK_SIZE      (8 * 1024)
#define WS_TASK_PRIORITY        4
#define WS_SEND_TIMEOUT_MS      2000
#define WS_RECONNECT_MAX_MS     30000
#define DEFAULT_MAX_FRAME_SIZE  (128 * 1024)

typedef struct {
    uint8_t type;
    uint32_t seq_num;
    uint32_t pts;
    size_t size;
    uint8_t *data;
} frame_queue_item_t;

typedef struct {
    esp_websocket_client_handle_t client;
    ws_stream_config_t config;
    
    QueueHandle_t video_queue;
    QueueHandle_t audio_queue;
    
    TaskHandle_t send_task;
    SemaphoreHandle_t queue_sem;
    
    bool enabled;
    bool connected;
    bool running;
    
    uint32_t video_seq;
    uint32_t audio_seq;
    
    uint32_t reconnect_delay_ms;
} ws_stream_handle_t;

static ws_stream_handle_t *g_ws_handle = NULL;

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = event_data;
    ws_stream_handle_t *handle = handler_args;
    
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        handle->connected = true;
        handle->reconnect_delay_ms = handle->config.reconnect_timeout_ms;
        break;
        
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        handle->connected = false;
        break;
        
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error: type=%d", data->error_handle.error_type);
        handle->connected = false;
        break;
        
    default:
        break;
    }
}

static esp_err_t send_frame(ws_stream_handle_t *handle, frame_queue_item_t *item)
{
    uint8_t header[12];
    header[0] = WS_STREAM_MAGIC >> 8 & 0xFF;
    header[1] = WS_STREAM_MAGIC & 0xFF;
    header[2] = item->type;
    header[3] = 0;
    header[4] = item->seq_num >> 24 & 0xFF;
    header[5] = item->seq_num >> 16 & 0xFF;
    header[6] = item->seq_num >> 8 & 0xFF;
    header[7] = item->seq_num & 0xFF;
    header[8] = item->pts >> 24 & 0xFF;
    header[9] = item->pts >> 16 & 0xFF;
    header[10] = item->pts >> 8 & 0xFF;
    header[11] = item->pts & 0xFF;
    
    // ReSharper disable once CppRedundantCastExpression
    int sent = esp_websocket_client_send_bin_partial(handle->client, (const char *)header, sizeof(header),
                                                     pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
    if (sent < 0) {
        handle->connected = false;
        return ESP_FAIL;
    }
    
    sent = esp_websocket_client_send_cont_msg(handle->client, (const char *)item->data, (int)item->size,
                                               pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
    if (sent < 0) {
        handle->connected = false;
        return ESP_FAIL;
    }
    
    sent = esp_websocket_client_send_fin(handle->client, pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
    if (sent < 0) {
        handle->connected = false;
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

static void free_frame_item(frame_queue_item_t *item)
{
    if (item->data) {
        free(item->data);
        item->data = NULL;
    }
}

static void ws_send_task(void *arg)
{
    ws_stream_handle_t *handle = arg;
    frame_queue_item_t item;
    TickType_t last_reconnect = 0;
    
    ESP_LOGI(TAG, "WebSocket send task started");
    
    while (handle->running) {
        if (xSemaphoreTake(handle->queue_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        
        QueueHandle_t queue = NULL;
        if (uxQueueMessagesWaiting(handle->video_queue) > 0) {
            queue = handle->video_queue;
        } else if (uxQueueMessagesWaiting(handle->audio_queue) > 0) {
            queue = handle->audio_queue;
        }
        
        if (!queue) {
            continue;
        }
        
        if (xQueueReceive(queue, &item, 0) != pdTRUE) {
            continue;
        }
        
        if (!handle->enabled) {
            free_frame_item(&item);
            continue;
        }
        
        if (!handle->connected) {
            TickType_t now = xTaskGetTickCount();
            if (now - last_reconnect >= pdMS_TO_TICKS(handle->reconnect_delay_ms)) {
                ESP_LOGI(TAG, "Attempting reconnect...");
                esp_err_t ret = esp_websocket_client_start(handle->client);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Reconnect initiated");
                } else {
                    ESP_LOGE(TAG, "Reconnect failed: %s", esp_err_to_name(ret));
                    handle->reconnect_delay_ms = handle->reconnect_delay_ms * 2 > WS_RECONNECT_MAX_MS ?
                                                  WS_RECONNECT_MAX_MS : handle->reconnect_delay_ms * 2;
                }
                last_reconnect = now;
            }
            
            free_frame_item(&item);
            continue;
        }
        
        esp_err_t ret = send_frame(handle, &item);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send frame");
        }
        
        free_frame_item(&item);
    }
    
    ESP_LOGI(TAG, "WebSocket send task exiting");
    vTaskDelete(NULL);
}

esp_err_t ws_stream_init(const ws_stream_config_t *config)
{
    if (g_ws_handle) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    g_ws_handle = calloc(1, sizeof(ws_stream_handle_t));
    if (!g_ws_handle) {
        return ESP_ERR_NO_MEM;
    }
    
    if (config) {
        g_ws_handle->config = *config;
    } else {
        g_ws_handle->config.uri = CONFIG_WS_STREAM_URI;
        g_ws_handle->config.video_queue_size = CONFIG_WS_STREAM_VIDEO_QUEUE_SIZE;
        g_ws_handle->config.audio_queue_size = CONFIG_WS_STREAM_AUDIO_QUEUE_SIZE;
        g_ws_handle->config.reconnect_timeout_ms = CONFIG_WS_STREAM_RECONNECT_TIMEOUT_MS;
        g_ws_handle->config.max_frame_size = DEFAULT_MAX_FRAME_SIZE;
    }
    
    g_ws_handle->video_queue = xQueueCreate(g_ws_handle->config.video_queue_size, sizeof(frame_queue_item_t));
    g_ws_handle->audio_queue = xQueueCreate(g_ws_handle->config.audio_queue_size, sizeof(frame_queue_item_t));
    g_ws_handle->queue_sem = xSemaphoreCreateBinary();
    
    if (!g_ws_handle->video_queue || !g_ws_handle->audio_queue || !g_ws_handle->queue_sem) {
        ws_stream_destroy();
        return ESP_ERR_NO_MEM;
    }
    
    esp_websocket_client_config_t ws_cfg = {
        .uri = g_ws_handle->config.uri,
        .task_stack = WS_TASK_STACK_SIZE,
        .buffer_size = CONFIG_WS_BUFFER_SIZE,
        .network_timeout_ms = 10000,
        .reconnect_timeout_ms = (int)g_ws_handle->config.reconnect_timeout_ms,
        .disable_auto_reconnect = true,
    };
    
    g_ws_handle->client = esp_websocket_client_init(&ws_cfg);
    if (!g_ws_handle->client) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        ws_stream_destroy();
        return ESP_FAIL;
    }
    
    esp_websocket_register_events(g_ws_handle->client, WEBSOCKET_EVENT_ANY,
                                   websocket_event_handler, g_ws_handle);
    
    g_ws_handle->running = true;
    g_ws_handle->reconnect_delay_ms = g_ws_handle->config.reconnect_timeout_ms;
    
    if (xTaskCreate(ws_send_task, "ws_send", WS_TASK_STACK_SIZE, g_ws_handle,
                    WS_TASK_PRIORITY, &g_ws_handle->send_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create send task");
        ws_stream_destroy();
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "WebSocket streaming initialized");
    return ESP_OK;
}

esp_err_t ws_stream_enable(bool enable)
{
    if (!g_ws_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (enable && !g_ws_handle->enabled) {
        ESP_LOGI(TAG, "Enabling WebSocket streaming");
        g_ws_handle->enabled = true;
        esp_err_t ret = esp_websocket_client_start(g_ws_handle->client);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
            return ret;
        }
    } else if (!enable && g_ws_handle->enabled) {
        ESP_LOGI(TAG, "Disabling WebSocket streaming");
        g_ws_handle->enabled = false;
        esp_websocket_client_close(g_ws_handle->client, pdMS_TO_TICKS(3000));
        g_ws_handle->connected = false;
        
        frame_queue_item_t item;
        while (xQueueReceive(g_ws_handle->video_queue, &item, 0) == pdTRUE) {
            free_frame_item(&item);
        }
        while (xQueueReceive(g_ws_handle->audio_queue, &item, 0) == pdTRUE) {
            free_frame_item(&item);
        }
    }
    
    return ESP_OK;
}

esp_err_t ws_stream_queue_frame(esp_capture_stream_type_t type, const uint8_t *data, size_t size, uint32_t pts)
{
    if (!g_ws_handle || !g_ws_handle->enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!data || size == 0 || size > g_ws_handle->config.max_frame_size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    QueueHandle_t queue;
    uint8_t frame_type;
    uint32_t *seq_num;
    
    if (type == ESP_CAPTURE_STREAM_TYPE_VIDEO) {
        queue = g_ws_handle->video_queue;
        frame_type = WS_STREAM_TYPE_VIDEO;
        seq_num = &g_ws_handle->video_seq;
    } else if (type == ESP_CAPTURE_STREAM_TYPE_AUDIO) {
        queue = g_ws_handle->audio_queue;
        frame_type = WS_STREAM_TYPE_AUDIO;
        seq_num = &g_ws_handle->audio_seq;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    
    frame_queue_item_t item;
    item.type = frame_type;
    item.seq_num = (*seq_num)++;
    item.pts = pts;
    item.size = size;
    // ReSharper disable CppDFAMemoryLeak
    item.data = malloc(size);
    
    if (!item.data) {
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(item.data, data, size);
    
    if (xQueueSend(queue, &item, 0) != pdTRUE) {
        frame_queue_item_t old_item;
        if (xQueueReceive(queue, &old_item, 0) == pdTRUE) {
            free_frame_item(&old_item);
        }
        
        if (xQueueSend(queue, &item, 0) != pdTRUE) {
            free_frame_item(&item);
            return ESP_ERR_NO_MEM;
        }
    }
    
    xSemaphoreGive(g_ws_handle->queue_sem);
    return ESP_OK;
}

bool ws_stream_is_connected(void)
{
    // ReSharper disable once CppDFANullDereference
    return g_ws_handle && g_ws_handle->connected;
}

void ws_stream_destroy(void)
{
    if (!g_ws_handle) {
        return;
    }
    
    g_ws_handle->running = false;
    g_ws_handle->enabled = false;
    
    if (g_ws_handle->send_task) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    if (g_ws_handle->client) {
        esp_websocket_client_stop(g_ws_handle->client);
        esp_websocket_client_destroy(g_ws_handle->client);
    }
    
    if (g_ws_handle->video_queue) {
        frame_queue_item_t item;
        while (xQueueReceive(g_ws_handle->video_queue, &item, 0) == pdTRUE) {
            free_frame_item(&item);
        }
        vQueueDelete(g_ws_handle->video_queue);
    }
    
    if (g_ws_handle->audio_queue) {
        frame_queue_item_t item;
        while (xQueueReceive(g_ws_handle->audio_queue, &item, 0) == pdTRUE) {
            free_frame_item(&item);
        }
        vQueueDelete(g_ws_handle->audio_queue);
    }
    
    if (g_ws_handle->queue_sem) {
        vSemaphoreDelete(g_ws_handle->queue_sem);
    }
    
    free(g_ws_handle);
    g_ws_handle = NULL;
    
    ESP_LOGI(TAG, "WebSocket streaming destroyed");
}


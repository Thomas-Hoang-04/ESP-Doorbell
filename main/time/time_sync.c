#include "time_sync.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include "esp_log.h"

#include <sys/time.h>

static esp_err_t time_synced = ESP_FAIL;

char time_buffer[TIME_BUFFER_SIZE];

// Callback when time is synchronized
// ReSharper disable once CppParameterMayBeConstPtrOrRef
static void time_sync_notification_cb(struct timeval *tv) {
    memset(time_buffer, 0, TIME_BUFFER_SIZE);
    unix_to_human_utc((time_t)tv->tv_sec, time_buffer, TIME_BUFFER_SIZE);
    ESP_LOGI(TIME_TAG, "Time synchronized: %s", time_buffer);

    time_synced = ESP_OK;
}

void time_sync_init(void) {
    ESP_LOGI(TIME_TAG, "Initializing NTP time sync");

    // Set callback
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");

    // Start SNTP
    esp_sntp_init();

    // Set timezone to UTC by default
    setenv("TZ", "UTC", 1);
    tzset();
}

esp_err_t time_sync_wait(int timeout_seconds) {
    int retry = 0;
    const int max_retry = timeout_seconds * 2;

    ESP_LOGI(TIME_TAG, "Waiting for time synchronization...");

    while (time_synced != ESP_OK && ++retry < max_retry) {
        vTaskDelay(pdMS_TO_TICKS(500));

        // Also check SNTP status
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            time_synced = ESP_OK;
            break;
        }
    }

    if (time_synced == ESP_OK) {
        ESP_LOGI(TIME_TAG, "Time synchronized successfully");
        time_t now = time(NULL);

        memset(time_buffer, 0, TIME_BUFFER_SIZE);
        unix_to_human_utc(now, time_buffer, TIME_BUFFER_SIZE);
        ESP_LOGI(TIME_TAG, "Current time (UTC): %s", time_buffer);

        memset(time_buffer, 0, TIME_BUFFER_SIZE);
        unix_to_human_local(now, time_buffer, TIME_BUFFER_SIZE);
        ESP_LOGI(TIME_TAG, "Current time: %s", time_buffer);
    } else {
        ESP_LOGW(TIME_TAG, "Time synchronization timeout");
    }

    return time_synced;
}

time_t get_unix_timestamp(void) {
    return time(NULL);
}

uint64_t get_unix_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

bool time_is_synced(void) {
    return time_synced == ESP_OK && (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
}

void time_set_timezone(const char* timezone) {
    ESP_LOGI(TIME_TAG, "Setting timezone to: %s", timezone);
    setenv("TZ", timezone, 1);
    tzset();
}

void unix_to_human_local(time_t timestamp, char* buffer, size_t buffer_size) {
    struct tm *local_tm = localtime(&timestamp);
    if (local_tm != NULL) {
        strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S %Z", local_tm);
    } else {
        snprintf(buffer, buffer_size, "Invalid timestamp");
    }
}

void unix_to_human_utc(time_t timestamp, char* buffer, size_t buffer_size) {
    struct tm *utc_tm = gmtime(&timestamp);
    if (utc_tm != NULL) {
        strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S UTC", utc_tm);
    } else {
        snprintf(buffer, buffer_size, "Invalid timestamp");
    }
}
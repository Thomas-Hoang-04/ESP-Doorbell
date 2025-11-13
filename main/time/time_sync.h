#ifndef DOORBELL_TIME_SYNC_H
#define DOORBELL_TIME_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#define TIME_TAG "TIME_SYNC"

#define TIME_BUFFER_SIZE 32

// Global buffer to hold formatted time string
extern char time_buffer[TIME_BUFFER_SIZE];

/**
 * @brief Initialize time synchronization via SNTP
 */
void time_sync_init(void);

/**
 * @brief Wait for time synchronization to complete
 *
 * @param timeout_seconds Maximum time to wait in seconds
 * @return ESP_OK if time synchronized, ESP_ERR_TIMEOUT if timeout occurred
 */
esp_err_t time_sync_wait(int timeout_seconds);

/**
 * @brief Get current UNIX timestamp in seconds
 *
 * @return Current UNIX timestamp in seconds
 */
time_t get_unix_timestamp(void);

/**
 * @brief Get current UNIX timestamp in milliseconds
 *
 * @return Current UNIX timestamp in milliseconds
 */
uint64_t get_unix_timestamp_ms(void);

/**
 * @brief Check if time is synchronized
 *
 * @return true if time is synchronized, false otherwise
 */
bool time_is_synced(void);

/**
 * @brief Set the timezone for time conversions
 *
 * @param timezone Timezone string (e.g., "UTC", "PST8PDT")
 */
void time_set_timezone(const char* timezone);

/**
 * @brief Convert UNIX timestamp to human-readable string (local time)
 *
 * @param timestamp UNIX timestamp to convert
 * @param buffer Buffer to store the resulting string
 * @param buffer_size Size of the buffer
 */
void unix_to_human_local(time_t timestamp, char* buffer, size_t buffer_size);

/**
 * @brief Convert UNIX timestamp to human-readable string (UTC)
 *
 * @param timestamp UNIX timestamp to convert
 * @param buffer Buffer to store the resulting string
 * @param buffer_size Size of the buffer
 */
void unix_to_human_utc(time_t timestamp, char* buffer, size_t buffer_size);

#endif //DOORBELL_TIME_SYNC_H
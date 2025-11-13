/**
 * Simple SD card read/write regression.
 *
 * This helper intentionally lives in its own translation unit so the logic
 * stays easy to copy/paste into diagnostics tasks.  It relies solely on the
 * public `sd_handler` API plus libc, which means you can call it at any time
 * after boot once the card power rails are stable.  The test performs the
 * following high‑level steps:
 *
 *   1. Ensure the card is mounted (mounts it if needed).
 *   2. Create a deterministic payload in RAM.
 *   3. Write the payload to a file on the SD card.
 *   4. Read the file back and validate byte-for-byte correctness.
 *   5. Emit informative logs with timings, payload size, and clean-up status.
 *
 * All comments that explain the intent of the individual blocks are kept
 * verbose on purpose—please keep them when cloning this snippet for other
 * projects.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "../sd_handler/sd_handler.h"

#define SD_TEST_FILENAME   MOUNT_POINT "/sd_self_test.bin"
#define SD_TEST_PAYLOAD_SZ 1024

#define SD_STRESS_FILENAME   MOUNT_POINT "/sd_stress_test.bin"
#define SD_STRESS_CHUNK_SIZE (32 * 1024)
#define SD_STRESS_TOTAL_BYTES (20 * 1024 * 1024)  // 10 MB sustained transfer per phase
#define SD_STRESS_ITERATIONS 5                  // Number of back-to-back passes

static void fill_test_pattern(uint8_t *buffer, size_t length)
{
    // Generate a simple deterministic waveform (sawtooth) so any mismatch is obvious.
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = (uint8_t)((i * 37U + 13U) & 0xFF);  // Multipliers chosen arbitrarily.
    }
}

static void log_elapsed(const char *phase, int64_t start_us)
{
    const int64_t elapsed_us = esp_timer_get_time() - start_us;
    ESP_LOGI(SD_TAG, "%s completed in %lld us (~%0.2f ms)",
             phase, (long long)elapsed_us, (double)elapsed_us / 1000.0);
}

static void sd_card_self_test_body(void)
{
    ESP_LOGI(SD_TAG, "===== SD CARD SELF-TEST START =====");

    // Prepare deterministic payload and scratch buffer for validation.
    const size_t read_buffer_multiplier = 6;  // adjust as needed
    uint8_t *write_buffer = malloc(SD_TEST_PAYLOAD_SZ);
    uint8_t *read_buffer = malloc(SD_TEST_PAYLOAD_SZ * read_buffer_multiplier);
    if (!write_buffer || !read_buffer) {
        ESP_LOGE(SD_TAG, "Failed to allocate read/write buffers (%u / %u bytes)",
                 (unsigned)SD_TEST_PAYLOAD_SZ,
                 (unsigned)(SD_TEST_PAYLOAD_SZ * read_buffer_multiplier));
        free(write_buffer);
        free(read_buffer);
        return;
    }
    fill_test_pattern(write_buffer, SD_TEST_PAYLOAD_SZ);
    memset(read_buffer, 0, SD_TEST_PAYLOAD_SZ * read_buffer_multiplier);

    // Remove any stale test file to ensure we always start fresh.
    if (file_exists_on_sd(SD_TEST_FILENAME)) {
        ESP_LOGW(SD_TAG, "Previous test file exists, deleting it first");
        ESP_ERROR_CHECK(delete_from_sd(SD_TEST_FILENAME));
    }

    // Step 2: Write the payload to disk (binary mode to avoid newline mangling).
    int64_t start_us = esp_timer_get_time();
    FILE *wf = fopen(SD_TEST_FILENAME, "wb");
    if (!wf) {
        ESP_LOGE(SD_TAG, "Failed to open %s for writing", SD_TEST_FILENAME);
        free(write_buffer);
        free(read_buffer);
        return;
    }
    size_t written = fwrite(write_buffer, 1, SD_TEST_PAYLOAD_SZ, wf);
    fflush(wf);
    fclose(wf);
    if (written != SD_TEST_PAYLOAD_SZ) {
        ESP_LOGE(SD_TAG, "Write mismatch: wrote %u bytes instead of %u",
                 (unsigned)written, (unsigned)SD_TEST_PAYLOAD_SZ);
        free(write_buffer);
        free(read_buffer);
        return;
    }
    log_elapsed("Write phase", start_us);

    // Step 3: Read the payload back.
    start_us = esp_timer_get_time();
    FILE *rf = fopen(SD_TEST_FILENAME, "rb");
    if (!rf) {
        ESP_LOGE(SD_TAG, "Failed to open %s for reading", SD_TEST_FILENAME);
        free(write_buffer);
        free(read_buffer);
        return;
    }
    size_t read = fread(read_buffer, 1, SD_TEST_PAYLOAD_SZ * read_buffer_multiplier, rf);
    fclose(rf);
    if (read != SD_TEST_PAYLOAD_SZ) {
        ESP_LOGE(SD_TAG, "Read mismatch: read %u bytes instead of %u",
                 (unsigned)read, (unsigned)SD_TEST_PAYLOAD_SZ);
        free(write_buffer);
        free(read_buffer);
        return;
    }
    log_elapsed("Read phase", start_us);

    // Step 4: Validate the payload contents.
    if (memcmp(write_buffer, read_buffer, SD_TEST_PAYLOAD_SZ) != 0) {
        ESP_LOGE(SD_TAG, "Data integrity check FAILED (payload differs)");
    } else {
        ESP_LOGI(SD_TAG, "Data integrity check PASSED");
    }

    free(write_buffer);
    free(read_buffer);

    // Step 5: Optionally keep the file for inspection or remove it.
    // Comment/uncomment the block below depending on your diagnostics needs.
    // Keeping the file allows you to inspect the binary payload on a PC card reader.
    // Removing it keeps the filesystem clean after every test cycle.
    const bool keep_test_file = false;
    if (!keep_test_file) {
        ESP_ERROR_CHECK(delete_from_sd(SD_TEST_FILENAME));
        ESP_LOGI(SD_TAG, "Temporary test file removed");
    }

    // Give the user a snapshot of current card stats after the operation.
    get_sd_card_info();

    // ----------------------------- Sustained throughput test -----------------------------
    ESP_LOGI(SD_TAG, "--- SD sustained throughput test (%d iterations) ---", SD_STRESS_ITERATIONS);

    // Prepare stress buffers (using the same deterministic filler to help spot mismatches).
    uint8_t *stress_write = malloc(SD_STRESS_CHUNK_SIZE);
    uint8_t *stress_read = malloc(SD_STRESS_CHUNK_SIZE);
    if (!stress_write || !stress_read) {
        ESP_LOGE(SD_TAG, "Failed to allocate stress buffers (%d bytes each)", SD_STRESS_CHUNK_SIZE);
        free(stress_write);
        free(stress_read);
        ESP_LOGI(SD_TAG, "===== SD CARD SELF-TEST END =====");
        return;
    }
    fill_test_pattern(stress_write, SD_STRESS_CHUNK_SIZE);

    for (int pass = 0; pass < SD_STRESS_ITERATIONS; ++pass) {
        ESP_LOGI(SD_TAG, ">>> Sustained pass %d/%d", pass + 1, SD_STRESS_ITERATIONS);

        // Write phase (loop until total bytes reached).
        FILE *stress_wf = fopen(SD_STRESS_FILENAME, "wb");
        if (!stress_wf) {
            ESP_LOGE(SD_TAG, "Failed to open %s for stress writing", SD_STRESS_FILENAME);
            break;
        }
        int64_t stress_start = esp_timer_get_time();
        size_t remaining = SD_STRESS_TOTAL_BYTES;
        while (remaining > 0) {
            size_t chunk = remaining > SD_STRESS_CHUNK_SIZE ? SD_STRESS_CHUNK_SIZE : remaining;
            size_t written_chunk = fwrite(stress_write, 1, chunk, stress_wf);
            if (written_chunk != chunk) {
                ESP_LOGE(SD_TAG, "Sustained write failed after %zu bytes", SD_STRESS_TOTAL_BYTES - remaining + written_chunk);
                break;
            }
            remaining -= chunk;
        }
        fflush(stress_wf);
        fclose(stress_wf);
        int64_t stress_elapsed = esp_timer_get_time() - stress_start;
        double write_mb = (double)SD_STRESS_TOTAL_BYTES / (1024.0 * 1024.0);
        double write_sec = (double)stress_elapsed / 1e6;
        ESP_LOGI(SD_TAG, "Write throughput: %.2f MB/s (%0.2f MB in %.2f s)",
                 write_mb / write_sec, write_mb, write_sec);

        // Read phase.
        FILE *stress_rf = fopen(SD_STRESS_FILENAME, "rb");
        if (!stress_rf) {
            ESP_LOGE(SD_TAG, "Failed to open %s for stress reading", SD_STRESS_FILENAME);
            break;
        }
        stress_start = esp_timer_get_time();
        remaining = SD_STRESS_TOTAL_BYTES;
        size_t total_read = 0;
        bool read_error = false;
        while (remaining > 0) {
            size_t chunk = remaining > SD_STRESS_CHUNK_SIZE ? SD_STRESS_CHUNK_SIZE : remaining;
            size_t read_chunk = fread(stress_read, 1, chunk, stress_rf);
            if (read_chunk != chunk) {
                ESP_LOGE(SD_TAG, "Sustained read failed after %zu bytes", total_read + read_chunk);
                read_error = true;
                break;
            }
            if (memcmp(stress_write, stress_read, chunk) != 0) {
                ESP_LOGE(SD_TAG, "Sustained data mismatch near offset %zu", total_read);
                read_error = true;
                break;
            }
            total_read += chunk;
            remaining -= chunk;
        }
        fclose(stress_rf);
        stress_elapsed = esp_timer_get_time() - stress_start;
        double read_mb = (double)total_read / (1024.0 * 1024.0);
        double read_sec = (double)stress_elapsed / 1e6;
        if (!read_error) {
            ESP_LOGI(SD_TAG, "Read throughput: %.2f MB/s (%0.2f MB in %.2f s)",
                     read_mb / read_sec, read_mb, read_sec);
        }

        // Clean up between passes to avoid disk filling up.
        ESP_ERROR_CHECK(delete_from_sd(SD_STRESS_FILENAME));
    }

    free(stress_write);
    free(stress_read);

    get_sd_card_info();

    ESP_LOGI(SD_TAG, "--- Sustained throughput test complete ---");

    ESP_LOGI(SD_TAG, "===== SD CARD SELF-TEST END =====");
}

static void sd_card_self_test_task(void *arg)
{
    sd_card_self_test_body();
    vTaskDelete(NULL);
}

void sd_card_self_test(void)
{
    const uint32_t stack_words = 8192;  // words, not bytes
    if (xTaskCreate(sd_card_self_test_task, "sd_self_test", stack_words, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(SD_TAG, "Failed to create SD self-test task");
    }
}

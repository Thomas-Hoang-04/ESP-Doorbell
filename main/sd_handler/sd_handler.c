#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#include "sd_handler.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_default_configs.h"
#include "driver/sdmmc_host.h"
#include "ff.h"
#include "sdmmc_cmd.h"
#include "sys/dirent.h"

// SD Card Reference
sdmmc_card_t *card;

// SD Card Management Functions
esp_err_t mount_sd_card(void) {
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = MAX_FILES,
        .allocation_unit_size = ALLOCATION_UNIT_SIZE,
    };

    ESP_LOGI(SD_TAG, "Initializing SD Card with SDMMC...");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SD_CLK;

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();

    slot_cfg.width = BUS_WIDTH;

    slot_cfg.clk = SD_PIN_CLK;
    slot_cfg.cmd = SD_PIN_CMD;
    slot_cfg.d0 = SD_PIN_DATA0;

    slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP | SDMMC_SLOT_FLAG_UHS1;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(SD_TAG, "Failed to mount SD Card: %d", esp_err_to_name(ret));
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return ret;
    }

    sdmmc_card_print_info(stdout, card);

    ESP_LOGI(SD_TAG, "SD Card mounted successfully");

    return ret;
}

esp_err_t unmount_sd_card(void) {
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    if (ret != ESP_OK)
        ESP_LOGE(SD_TAG, "Unmount SD card failed: %s", esp_err_to_name(ret));
    return ret;
}

esp_err_t format_sd_card(void) {
    esp_err_t ret = esp_vfs_fat_sdcard_format(MOUNT_POINT, card);
    if (ret != ESP_OK)
        ESP_LOGE(SD_TAG, "Format SD card failed: %s", esp_err_to_name(ret));
    return ret;
}

// SD Card File Management Functions
esp_err_t write_to_sd(const char *filename, uint8_t* data, const size_t size, const char* mode) {
    ESP_LOGI(SD_TAG, "Writing to SD Card: %s", filename);

    FILE *f = fopen(filename, mode);
    if (f == NULL) {
        ESP_LOGE(SD_TAG, "Failed to open file: %s", filename);
        return ESP_FAIL;
    }

    if (strcmp(mode, "w") != 0 || strcmp(mode, "a") != 0)
        fprintf(f, "%s", (char*)data);
    else {
        size_t bytes_written = fwrite(data, 1, size, f);
        if (bytes_written != size) {
            ESP_LOGE(SD_TAG, "Write error: wrote %d bytes of %d bytes", bytes_written, size);
            return ESP_FAIL;
        }
    }

    ESP_LOGI(SD_TAG, "Written to SD Card: %s", filename);
    return ESP_OK;
}

esp_err_t read_from_sd(const char *filename, uint8_t* data, const size_t size, const char* mode) {
    ESP_LOGI(SD_TAG, "Reading from SD Card: %s", filename);

    FILE *f = fopen(filename, mode);
    if (f == NULL) {
        ESP_LOGE(SD_TAG, "Failed to open file: %s", filename);
        return ESP_FAIL;
    }

    size_t read_size = strcmp(mode, "r") == 0 ? size - 1 : size;
    size_t bytes_read = fread(data, 1, read_size, f);
    fclose(f);

    if (read_size < size) data[bytes_read] = '\0';

    ESP_LOGI(SD_TAG, "Read %d bytes from SD Card: %s", bytes_read, filename);

    return ESP_OK;
} 

esp_err_t delete_from_sd(const char *filename) {
    ESP_LOGI(SD_TAG, "Deleting file: %s", filename);

    if (unlink(filename) == 0) {
        ESP_LOGI(SD_TAG, "File deleted successfully");
        return ESP_OK;
    }
    ESP_LOGE(SD_TAG, "Failed to delete file");
    return ESP_FAIL;
}

// SD Card Information Functions
void list_files_on_sd(const char* path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(SD_TAG, "Failed to open directory: %s", path);
        return;
    }

    ESP_LOGI(SD_TAG, "Listing directory: %s", path);

    struct dirent *entry;
    char full_path[1024];
    while ((entry = readdir(dir))) {
        memset(full_path, 0, sizeof(full_path));
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode))
                ESP_LOGI(SD_TAG, " [DIR] %s", entry->d_name);
            else
                ESP_LOGI(SD_TAG, " [FILE] %s (%ld bytes)", entry->d_name, st.st_size);
        }
    }

    closedir(dir);
}

bool file_exists_on_sd(const char *filename) {
    struct stat st;
    return stat(filename, &st) == 0;
}

uint64_t get_file_size_on_sd(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        ESP_LOGE(SD_TAG, "Failed to open file");
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    uint64_t size = ftell(f);
    fclose(f);
    
    return size;
}

void get_sd_card_info(void) {
    if (card == NULL) {
        ESP_LOGE(SD_TAG, "Card not initialized");
        return;
    }

    sdmmc_card_print_info(stdout, card);

    FATFS *fs;
    DWORD fre_clust;

    if (f_getfree(MOUNT_POINT, &fre_clust, &fs) == FR_OK) {
        uint64_t total_bytes = (uint64_t)(fs->n_fatent - 2) * fs->csize * 512;
        uint64_t free_bytes = (uint64_t)fre_clust * fs->csize * 512;

        ESP_LOGI(SD_TAG, "SD Card Size: %llu MB", total_bytes / (1024 * 1024));
        ESP_LOGI(SD_TAG, "Free Space: %llu MB", free_bytes / (1024 * 1024));
        ESP_LOGI(SD_TAG, "Used Space: %llu MB", (total_bytes - free_bytes) / (1024 * 1024));
    } else {
        ESP_LOGW(SD_TAG, "Failed to query free clusters");
    }

    DIR *dir = opendir(MOUNT_POINT);
    if (dir == NULL) {
        ESP_LOGE(SD_TAG, "Failed to open directory: %s", MOUNT_POINT);
        return;
    }

    ESP_LOGI(SD_TAG, "Listing files under %s:", MOUNT_POINT);
    struct dirent *entry;
    char full_path[1024];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) != 0) {
            ESP_LOGW(SD_TAG, "  [???] %s (stat failed)", entry->d_name);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            ESP_LOGI(SD_TAG, "  [DIR ] %s", entry->d_name);
        } else {
            ESP_LOGI(SD_TAG, "  [FILE] %s (%ld bytes)", entry->d_name, st.st_size);
        }
    }
    closedir(dir);
}

// ---------------------------------------------------------------------------
// File Cleanup Logic
// ---------------------------------------------------------------------------

static TaskHandle_t s_cleanup_task = NULL;
static char s_cleanup_path[64] = {0};

static time_t parse_timestamp_from_filename(const char* filename) {
    // Expected format: capture-YYYYMMDD_HHMMSS_Z-INDEX.mp4
    // Example: capture-20250101_120000_UTC-0.mp4
    
    // Find "capture-" prefix
    const char* start = strstr(filename, "capture-");
    if (!start) return 0;
    start += 8; // Skip "capture-"

    // Check we have enough length (YYYYMMDD_HHMMSS is 15 chars)
    if (strlen(start) < 15) return 0;

    struct tm tm = {0};
    char buf[8] = {0};

    // Parse YYYY
    strncpy(buf, start, 4);
    buf[4] = 0;
    tm.tm_year = (int)strtol(buf, NULL, 10) - 1900;
    start += 4;

    // Parse MM
    strncpy(buf, start, 2);
    buf[2] = 0;
    tm.tm_mon = (int)strtol(buf, NULL, 10) - 1;
    start += 2;

    // Parse DD
    strncpy(buf, start, 2);
    buf[2] = 0;
    tm.tm_mday = (int)strtol(buf, NULL, 10);
    start += 3;

    // Parse HH
    strncpy(buf, start, 2);
    buf[2] = 0;
    tm.tm_hour = (int)strtol(buf, NULL, 10);
    start += 2;

    // Parse MM
    strncpy(buf, start, 2);
    buf[2] = 0;
    tm.tm_min = (int)strtol(buf, NULL, 10);
    start += 2;

    // Parse SS
    strncpy(buf, start, 2);
    buf[2] = 0;
    tm.tm_sec = (int)strtol(buf, NULL, 10);

    return mktime(&tm);
}

static void cleanup_old_files(const char* path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(SD_TAG, "Cleanup: Failed to open directory: %s", path);
        return;
    }

    ESP_LOGI(SD_TAG, "Running file cleanup on %s...", path);

    time_t now = time(NULL);
    // Convert retention hours to seconds
    double max_age_seconds = (double)CONFIG_RECORDING_RETENTION_HOURS * 3600.0;
    int deleted_count = 0;

    struct dirent *entry;
    char full_path[512];

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) continue; // Skip non-files

        time_t file_time = parse_timestamp_from_filename(entry->d_name);
        if (file_time == 0) continue; // Couldn't parse timestamp

        double age = difftime(now, file_time);

        if (age > max_age_seconds) {
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            ESP_LOGI(SD_TAG, "Deleting old file: %s (Age: %.1f hours)", entry->d_name, age / 3600.0);
            
            if (unlink(full_path) == 0) {
                deleted_count++;
            } else {
                ESP_LOGE(SD_TAG, "Failed to delete: %s", full_path);
            }
        }
    }

    closedir(dir);
    ESP_LOGI(SD_TAG, "Cleanup complete. Deleted %d files.", deleted_count);
}

static void cleanup_task_loop(void *arg) {
    const int check_interval_ms = 5 * 60 * 1000; // 5 minutes

    // ReSharper disable once CppDFAEndlessLoop
    while (1) {
        // Wait first (offset the check from boot time)
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
        
        cleanup_old_files(s_cleanup_path);
    }
}

void start_file_cleanup_task(const char* path) {
    if (s_cleanup_task) {
        ESP_LOGW(SD_TAG, "Cleanup task already running");
        return;
    }

    // copy path
    strncpy(s_cleanup_path, path, sizeof(s_cleanup_path) - 1);

    if (xTaskCreate(cleanup_task_loop, "cleanup_task", 4096, NULL, 1, &s_cleanup_task) != pdPASS) {
        ESP_LOGE(SD_TAG, "Failed to create cleanup task");
    } else {
        ESP_LOGI(SD_TAG, "Cleanup task started (Retention: %d hours)", CONFIG_RECORDING_RETENTION_HOURS);
    }
}

void stop_file_cleanup_task(void) {
    if (s_cleanup_task) {
        vTaskDelete(s_cleanup_task);
        s_cleanup_task = NULL;
        ESP_LOGI(SD_TAG, "Cleanup task stopped");
    }
}
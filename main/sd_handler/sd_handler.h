/**
 * @file sd_handler.h
 * @brief SD Card management driver using SDMMC interface
 */

#ifndef SD_HANDLER_H
#define SD_HANDLER_H

#include "sd_protocol_types.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
    
#define SD_TAG "SD_CARD"

#ifdef __cplusplus
extern "C" {
#endif

/** @name SD Card Configuration
 * @{
 */
#define BUS_WIDTH 1                      /**< SDMMC bus width (1-bit mode) */
#define SD_CLK SDMMC_FREQ_HIGHSPEED      /**< SDMMC clock frequency */
#define MOUNT_POINT "/sdcard"            /**< VFS mount point for SD card */
#define MAX_FILES 5                      /**< Maximum open files */
#define ALLOCATION_UNIT_SIZE (32 * 1024) /**< FAT allocation unit size */
/** @} */

/** @name SD Card Pin Mapping
 * @{
 */
#define SD_PIN_CLK GPIO_NUM_39           /**< SDMMC CLK pin */
#define SD_PIN_CMD GPIO_NUM_38           /**< SDMMC CMD pin */
#define SD_PIN_DATA0 GPIO_NUM_40         /**< SDMMC DATA0 pin */
/** @} */

/** @brief Global SD card handle */
extern sdmmc_card_t *card;

/**
 * @brief Mount the SD card filesystem
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mount_sd_card(void);

/**
 * @brief Unmount the SD card filesystem
 * @return ESP_OK on success, error code on failure
 */
esp_err_t unmount_sd_card(void);

/**
 * @brief Format the SD card with FAT filesystem
 * @return ESP_OK on success, error code on failure
 */
esp_err_t format_sd_card(void);

/**
 * @brief Write data to a file on the SD card
 * @param[in] filename Full path to file
 * @param[in] data Pointer to data buffer
 * @param[in] size Number of bytes to write
 * @param[in] mode File open mode ("w", "a", "wb", etc.)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t write_to_sd(const char *filename, uint8_t* data, size_t size, const char* mode);

/**
 * @brief Read data from a file on the SD card
 * @param[in] filename Full path to file
 * @param[out] data Buffer to store read data
 * @param[in] size Maximum bytes to read
 * @param[in] mode File open mode ("r", "rb", etc.)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t read_from_sd(const char *filename, uint8_t* data, size_t size, const char* mode);

/**
 * @brief Delete a file from the SD card
 * @param[in] filename Full path to file
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t delete_from_sd(const char *filename);

/**
 * @brief List all files in a directory on the SD card
 * @param[in] path Directory path to list
 */
void list_files_on_sd(const char* path);

/**
 * @brief Check if a file exists on the SD card
 * @param[in] filename Full path to file
 * @return true if file exists, false otherwise
 */
bool file_exists_on_sd(const char *filename);

/**
 * @brief Get the size of a file on the SD card
 * @param[in] filename Full path to file
 * @return File size in bytes, or -1 on error
 */
uint64_t get_file_size_on_sd(const char *filename);

/**
 * @brief Print SD card information to console
 */
void get_sd_card_info(void);

/**
 * @brief Run a basic read/write regression on the mounted SD card
 *
 * Writes a deterministic payload, reads it back for verification, 
 * and logs timing and error information.
 */
void sd_card_self_test(void);

#ifdef __cplusplus
}
#endif

#endif
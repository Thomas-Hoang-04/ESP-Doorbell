#ifndef SD_HANDLER_H
#define SD_HANDLER_H

#include "sd_protocol_types.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
    
#define SD_TAG "SD_CARD"

// SD Card Configuration
#define BUS_WIDTH 1
#define SD_CLK SDMMC_FREQ_HIGHSPEED
#define MOUNT_POINT "/sdcard"
#define MAX_FILES 5
#define ALLOCATION_UNIT_SIZE (32 * 1024)

// SD Card Pin Mapping
#define SD_PIN_CLK GPIO_NUM_39
#define SD_PIN_CMD GPIO_NUM_38
#define SD_PIN_DATA0 GPIO_NUM_40

// SD Card Reference    
extern sdmmc_card_t *card;

// SD Card Management Functions
esp_err_t mount_sd_card(void);
esp_err_t unmount_sd_card(void);
esp_err_t format_sd_card(void);

// SD Card File Management Functions
esp_err_t write_to_sd(const char *filename, uint8_t* data, size_t size, char* mode);
esp_err_t read_from_sd(const char *filename, uint8_t* data, size_t size, char* mode);
esp_err_t delete_from_sd(const char *filename);

// SD Card Information Functions
void list_files_on_sd(const char* path);
bool file_exists_on_sd(const char *filename);
uint64_t get_file_size_on_sd(const char *filename);
void get_sd_card_info(void);

#endif
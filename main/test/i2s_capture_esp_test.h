#pragma once

#include "../sd_handler/sd_handler.h"

#define I2S_CAPTURE_ESP_TEST_DURATION_SEC 60

#define I2S_CAPTURE_ESP_TEST_SAMPLE_RATE 16000

#define I2S_CAPTURE_ESP_TEST_CHANNELS 2

#define I2S_CAPTURE_ESP_TEST_BITS_PER_SAMPLE 16

#define I2S_CAPTURE_ESP_TEST_BITRATE 128000

#define I2S_CAPTURE_ESP_TEST_ALC_GAIN_DB 48

#define I2S_CAPTURE_ESP_TEST_OUTPUT MOUNT_POINT "/i2s_capture_esp_test.aac"

/**
 * Launches the esp_capture-based I2S recording test. The pipeline captures audio
 * from the I2S microphone, encodes it to AAC, and stores the ADTS stream on the SD card.
 * The legacy direct-to-AAC test remains available separately.
 */
void i2s_capture_esp_test(void);

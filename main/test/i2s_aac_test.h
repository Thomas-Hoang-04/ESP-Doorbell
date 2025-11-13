#pragma once

#include "../sd_handler/sd_handler.h"

/**
 * Duration of the I2S capture window in seconds. Feel free to override this
 * macro before including the header if you need a different run length.
 */
#define I2S_AAC_TEST_DURATION_SEC 60

/**
 * Sample rate used for the capture test (Hz).
 */
#define I2S_AAC_TEST_SAMPLE_RATE 16000

/**
 * Number of channels captured from the I2S bus (1 = mono, 2 = stereo).
 */
#define I2S_AAC_TEST_CHANNELS 2

/**
 * PCM bit depth per sample.
 */
#define I2S_AAC_TEST_BITS_PER_SAMPLE 16

/**
 * I2S slot config
 */
#define I2S_AAC_SLOT I2S_STD_SLOT_BOTH

/**
 * Enable Automatic Level Control (ALC) processing for the captured PCM prior to
 * encoding. Set to 0 to disable.
 */
#define I2S_AAC_TEST_ENABLE_ALC 1

/**
 * Static gain in dB applied via ALC per channel (range [-64, 63]).
 */
#define I2S_AAC_TEST_ALC_GAIN_DB 48

/**
 * Target AAC bitrate (bits per second). Ensure it stays within the encoder's
 * supported range for the chosen sample rate and channel count.
 */
#define I2S_AAC_TEST_BITRATE 128000

/**
 * Destination file for the encoded AAC frames.
 */
#define I2S_AAC_TEST_OUTPUT MOUNT_POINT "/i2s_aac_test.aac"

/**
 * I2S peripheral configuration (port and pins). Adjust these defaults to match
 * your board wiring. Leave MCLK as GPIO_NUM_NC if unused.
 */
#define I2S_AAC_TEST_PORT I2S_NUM_0
#define I2S_AAC_TEST_PIN_MCLK GPIO_NUM_NC
#define I2S_AAC_TEST_PIN_BCLK GPIO_NUM_2
#define I2S_AAC_TEST_PIN_WS GPIO_NUM_42
#define I2S_AAC_TEST_PIN_DIN GPIO_NUM_41

/**
 * Launches the standalone I2S -> AAC capture test. This function spawns an
 * internal FreeRTOS task with sufficient stack to carry out the recording,
 * so it returns immediately.
 */
void i2s_aac_record_test(void);

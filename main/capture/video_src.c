#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"

#include "audio_i2s_src.h"
#include "impl/esp_capture_video_dvp_src.h"
#include "mp4_muxer.h"
#include "esp_audio_enc_default.h"

#include "../camera/camera.h"

#include "video_src.h"

#include <sys/errno.h>
#include <sys/stat.h>

av_handles_t av_handles = {0};

av_task_handle_t av_task_handles = {0};

static int mp4_url_pattern(char *file_path, int len, int slice_idx)
{
    if (len <= 0) { return -1; }
    // TODO: Generate unique file names for each slice if needed
    strlcpy(file_path, AV_CAPTURE_MP4_DIR, len);
    return 0;
}

esp_err_t capture_setup(void) {
    ESP_LOGI(AV_LOG_TAG, "====== Capture setup started ======");

    esp_audio_err_t reg_ret = esp_audio_enc_register_default();
    if (reg_ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(AV_LOG_TAG, "Failed to register default audio encoders (%d)", reg_ret);
        return ESP_FAIL;
    }

    if (mp4_muxer_register() != ESP_MUXER_ERR_OK) {
        ESP_LOGE(AV_LOG_TAG, "Failed to register MP4 muxer");
        return ESP_FAIL;
    }

    if (mkdir(AV_CAPTURE_MP4_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(AV_LOG_TAG, "Failed to create directory for MP4 files");
        return ESP_FAIL;
    }

    av_handles.audio_src = esp_capture_new_audio_i2s_src(NULL);
    if (av_handles.audio_src == NULL) {
        ESP_LOGE(AV_LOG_TAG, "Failed to create audio I2S source");
        return ESP_FAIL;
    }

    i2c_master_bus_config_t i2c_mst_cfg = SCCB_DEFAULT();
    esp_err_t ret = i2c_new_master_bus(&i2c_mst_cfg, &av_handles.sccb_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(AV_LOG_TAG, "Failed to install SCCB I2C master bus: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_capture_video_dvp_src_cfg_t vid_cfg = DVP_SRC_DEFAULT(CAMERA_BUFFER_COUNT);
    av_handles.video_src = esp_capture_new_video_dvp_src(&vid_cfg);
    if (av_handles.video_src == NULL) {
        ESP_LOGE(AV_LOG_TAG, "Failed to create DVP video source");
        return ESP_FAIL;
    }

    esp_capture_cfg_t capture_cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_src = av_handles.audio_src,
        .video_src = av_handles.video_src,
    };

    if (esp_capture_open(&capture_cfg, &av_handles.capture) != ESP_CAPTURE_ERR_OK || av_handles.capture == NULL) {
        ESP_LOGE(AV_LOG_TAG, "Failed to open capture instance");
        return ESP_FAIL;
    }

    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .format_id = AUDIO_FORMAT,
            .sample_rate = AUDIO_SAMPLE_RATE,
            .channel = AUDIO_CHANNELS,
            .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
        },
        .video_info = {
            .format_id = VIDEO_FORMAT,
            .width = VIDEO_WIDTH,
            .height = VIDEO_HEIGHT,
            .fps = VIDEO_FPS,
        },
    };

    esp_capture_err_t ret_sink = esp_capture_sink_setup(av_handles.capture, 0, &sink_cfg, &av_handles.video_sink);
    if (ret_sink != ESP_CAPTURE_ERR_OK || av_handles.video_sink == NULL) {
        ESP_LOGE(AV_LOG_TAG, "Failed to setup capture sink");
        return ESP_FAIL;
    }

    ret_sink = esp_capture_sink_setup(av_handles.capture, 1, &sink_cfg, &av_handles.stream_sink);
    if (ret_sink != ESP_CAPTURE_ERR_OK || av_handles.stream_sink == NULL) {
        ESP_LOGE(AV_LOG_TAG, "Failed to setup stream sink");
        return ESP_FAIL;
    }

    mp4_muxer_config_t mp4_cfg = {
        .base_config = {
            .muxer_type = AV_MUXER_TYPE,
            .slice_duration = AV_CAPTURE_MP4_DURATION_SEC,
            .url_pattern = mp4_url_pattern,
            .ram_cache_size = AV_MUXER_CACHE_SIZE,
        },
        .display_in_order = true,
        .moov_before_mdat = true,
    };

    esp_capture_muxer_cfg_t muxer_cfg = {
        .base_config = &mp4_cfg.base_config,
        .cfg_size = sizeof(mp4_cfg),
    };

    if (esp_capture_sink_add_muxer(av_handles.video_sink, &muxer_cfg) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_LOG_TAG, "Failed to add MP4 muxer");
        return ESP_FAIL;
    }
    esp_capture_sink_enable_muxer(av_handles.video_sink, true);

    return ret;
}

static void start_capture(void* arg) {
    // ReSharper disable once CppTooWideScope
    esp_capture_stream_frame_t frame = {0};

    if (av_handles.capture == NULL) {
        ESP_LOGE(AV_LOG_TAG, "Capture instance not initialized");
        vTaskDelete(NULL);
        return;
    }

    if (av_handles.video_sink == NULL) {
        ESP_LOGE(AV_LOG_TAG, "Capture sink not initialized");
        vTaskDelete(NULL);
        return;
    }

    if (av_handles.capture_initialized) {
        ESP_LOGW(AV_LOG_TAG, "Capture already initialized");
        goto enable_sink;
    }

    esp_capture_err_t ret = esp_capture_start(av_handles.capture);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_LOG_TAG, "Failed to start capture");
        vTaskDelete(NULL);
        return;
    }
    av_handles.capture_initialized = true;

enable_sink:
    if (av_handles.video_sink_enabled) {
        ESP_LOGW(AV_LOG_TAG, "Video sink already enabled");
        goto action;
    }

    ret = esp_capture_sink_enable(av_handles.video_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_LOG_TAG, "Failed to enable stream sink");
        vTaskDelete(NULL);
        return;
    }
    av_handles.video_sink_enabled = true;

action:
    av_handles.capture_started = true;
    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
        while (esp_capture_sink_acquire_frame(av_handles.video_sink, &frame, true) == ESP_CAPTURE_ERR_OK)
            esp_capture_sink_release_frame(av_handles.video_sink, &frame);
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
        while (esp_capture_sink_acquire_frame(av_handles.video_sink, &frame, true) == ESP_CAPTURE_ERR_OK)
            esp_capture_sink_release_frame(av_handles.video_sink, &frame);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void start_streaming(void* arg) {
    // ReSharper disable once CppTooWideScope
    esp_capture_stream_frame_t frame = {0};

    if (av_handles.capture == NULL) {
        ESP_LOGE(AV_LOG_TAG, "Capture instance not initialized");
        vTaskDelete(NULL);
        return;
    }

    if (av_handles.stream_sink == NULL) {
        ESP_LOGE(AV_LOG_TAG, "Capture sink not initialized");
        vTaskDelete(NULL);
        return;
    }

    if (av_handles.capture_initialized) {
        ESP_LOGW(AV_LOG_TAG, "Capture already started");
        goto enable_sink;
    }

    esp_capture_err_t ret = esp_capture_start(av_handles.capture);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_LOG_TAG, "Failed to start capture");
        vTaskDelete(NULL);
        return;
    }
    av_handles.capture_initialized = true;

enable_sink:
    if (av_handles.stream_sink_enabled) {
        ESP_LOGW(AV_LOG_TAG, "Video sink already enabled");
        goto action;
    }

    ret = esp_capture_sink_enable(av_handles.stream_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_LOG_TAG, "Failed to enable stream sink");
        vTaskDelete(NULL);
        return;
    }
    av_handles.stream_sink_enabled = true;

action:
    av_handles.streaming_started = true;
    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
        while (esp_capture_sink_acquire_frame(av_handles.stream_sink, &frame, true) == ESP_CAPTURE_ERR_OK)
            // TODO: process streaming audio frame here
            esp_capture_sink_release_frame(av_handles.stream_sink, &frame);
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
        while (esp_capture_sink_acquire_frame(av_handles.stream_sink, &frame, true) == ESP_CAPTURE_ERR_OK)
            // TODO: process streaming video frame here
            esp_capture_sink_release_frame(av_handles.stream_sink, &frame);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void start_streaming_task(void) {
    if (av_task_handles.stream_task != NULL) {
        ESP_LOGW(AV_LOG_TAG, "Streaming task already running");
        return;
    }
    BaseType_t ret = xTaskCreate(start_streaming, "av_streaming_task", 16 * 1024, NULL, 5, &av_task_handles.stream_task);
    if (ret != pdPASS) {
        ESP_LOGE(AV_LOG_TAG, "Failed to create streaming task");
        av_task_handles.stream_task = NULL;
    }
}

void start_capture_task(void) {
    if (av_task_handles.capture_task != NULL) {
        ESP_LOGW(AV_LOG_TAG, "Capture task already running");
        return;
    }
    BaseType_t ret = xTaskCreate(start_capture, "av_capture_task", 16 * 1024, NULL, 5, &av_task_handles.capture_task);
    if (ret != pdPASS) {
        ESP_LOGE(AV_LOG_TAG, "Failed to create capture task");
        av_task_handles.capture_task = NULL;
    }
}

void destroy_av_tasks(void) {
    if (av_task_handles.capture_task) {
        vTaskDelete(av_task_handles.capture_task);
        av_task_handles.capture_task = NULL;
        av_handles.capture_started = false;
    }
    if (av_task_handles.stream_task) {
        vTaskDelete(av_task_handles.stream_task);
        av_task_handles.stream_task = NULL;
        av_handles.streaming_started = false;
    }

    if (av_handles.video_sink_enabled) {
        esp_capture_sink_enable(av_handles.video_sink, ESP_CAPTURE_RUN_MODE_DISABLE);
        av_handles.video_sink_enabled = false;
    }

    if (av_handles.stream_sink_enabled) {
        esp_capture_sink_enable(av_handles.stream_sink, ESP_CAPTURE_RUN_MODE_DISABLE);
        av_handles.stream_sink_enabled = false;
    }

    if (av_handles.capture_initialized) {
        esp_capture_stop(av_handles.capture);
        av_handles.capture_initialized = false;
    }

    if (av_handles.capture) {
        esp_capture_close(av_handles.capture);
        av_handles.capture = NULL;
    }

    if (av_handles.audio_src) {
        esp_capture_delete_audio_i2s_src(av_handles.audio_src);
        av_handles.audio_src = NULL;
    }

    if (av_handles.video_src) {
        free(av_handles.video_src);
        i2c_del_master_bus(av_handles.sccb_i2c_bus);
        av_handles.video_src = NULL;
    }
}

esp_capture_err_t suspend_capture_task(void) {
    ESP_RETURN_ON_FALSE(av_task_handles.capture_task && av_handles.capture_initialized
        && av_handles.capture_started, ESP_ERR_INVALID_STATE, AV_VIDEO_TAG, "Capture task not running");

    vTaskSuspend(av_task_handles.capture_task);
    av_handles.capture_started = false;

    esp_capture_err_t ret = esp_capture_stop(av_handles.capture);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_VIDEO_TAG, "Failed to stop capture");
        return ret;
    }
    av_handles.capture_initialized = false;

    ret = esp_capture_sink_enable(av_handles.video_sink, ESP_CAPTURE_RUN_MODE_DISABLE);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_VIDEO_TAG, "Failed to disable video sink");
        return ret;
    }
    av_handles.video_sink_enabled = false;

    return ret;
}

esp_capture_err_t resume_capture_task(void) {
    ESP_RETURN_ON_FALSE(av_task_handles.capture_task, ESP_ERR_INVALID_STATE, AV_VIDEO_TAG, "Capture task not initialized");
    ESP_RETURN_ON_FALSE(!av_handles.capture_started, ESP_ERR_INVALID_STATE, AV_VIDEO_TAG, "Capture task already running");

    esp_capture_err_t ret = esp_capture_sink_enable(av_handles.video_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_VIDEO_TAG, "Failed to enable video sink");
        return ret;
    }
    av_handles.video_sink_enabled = true;

    if (!av_handles.capture_initialized) {
        ret = esp_capture_start(av_handles.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(AV_VIDEO_TAG, "Failed to start capture");
            return ret;
        }
        av_handles.capture_initialized = true;
    }

    vTaskResume(av_task_handles.capture_task);
    av_handles.capture_started = true;

    return ret;
}

esp_capture_err_t suspend_streaming_task(void) {
    ESP_RETURN_ON_FALSE(av_task_handles.stream_task && av_handles.capture_initialized
        && av_handles.streaming_started, ESP_ERR_INVALID_STATE, AV_VIDEO_TAG, "Streaming task not running");

    vTaskSuspend(av_task_handles.stream_task);
    av_handles.streaming_started = false;

    esp_capture_err_t ret = esp_capture_stop(av_handles.capture);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_VIDEO_TAG, "Failed to stop capture");
        return ret;
    }
    av_handles.capture_initialized = false;

    ret = esp_capture_sink_enable(av_handles.stream_sink, ESP_CAPTURE_RUN_MODE_DISABLE);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_VIDEO_TAG, "Failed to disable stream sink");
        return ret;
    }
    av_handles.stream_sink_enabled = false;

    return ret;
}

esp_capture_err_t resume_streaming_task(void) {
    ESP_RETURN_ON_FALSE(av_task_handles.stream_task, ESP_ERR_INVALID_STATE, AV_VIDEO_TAG, "Streaming task not initialized");
    ESP_RETURN_ON_FALSE(!av_handles.streaming_started, ESP_ERR_INVALID_STATE, AV_VIDEO_TAG, "Streaming task already running");

    esp_capture_err_t ret = esp_capture_sink_enable(av_handles.stream_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_VIDEO_TAG, "Failed to enable stream sink");
        return ret;
    }
    av_handles.stream_sink_enabled = true;

    if (!av_handles.capture_initialized) {
        ret = esp_capture_start(av_handles.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(AV_VIDEO_TAG, "Failed to start capture");
            return ret;
        }
        av_handles.capture_initialized = true;
    }

    vTaskResume(av_task_handles.stream_task);
    av_handles.streaming_started = true;

    return ret;
}
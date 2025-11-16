#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_log.h"
#include "esp_check.h"

#include "audio_i2s_src.h"
#include "impl/esp_capture_video_dvp_src.h"
#include "mp4_muxer.h"
#include "esp_audio_enc_default.h"

#include "video_src.h"

#include <sys/errno.h>
#include <sys/stat.h>

#include "../time/time_sync.h"
#include "../websocket/ws_stream.h"

av_handles_t av_handles = {0};

TaskHandle_t capture_task = NULL;

static int mp4_url_pattern(char *file_path, int len, int slice_idx)
{
    if (len <= 0) { return -1; }
    static char _filename[255];
    time_t current_time = get_unix_timestamp();
    struct tm *time_info = localtime(&current_time);
    static char timestamp[32] = {0};
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S_%Z", time_info);
    memset(_filename, 0, sizeof(_filename));
    snprintf(_filename, sizeof(_filename), "%s/capture-%s-%d.mp4", AV_CAPTURE_MP4_DIR, timestamp, slice_idx);
    strlcpy(file_path, _filename, len);
    return 0;
}

static esp_err_t setup_capture_pipeline(void) {
    esp_capture_cfg_t capture_cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_src = av_handles.audio_src,
        .video_src = av_handles.video_src,
    };

    ESP_RETURN_ON_FALSE(esp_capture_open(&capture_cfg, &av_handles.capture) == ESP_CAPTURE_ERR_OK && av_handles.capture,
                        ESP_FAIL, AV_LOG_TAG, "Failed to open capture instance");

    static esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {AUDIO_FORMAT, AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, AUDIO_BITS_PER_SAMPLE},
        .video_info = {VIDEO_FORMAT, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FPS},
    };

    ESP_RETURN_ON_FALSE(esp_capture_sink_setup(av_handles.capture, 0, &sink_cfg, &av_handles.video_sink) == ESP_CAPTURE_ERR_OK && av_handles.video_sink,
                        ESP_FAIL, AV_LOG_TAG, "Failed to setup capture sink");

    static mp4_muxer_config_t mp4_cfg = {
        .base_config = {
            .muxer_type = AV_MUXER_TYPE,
            .slice_duration = AV_CAPTURE_MP4_DURATION_MSEC,
            .url_pattern = mp4_url_pattern,
            .ram_cache_size = AV_MUXER_CACHE_SIZE,
        },
        .display_in_order = true,
        .moov_before_mdat = true,
    };

    static esp_capture_muxer_cfg_t muxer_cfg = {
        .base_config = &mp4_cfg.base_config,
        .cfg_size = sizeof(mp4_cfg),
    };

    esp_capture_err_t ret = esp_capture_sink_add_muxer(av_handles.video_sink, &muxer_cfg);
    ESP_RETURN_ON_FALSE(ret == ESP_CAPTURE_ERR_OK || ret == ESP_CAPTURE_ERR_INVALID_STATE,
                        ESP_FAIL, AV_LOG_TAG, "Failed to add MP4 muxer");

    esp_capture_sink_enable_muxer(av_handles.video_sink, true);
    return ESP_OK;
}

esp_err_t capture_setup(void) {
    ESP_LOGI(AV_LOG_TAG, "====== Capture setup started ======");

    ESP_RETURN_ON_FALSE(esp_audio_enc_register_default() == ESP_AUDIO_ERR_OK,
                        ESP_FAIL, AV_LOG_TAG, "Failed to register default audio encoders");

    ESP_RETURN_ON_FALSE(mp4_muxer_register() == ESP_MUXER_ERR_OK,
                        ESP_FAIL, AV_LOG_TAG, "Failed to register MP4 muxer");

    ESP_RETURN_ON_FALSE(mkdir(AV_CAPTURE_MP4_DIR, 0775) == 0 || errno == EEXIST,
                        ESP_FAIL, AV_LOG_TAG, "Failed to create directory for MP4 files");

    av_handles.audio_src = esp_capture_new_audio_i2s_src(NULL);
    ESP_RETURN_ON_FALSE(av_handles.audio_src, ESP_FAIL, AV_LOG_TAG, "Failed to create audio I2S source");

    i2c_master_bus_config_t i2c_mst_cfg = SCCB_DEFAULT();
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_mst_cfg, &av_handles.sccb_i2c_bus), AV_LOG_TAG, "Failed to install SCCB I2C master bus");

    esp_capture_video_dvp_src_cfg_t vid_cfg = DVP_SRC_DEFAULT(CAMERA_BUFFER_COUNT);
    av_handles.video_src = esp_capture_new_video_dvp_src(&vid_cfg);
    ESP_RETURN_ON_FALSE(av_handles.video_src, ESP_FAIL, AV_LOG_TAG, "Failed to create DVP video source");

    return ESP_OK;
}

static void start_capture(void* arg) {
    // ReSharper disable once CppTooWideScope
    esp_capture_stream_frame_t frame = {0};

    if (setup_capture_pipeline() != ESP_OK || 
        esp_capture_sink_enable(av_handles.video_sink, ESP_CAPTURE_RUN_MODE_ALWAYS) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(AV_LOG_TAG, "Failed to setup capture pipeline");
        vTaskDelete(NULL);
        return;
    }

    if (!av_handles.capture_initialized) {
        if (esp_capture_start(av_handles.capture) != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(AV_LOG_TAG, "Failed to start capture");
            vTaskDelete(NULL);
            return;
        }
        av_handles.capture_initialized = true;
    }

    av_handles.capture_started = true;
    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
        while (esp_capture_sink_acquire_frame(av_handles.video_sink, &frame, true) == ESP_CAPTURE_ERR_OK) {
            if (av_handles.streaming_enabled) {
                ws_stream_queue_frame(frame.stream_type, frame.data, frame.size, frame.pts);
            }
            esp_capture_sink_release_frame(av_handles.video_sink, &frame);
        }
        
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
        while (esp_capture_sink_acquire_frame(av_handles.video_sink, &frame, true) == ESP_CAPTURE_ERR_OK) {
            if (av_handles.streaming_enabled) {
                ws_stream_queue_frame(frame.stream_type, frame.data, frame.size, frame.pts);
            }
            esp_capture_sink_release_frame(av_handles.video_sink, &frame);
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void start_capture_task(void) {
    if (capture_task) {
        ESP_LOGW(AV_LOG_TAG, "Capture task already running");
        return;
    }
    
    if (xTaskCreate(start_capture, "av_capture_task", 16 * 1024, NULL, 5, &capture_task) != pdPASS) {
        ESP_LOGE(AV_LOG_TAG, "Failed to create capture task");
        capture_task = NULL;
    }
}

void destroy_av_tasks(void) {
    if (av_handles.capture_initialized) {
        esp_capture_stop(av_handles.capture);
        av_handles.capture_initialized = false;
        vTaskDelay(pdMS_TO_TICKS(500));
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

    if (capture_task) {
        vTaskDelete(capture_task);
        capture_task = NULL;
        av_handles.capture_started = false;
    }
}

esp_capture_err_t suspend_capture_task(void) {
    ESP_RETURN_ON_FALSE(capture_task && av_handles.capture_started,
                        ESP_ERR_INVALID_STATE, AV_VIDEO_TAG, "Capture task not running");

    vTaskSuspend(capture_task);
    av_handles.capture_started = false;

    ESP_RETURN_ON_ERROR(esp_capture_stop(av_handles.capture), AV_VIDEO_TAG, "Failed to stop capture");
    ESP_RETURN_ON_ERROR(esp_capture_close(av_handles.capture), AV_VIDEO_TAG, "Failed to close capture");

    av_handles.capture = NULL;
    av_handles.capture_initialized = false;
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t resume_capture_task(void) {
    ESP_RETURN_ON_FALSE(capture_task, ESP_ERR_INVALID_STATE, AV_VIDEO_TAG, "Capture task not initialized");
    ESP_RETURN_ON_FALSE(!av_handles.capture_started, ESP_ERR_INVALID_STATE, AV_VIDEO_TAG, "Capture task already running");

    ESP_RETURN_ON_ERROR(setup_capture_pipeline(), AV_VIDEO_TAG, "Failed to setup capture pipeline");
    ESP_RETURN_ON_ERROR(esp_capture_sink_enable(av_handles.video_sink, ESP_CAPTURE_RUN_MODE_ALWAYS), AV_VIDEO_TAG, "Failed to enable capture sink");

    if (!av_handles.capture_initialized) {
        ESP_RETURN_ON_ERROR(esp_capture_start(av_handles.capture), AV_VIDEO_TAG, "Failed to start capture");
        av_handles.capture_initialized = true;
    }

    vTaskResume(capture_task);
    av_handles.capture_started = true;

    return ESP_CAPTURE_ERR_OK;
}

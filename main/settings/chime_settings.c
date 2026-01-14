#include "chime_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define NVS_NAMESPACE "chime"
#define NVS_KEY_CHIME_INDEX "chime_idx"

static int s_chime_index = CHIME_DEFAULT_INDEX;

esp_err_t chime_settings_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    
    if (err == ESP_OK) {
        int32_t stored_index = CHIME_DEFAULT_INDEX;
        err = nvs_get_i32(handle, NVS_KEY_CHIME_INDEX, &stored_index);
        if (err == ESP_OK && stored_index >= CHIME_MIN_INDEX && stored_index <= CHIME_MAX_INDEX) {
            s_chime_index = (int)stored_index;
            ESP_LOGI(CHIME_SETTINGS_TAG, "Loaded chime index from NVS: %d", s_chime_index);
        } else {
            s_chime_index = CHIME_DEFAULT_INDEX;
            ESP_LOGI(CHIME_SETTINGS_TAG, "Using default chime index: %d", s_chime_index);
        }
        nvs_close(handle);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_chime_index = CHIME_DEFAULT_INDEX;
        ESP_LOGI(CHIME_SETTINGS_TAG, "No stored chime index, using default: %d", s_chime_index);
        err = ESP_OK;
    } else {
        ESP_LOGE(CHIME_SETTINGS_TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        s_chime_index = CHIME_DEFAULT_INDEX;
    }
    
    return ESP_OK;
}

int chime_settings_get_index(void)
{
    return s_chime_index;
}

esp_err_t chime_settings_set_index(int index)
{
    if (index < CHIME_MIN_INDEX || index > CHIME_MAX_INDEX) {
        ESP_LOGW(CHIME_SETTINGS_TAG, "Invalid chime index %d, must be %d-%d", 
                 index, CHIME_MIN_INDEX, CHIME_MAX_INDEX);
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(CHIME_SETTINGS_TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_i32(handle, NVS_KEY_CHIME_INDEX, (int32_t)index);
    if (err != ESP_OK) {
        ESP_LOGE(CHIME_SETTINGS_TAG, "Failed to write chime index: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err == ESP_OK) {
        s_chime_index = index;
        ESP_LOGI(CHIME_SETTINGS_TAG, "Chime index updated to: %d", s_chime_index);
    }
    
    return err;
}

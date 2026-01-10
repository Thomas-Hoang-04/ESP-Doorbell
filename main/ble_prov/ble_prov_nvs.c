#include "ble_prov_nvs.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ble_prov_nvs";

esp_err_t ble_prov_nvs_save_wifi(const char *ssid, const char *password) {
    nvs_handle_t handle;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_PROVISIONED, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save provisioned flag: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "WiFi credentials saved to NVS");
    return err;
}

esp_err_t ble_prov_nvs_load_wifi(char *ssid, size_t ssid_len, char *password, size_t pass_len) {
    nvs_handle_t handle;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_get_str(handle, NVS_KEY_PASS, password, &pass_len);
    nvs_close(handle);

    return err;
}

esp_err_t ble_prov_nvs_save_device(const char *device_id, const uint8_t *device_key, size_t key_len) {
    nvs_handle_t handle;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_DEVICE_ID, device_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save device ID: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY_DEVICE_KEY, device_key, key_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save device key: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Device credentials saved to NVS");
    return err;
}

esp_err_t ble_prov_nvs_load_device_key(uint8_t *device_key, size_t key_len) {
    nvs_handle_t handle;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, NVS_KEY_DEVICE_KEY, device_key, &key_len);
    nvs_close(handle);

    return err;
}

esp_err_t ble_prov_nvs_erase(void) {
    nvs_handle_t handle;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    ESP_LOGI(TAG, "NVS credentials erased");
    return err;
}

bool ble_prov_nvs_is_provisioned(void) {
    nvs_handle_t handle;
    uint8_t provisioned = 0;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_get_u8(handle, NVS_KEY_PROVISIONED, &provisioned);
    nvs_close(handle);

    return err == ESP_OK && provisioned == 1;
}

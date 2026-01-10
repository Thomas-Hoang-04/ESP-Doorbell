#include "ble_prov_gatt.h"
#include "ble_prov.h"
#include "ble_prov_nvs.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <string.h>

static const char *TAG = "ble_prov_gatt";

static const ble_uuid128_t prov_svc_uuid = BLE_UUID128_INIT(
    0xce, 0x7d, 0x56, 0xdf, 0x66, 0x11, 0x4e, 0xa2,
    0x9d, 0x4b, 0x7a, 0xc8, 0x77, 0xb4, 0x3f, 0xcb
);
static const ble_uuid16_t ssid_chr_uuid = BLE_UUID16_INIT(0xFFE1);
static const ble_uuid16_t password_chr_uuid = BLE_UUID16_INIT(0xFFE2);
static const ble_uuid16_t command_chr_uuid = BLE_UUID16_INIT(0xFFE3);
static const ble_uuid16_t status_chr_uuid = BLE_UUID16_INIT(0xFFE4);
static const ble_uuid16_t device_id_chr_uuid = BLE_UUID16_INIT(0xFFE5);
static const ble_uuid16_t device_key_chr_uuid = BLE_UUID16_INIT(0xFFE6);
static const ble_uuid16_t model_chr_uuid = BLE_UUID16_INIT(0xFFE7);
static const ble_uuid16_t firmware_chr_uuid = BLE_UUID16_INIT(0xFFE8);

static uint16_t status_chr_val_handle;
static uint16_t device_key_chr_val_handle;

static char pending_ssid[33] = {0};
static char pending_password[65] = {0};
static char device_id[40] = {0};
static uint8_t device_key[DEVICE_KEY_LENGTH] = {0};
static uint8_t prov_status = BLE_PROV_STATUS_IDLE;
static bool device_id_generated = false;

static uint16_t current_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool status_subscribed = false;
static bool device_key_subscribed = false;

static int ssid_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);
static int password_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static int command_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg);
static int status_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);
static int device_id_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);
static int device_key_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int model_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg);
static int firmware_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &prov_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &ssid_chr_uuid.u,
                .access_cb = ssid_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &password_chr_uuid.u,
                .access_cb = password_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &command_chr_uuid.u,
                .access_cb = command_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &status_chr_uuid.u,
                .access_cb = status_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &status_chr_val_handle,
            },
            {
                .uuid = &device_id_chr_uuid.u,
                .access_cb = device_id_chr_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &device_key_chr_uuid.u,
                .access_cb = device_key_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &device_key_chr_val_handle,
            },
            {
                .uuid = &model_chr_uuid.u,
                .access_cb = model_chr_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &firmware_chr_uuid.u,
                .access_cb = firmware_chr_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {0},
        },
    },
    {0},
};

static int ssid_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ctxt->om->om_len > 0 && ctxt->om->om_len <= 32) {
            memset(pending_ssid, 0, sizeof(pending_ssid));
            os_mbuf_copydata(ctxt->om, 0, ctxt->om->om_len, pending_ssid);
            ESP_LOGI(TAG, "Received SSID: %s", pending_ssid);
            return 0;
        }
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int password_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ctxt->om->om_len > 0 && ctxt->om->om_len <= 64) {
            memset(pending_password, 0, sizeof(pending_password));
            os_mbuf_copydata(ctxt->om, 0, ctxt->om->om_len, pending_password);
            ESP_LOGI(TAG, "Received password (length: %d)", ctxt->om->om_len);
            return 0;
        }
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int command_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t cmd = 0;
        os_mbuf_copydata(ctxt->om, 0, 1, &cmd);
        ESP_LOGI(TAG, "Received command: 0x%02X", cmd);

        if (cmd == 0x01 && ctxt->om->om_len == 1) {
            if (strlen(pending_ssid) > 0 && strlen(pending_password) > 0) {
                ESP_LOGI(TAG, "Starting WiFi connection...");
                prov_status = BLE_PROV_STATUS_CONNECTING;
                current_conn_handle = conn_handle;

                esp_err_t err = ble_prov_nvs_save_wifi(pending_ssid, pending_password);
                if (err == ESP_OK) {
                    ble_prov_nvs_save_device(device_id, device_key, DEVICE_KEY_LENGTH);
                }
            } else {
                ESP_LOGW(TAG, "SSID or password not set");
                prov_status = BLE_PROV_STATUS_FAILED;
            }
            return 0;
        } else if (cmd == 0x02) {
            ESP_LOGI(TAG, "Reset credentials command received");
            ble_prov_nvs_erase();
            return 0;
        }
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int status_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, &prov_status, sizeof(prov_status));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static void generate_device_id(void) {
    if (device_id_generated) return;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);

    snprintf(device_id, sizeof(device_id),
             "%02x%02x%02x%02x-%02x%02x-4%03u-8%03u-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5],
            (unsigned int)(esp_random() % 1000),
            (unsigned int)(esp_random() % 1000),
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (ble_prov_nvs_load_device_key(device_key, DEVICE_KEY_LENGTH) != ESP_OK) {
        esp_fill_random(device_key, DEVICE_KEY_LENGTH);
        ESP_LOGI(TAG, "Generated new device key");
    }

    device_id_generated = true;
    ESP_LOGI(TAG, "Device ID: %s", device_id);
}

static int device_id_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        generate_device_id();
        int rc = os_mbuf_append(ctxt->om, device_id, strlen(device_id));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int device_key_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        generate_device_id();
        int rc = os_mbuf_append(ctxt->om, device_key, DEVICE_KEY_LENGTH);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int model_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char *model = CONFIG_PROV_DEVICE_MODEL;
        int rc = os_mbuf_append(ctxt->om, model, strlen(model));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int firmware_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char *version = CONFIG_PROV_FIRMWARE_VERSION;
        int rc = os_mbuf_append(ctxt->om, version, strlen(version));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "Registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "Registered characteristic %s with def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "Registered descriptor %s with handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
        break;
    default:
        break;
    }
}

void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    if (event->subscribe.attr_handle == status_chr_val_handle) {
        status_subscribed = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "Status notifications %s", status_subscribed ? "enabled" : "disabled");
    } else if (event->subscribe.attr_handle == device_key_chr_val_handle) {
        device_key_subscribed = event->subscribe.cur_notify || event->subscribe.cur_indicate;
        ESP_LOGI(TAG, "DeviceKey notifications %s", device_key_subscribed ? "enabled" : "disabled");
    }
}

esp_err_t ble_prov_gatt_init(void) {
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "GATT server initialized");
    return ESP_OK;
}

void ble_prov_gatt_set_status(uint8_t status) {
    prov_status = status;
}

void ble_prov_gatt_notify_status(uint16_t conn_handle) {
    if (status_subscribed && conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&prov_status, sizeof(prov_status));
        if (om) {
            ble_gatts_notify_custom(conn_handle, status_chr_val_handle, om);
        }
    }
}

void ble_prov_gatt_notify_device_key(uint16_t conn_handle) {
    if (device_key_subscribed && conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(device_key, DEVICE_KEY_LENGTH);
        if (om) {
            ble_gatts_notify_custom(conn_handle, device_key_chr_val_handle, om);
            ESP_LOGI(TAG, "Device key notification sent");
        }
    }
}

void ble_prov_gatt_reset_state(void) {
    memset(pending_ssid, 0, sizeof(pending_ssid));
    memset(pending_password, 0, sizeof(pending_password));
    prov_status = BLE_PROV_STATUS_IDLE;
    current_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    status_subscribed = false;
    device_key_subscribed = false;
    ESP_LOGI(TAG, "GATT state reset");
}

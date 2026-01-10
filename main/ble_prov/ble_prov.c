#include "ble_prov.h"
#include "ble_prov_gatt.h"
#include "ble_prov_nvs.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include <string.h>
#include <inttypes.h>

static const char *TAG = "ble_prov";

static ble_prov_wifi_connected_cb_t wifi_connected_cb = NULL;
static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};
static uint16_t current_conn_handle = BLE_HS_CONN_HANDLE_NONE;

extern void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
extern void gatt_svr_subscribe_cb(struct ble_gap_event *event);

/* Declaration is missing from NimBLE headers; provided by ble_store_config.c */
void ble_store_config_init(void);

static void format_addr(char *addr_str, uint8_t addr[]) {
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static void start_advertising(void);
static int gap_event_handler(struct ble_gap_event *event, void *arg);

static void on_stack_reset(int reason) {
    ESP_LOGI(TAG, "NimBLE stack reset, reason: %d", reason);
}

static void on_stack_sync(void) {
    char addr_str[18] = {0};

    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "No available BT address");
        return;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer address type: %d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to copy device address: %d", rc);
        return;
    }

    format_addr(addr_str, addr_val);
    ESP_LOGI(TAG, "Device address: %s", addr_str);

    start_advertising();
}

static void start_advertising(void) {
    int rc;
    struct ble_hs_adv_fields adv_fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    struct ble_gap_adv_params adv_params = {0};
    const char *name;

    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    name = ble_svc_gap_device_name();
    if (name == NULL || name[0] == '\0') {
        name = "ESP32-Prov";
        ESP_LOGW(TAG, "BLE device name not set; using default '%s'", name);
    }
    size_t name_len = strlen(name);
    if (name_len > BLE_HS_ADV_MAX_FIELD_SZ) {
        ESP_LOGW(TAG, "Device name too long for scan response; truncating to %d",
                 BLE_HS_ADV_MAX_FIELD_SZ);
        name_len = BLE_HS_ADV_MAX_FIELD_SZ;
    }
    rsp_fields.name = (uint8_t *)name;
    rsp_fields.name_len = name_len;
    rsp_fields.name_is_complete = 1;

    static ble_uuid128_t prov_svc_uuid = BLE_UUID128_INIT(
        0xce, 0x7d, 0x56, 0xdf, 0x66, 0x11, 0x4e, 0xa2,
        0x9d, 0x4b, 0x7a, 0xc8, 0x77, 0xb4, 0x3f, 0xcb
    );
    adv_fields.uuids128 = &prov_svc_uuid;
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;

    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    adv_fields.tx_pwr_lvl_is_present = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data: %d", rc);
        return;
    }

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan response data: %d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(150);

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "Advertising started as '%s'", name);
}

static int gap_event_handler(struct ble_gap_event *event, void *arg) {
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0) {
                current_conn_handle = event->connect.conn_handle;
                ble_prov_gatt_set_status(BLE_PROV_STATUS_CONNECTING);
                ESP_LOGI(TAG, "Connected, handle=%d", current_conn_handle);
            }
        } else {
            ble_prov_gatt_set_status(BLE_PROV_STATUS_FAILED);
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        current_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_prov_gatt_set_status(BLE_PROV_STATUS_IDLE);
        ble_prov_gatt_reset_state();
        start_advertising();
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "Connection updated; status=%d", event->conn_update.status);
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertise complete; reason=%d", event->adv_complete.reason);
        start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
        gatt_svr_subscribe_cb(event);
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update; conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            ESP_LOGI(TAG, "Connection encrypted");
        } else {
            ESP_LOGE(TAG, "Encryption failed; status=%d", event->enc_change.status);
        }
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            struct ble_sm_io pkey = {0};
            pkey.action = event->passkey.params.action;
            pkey.numcmp_accept = 1;
            ESP_LOGI(TAG, "Numeric comparison: auto-accepting (user confirms on phone)");
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to accept Numeric Comparison: %d", rc);
            }
        }
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        ESP_LOGI(TAG, "Repeat pairing");
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        ESP_LOGI(TAG, "Unknown event type: %d", event->type);
        break;
    }

    return 0;
}

static void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void nimble_host_config_init(void) {
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_store_config_init();
}

esp_err_t ble_prov_init(ble_prov_wifi_connected_cb_t on_connected) {
    wifi_connected_cb = on_connected;

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE port: %d", ret);
        return ret;
    }

    ret = ble_prov_gatt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GATT server");
        return ret;
    }

    nimble_host_config_init();

    ESP_LOGI(TAG, "BLE provisioning initialized");
    return ESP_OK;
}

esp_err_t ble_prov_start(void) {
    nimble_port_freertos_init(nimble_host_task);
    ESP_LOGI(TAG, "BLE provisioning started");
    return ESP_OK;
}

esp_err_t ble_prov_stop(void) {
    int rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
        ESP_LOGI(TAG, "BLE provisioning stopped");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to stop NimBLE: %d", rc);
    return ESP_FAIL;
}

bool ble_prov_is_provisioned(void) {
    return ble_prov_nvs_is_provisioned();
}

esp_err_t ble_prov_reset_credentials(void) {
    return ble_prov_nvs_erase();
}

ble_prov_status_t ble_prov_get_status(void) {
    return (ble_prov_status_t)ble_prov_gatt_get_status();
}

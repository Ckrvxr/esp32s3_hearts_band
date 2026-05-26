#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble.h"

static const char *TAG = "BLE";

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_chr_handle;

// ── GATT Access Callback ──────────────────────────────────────────

static int ble_svc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint8_t buf[256];
        uint16_t len;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &len);
        if (rc == 0) {
            buf[len] = '\0';
            ESP_LOGI(TAG, "RX[%u]: %s", len, buf);
        }
        return rc;
    }

    default:
        break;
    }

    return 0;
}

// ── GATT Service Table ────────────────────────────────────────────

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFE0),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid = BLE_UUID16_DECLARE(0xFFE1),
            .access_cb = ble_svc_access_cb,
            .flags = BLE_GATT_CHR_F_READ |
                     BLE_GATT_CHR_F_WRITE |
                     BLE_GATT_CHR_F_NOTIFY,
            .val_handle = &g_chr_handle,
        }, { 0 } },
    }, { 0 }
};

// ── GAP Event Callback ────────────────────────────────────────────

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected, conn_handle=%u", g_conn_handle);
        } else {
            ESP_LOGE(TAG, "Connect failed, status=%d", event->connect.status);
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected, reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          NULL, ble_gap_event_cb, NULL);
        break;

    default:
        break;
    }

    return 0;
}

// ── Advertising ───────────────────────────────────────────────────

static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params = { 0 };
    struct ble_hs_adv_fields fields = { 0 };

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)BLE_DEVICE_NAME;
    fields.name_len = strlen(BLE_DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc_set = ble_gap_adv_set_fields(&fields);
    if (rc_set != 0) {
        ESP_LOGE(TAG, "Set adv fields failed, rc=%d", rc_set);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &adv_params, ble_gap_event_cb, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Advertising started");
    } else {
        ESP_LOGE(TAG, "Advertising start failed, rc=%d", rc);
    }
}

// ── Host Sync Callback ────────────────────────────────────────────

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Address setup failed, rc=%d", rc);
        return;
    }

    ble_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset, reason=%d", reason);
}

// ── Host Task ─────────────────────────────────────────────────────

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ── GATT Init ─────────────────────────────────────────────────────

static void gatt_svr_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count failed: %d", rc);
        return;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add failed: %d", rc);
    }
}

// ── Public API ────────────────────────────────────────────────────

void Ble_Driver_Init(void)
{
    int rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    nimble_port_init();

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    gatt_svr_init();

    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Device name set failed: %d", rc);
    }

    extern void ble_store_config_init(void);
    ble_store_config_init();

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "Driver initialized");
}

void Ble_Driver_Send(const uint8_t *data, uint16_t len)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) return;

    ble_gatts_notify_custom(g_conn_handle, g_chr_handle, om);
}

bool Ble_Driver_IsConnected(void)
{
    return g_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

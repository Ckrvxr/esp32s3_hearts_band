#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble.h"
#include "display.h"
#include "timer.h"

// Forward declarations for JSON helpers (defined below)
static const char *json_get_string(const char *json, const char *key, char *out, size_t out_size);
static bool json_get_int(const char *json, const char *key, int *out);

static const char *TAG = "BLE";

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_chr_handle;

bool g_ble_connected;

// ── GATT Access Callback ──────────────────────────────────────────
// Handles read/write operations on the characteristic.
static int ble_svc_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
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

            const char *raw = (const char *)buf;
            char cmd[32] = {0};
            if (!json_get_string(raw, "cmd", cmd, sizeof(cmd))) {
                return rc;
            }

            if (strcmp(cmd, "SYNC_TIME") == 0) {
                ESP_LOGI(TAG, "SYNC_TIME received");
                Ble_Driver_SendAck("sync", NULL, NULL, 0);

            } else if (strcmp(cmd, "SET_TIMER") == 0) {
                char action[16] = {0}, uuid[64] = {0}, type[16] = {0};
                int time_val = 0;

                json_get_string(raw, "action", action, sizeof(action));
                json_get_string(raw, "uuid", uuid, sizeof(uuid));
                json_get_string(raw, "type", type, sizeof(type));
                json_get_int(raw, "time", &time_val);

                if (action[0] && uuid[0] && type[0]) {
                    TimerType_t tt = (strcmp(type, "medicine") == 0)
                                     ? TIMER_MEDICINE : TIMER_DRINK;
                    const char *ts = (tt == TIMER_MEDICINE) ? "medicine" : "drink";
                    if (strcmp(action, "set") == 0 && time_val > 0) {
                        Timer_Set(tt, (uint32_t)time_val);
                        Timer_PushConfirm(tt, true, (uint32_t)time_val);
                        if (currentState != STATE_TIMER_DONE)
                            currentState = STATE_TIMER_SET;
                        Ble_Driver_SendAck("set_timer", uuid, ts, time_val);
                    } else if (strcmp(action, "cancel") == 0) {
                        if (Timer_IsActive(tt)) {
                            Timer_Cancel(tt);
                            Timer_PushConfirm(tt, false, 0);
                            if (currentState != STATE_TIMER_DONE)
                                currentState = STATE_TIMER_SET;
                            Ble_Driver_SendAck("cancel_timer", uuid, ts, 0);
                        } else {
                            ESP_LOGI(TAG, "ignored cancel for %s (not active)", ts);
                        }
                    }
                }
            }
        }
        return rc;
    }

    default:
        break;
    }

    return 0;
}

// ── GATT Service Table ────────────────────────────────────────────
// Service 0xFFE0, Characteristic 0xFFE1 (open access).
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

// Forward declaration for GAP event callback (used by advertising)
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);

// ── Advertising ───────────────────────────────────────────────────
static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params = { 0 };
    struct ble_hs_adv_fields fields = { 0 };

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.uuids16 = (ble_uuid16_t[]) { BLE_UUID16_INIT(0xFFE0) };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
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

// ── GAP Event Callback ────────────────────────────────────────────
int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            g_ble_connected = true;
            ESP_LOGI(TAG, "Connected, conn_handle=%u", g_conn_handle);
            ble_gattc_exchange_mtu(g_conn_handle, NULL, NULL);
        } else {
            ESP_LOGE(TAG, "Connect failed, status=%d", event->connect.status);
            ble_advertise();
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected, reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_ble_connected = false;
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change: status=%d", event->enc_change.status);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe: conn_handle=%d attr_handle=%d reason=%d "
                 "prev_notify=%d cur_notify=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.reason,
                 event->subscribe.prev_notify,
                 event->subscribe.cur_notify);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        ESP_LOGI(TAG, "Notify TX: conn_handle=%d status=%d",
                 event->notify_tx.conn_handle, event->notify_tx.status);
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "Connection update: status=%d",
                 event->conn_update.status);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete: reason=%d",
                 event->adv_complete.reason);
        ble_advertise();
        return 0;

    default:
        ESP_LOGD(TAG, "GAP event: type=%d", event->type);
        return 0;
    }
}

// ── Host Sync / Reset ─────────────────────────────────────────────
static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Address setup failed, rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "Host synced");
    ble_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset, reason=%d", reason);
}

static void ble_gatts_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "GATT service registered");
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "GATT characteristic registered, handle=%d",
                 ctxt->chr.val_handle);
        break;
    default:
        break;
    }
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

// ── Passkey Display ──────────────────────────────────────────────
void Ble_Driver_GetMac(char *buf, size_t len)
{
    uint8_t mac[6];
    if (ble_hs_id_copy_addr(BLE_OWN_ADDR_PUBLIC, mac, NULL) == 0) {
        snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        snprintf(buf, len, "N/A");
    }
}

void Ble_Driver_ClearBonds(void)
{
    ESP_LOGI(TAG, "Clearing all bonds...");
    ble_store_clear();
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_conn_handle, 0x13);
    } else {
        ble_advertise();
    }
    ESP_LOGI(TAG, "Bonds cleared");
}

// ── Public API ────────────────────────────────────────────────────
void Ble_Driver_Init(void)
{
    // Initialize NVS
    int rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Initialize NimBLE stack
    nimble_port_init();

    // Host callbacks
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = ble_gatts_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // No security (open access, no bonding/pairing)
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;

    // Register GATT services
    gatt_svr_init();

    // Set device name (appears in scan list)
    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Device name set failed: %d", rc);
    }

    // Initialize NimBLE NVS store (persistent bonding storage)
    extern void ble_store_config_init(void);
    ble_store_config_init();

    // Start NimBLE host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "Driver initialized");
}

void Ble_Driver_Send(const uint8_t *data, uint16_t len)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;

    ESP_LOGI(TAG, "TX[%u]: %.*s", len, len, (const char *)data);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om) ble_gatts_notify_custom(g_conn_handle, g_chr_handle, om);
}

bool Ble_Driver_IsConnected(void)
{
    return g_ble_connected;
}

// ── JSON Helpers ───────────────────────────────────────────────────
// Extract string value of "key" from JSON string.
//   json:   {"key":"value", ...}
//   search: "key":" then copy until closing "
static const char *json_get_string(const char *json, const char *key, char *out, size_t out_size)
{
    if (!json || !key || !out || out_size == 0) return NULL;

    char pattern[64];
    int n = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (n <= 0 || (size_t)n >= sizeof(pattern)) return NULL;

    const char *p = strstr(json, pattern);
    if (!p) return NULL;

    p += n;
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return (i > 0) ? out : NULL;
}

// Extract integer value of "key" from JSON string.
static bool json_get_int(const char *json, const char *key, int *out)
{
    if (!json || !key || !out) return false;

    char pattern[64];
    int n = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (n <= 0 || (size_t)n >= sizeof(pattern)) return false;

    const char *p = strstr(json, pattern);
    if (!p) return false;

    p += n;
    while (*p == ' ' || *p == '\t') p++;

    int sign = 1;
    if (*p == '-') { sign = -1; p++; }

    int val = 0;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    *out = val * sign;
    return true;
}

// ── JSON Send Helpers ──────────────────────────────────────────────
void Ble_Driver_SendAck(const char *ack_type, const char *uuid, const char *type, int time)
{
    char buf[256];
    int n;

    if (uuid && type && time > 0) {
        n = snprintf(buf, sizeof(buf),
            "{\"cmd\":\"ACK\",\"ack_type\":\"%s\",\"uuid\":\"%s\",\"type\":\"%s\",\"time\":%d}",
            ack_type, uuid, type, time);
    } else if (uuid && type) {
        n = snprintf(buf, sizeof(buf),
            "{\"cmd\":\"ACK\",\"ack_type\":\"%s\",\"uuid\":\"%s\",\"type\":\"%s\"}",
            ack_type, uuid, type);
    } else {
        n = snprintf(buf, sizeof(buf),
            "{\"cmd\":\"ACK\",\"ack_type\":\"%s\"}",
            ack_type);
    }

    if (n > 0) {
        Ble_Driver_Send((const uint8_t *)buf, n);
        ESP_LOGI(TAG, "Sent ACK(%s)", ack_type);
    }
}

void Ble_Driver_ReportHealth(uint8_t hr, uint8_t spo2)
{
    if (!g_ble_connected) return;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "{\"spo2\":%u,\"heart\":%u}", spo2, hr);
    if (n > 0) {
        Ble_Driver_Send((const uint8_t *)buf, n);
    }
}

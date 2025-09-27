#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "app_state.h"     
#include "alarm_task.h"    
#include "ble_alarm.h"

static const char *TAG = "BLE_ALARM";

#define BLE_SVC_UUID            0xFFF0
#define BLE_CHR_ALARM_TIME_UUID 0xFFF1  // R/W: time
#define BLE_CHR_RINGING_UUID    0xFFF2  // R/Notify: 0|1
#define BLE_CHR_COMMAND_UUID    0xFFF3  // W: 0=STOP, 1=ENABLE, 2=DISABLE

static uint16_t s_conn_handle = 0;
static uint16_t h_alarm_time;
static uint16_t h_ringing;
static uint16_t h_command;


static int read_alarm_time(uint8_t *buf, uint16_t maxlen) {
    if (maxlen < 2) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    buf[0] = (uint8_t)s_alarm_hour;
    buf[1] = (uint8_t)s_alarm_min;
    return 2;
}

void ble_alarm_notify_ringing(uint8_t st) {
    if (s_conn_handle == 0) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&st, 1);
    if (om) ble_gatts_notify_custom(s_conn_handle, h_ringing, om);
}

void ble_alarm_refresh_alarm_time(void) {
}


static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)arg;
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

    switch (uuid16) {
    case BLE_CHR_ALARM_TIME_UUID:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            uint8_t tmp[2];
            int n = read_alarm_time(tmp, sizeof(tmp));
            if (n < 0) return BLE_ATT_ERR_UNLIKELY;
            return os_mbuf_append(ctxt->om, tmp, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            if (OS_MBUF_PKTLEN(ctxt->om) != 2) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            uint8_t buf[2]; os_mbuf_copydata(ctxt->om, 0, 2, buf);
            int h = buf[0], m = buf[1];
            if (h < 0 || h > 23 || m < 0 || m > 59) return BLE_ATT_ERR_UNLIKELY;

            s_alarm_hour = h; s_alarm_min = m;  
            ESP_LOGI(TAG, "BLE set alarm -> %02d:%02d", h, m);
            return 0;
        }
        break;

    case BLE_CHR_RINGING_UUID:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            uint8_t st = s_alarm_ringing ? 1 : 0;   
            return os_mbuf_append(ctxt->om, &st, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        break;

    case BLE_CHR_COMMAND_UUID:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            if (OS_MBUF_PKTLEN(ctxt->om) != 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            uint8_t cmd; os_mbuf_copydata(ctxt->om, 0, 1, &cmd);
            if (cmd == 0) {            
                alarm_send_stop_ring(); 
            } else if (cmd == 1) {    
                s_alarm_enabled = true;
            } else if (cmd == 2) {     
                s_alarm_enabled = false;
            } else {
                return BLE_ATT_ERR_UNLIKELY;
            }
            return 0;
        }
        break;
    default:
        break;
    }
    return BLE_ATT_ERR_UNLIKELY;
}


static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(BLE_CHR_ALARM_TIME_UUID),
              .access_cb = gatt_access_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
              .val_handle = &h_alarm_time },
            { .uuid = BLE_UUID16_DECLARE(BLE_CHR_RINGING_UUID),
              .access_cb = gatt_access_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_ringing },
            { .uuid = BLE_UUID16_DECLARE(BLE_CHR_COMMAND_UUID),
              .access_cb = gatt_access_cb,
              .flags = BLE_GATT_CHR_F_WRITE,
              .val_handle = &h_command },
            { 0 }
        }
    },
    { 0 }
};


static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE connected. conn=%u", s_conn_handle);
        } else {
            s_conn_handle = 0;
            ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                &(struct ble_gap_adv_params){ .conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN },
                gap_event_cb, NULL);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = 0;
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
            &(struct ble_gap_adv_params){ .conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN },
            gap_event_cb, NULL);
        break;
    default: break;
    }
    return 0;
}

static void ble_advertise(void) {
    struct ble_hs_adv_fields fields = {0};
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t*)name;
    fields.name_len = (uint8_t)strlen(name);
    fields.name_is_complete = 1;

    uint16_t svc_uuid = BLE_SVC_UUID;
    fields.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(svc_uuid) };
    fields.num_uuids16 = 1; fields.uuids16_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params advp = {0};
    advp.conn_mode = BLE_GAP_CONN_MODE_UND; advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &advp, gap_event_cb, NULL);
}

static void on_sync(void) {
    uint8_t addr_val[6] = {0};
    ble_hs_id_infer_auto(0, &addr_val[0]);
    ble_svc_gap_device_name_set("ESP-AlarmClock");
    ble_advertise();
}
static void on_reset(int reason) { ESP_LOGW(TAG, "BLE reset, reason=%d", reason); }

static void host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_alarm_init(void)
{
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb  = on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "BLE init done, advertising...");
    return ESP_OK;
}

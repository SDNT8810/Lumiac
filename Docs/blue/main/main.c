#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh_api.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "media_keys.h"
#include "dashboard_data.h"

#define WIFI_SSID       "Lumiac"
#define WIFI_PASSWORD   "12345678"
#define MAX_DEVICES     24
#define LOG_LINES       60
#define LOG_LINE_LEN    150
#define SCAN_INQ_LEN    8   /* 8 * 1.28 s */

static const char *TAG = "LUMIAC";

typedef struct {
    bool used;
    esp_bd_addr_t bda;
    char name[64];
    int8_t rssi;
    uint32_t cod;
} bt_device_t;

static bt_device_t g_devices[MAX_DEVICES];
static bool g_scanning = false;
static bool g_bt_ready = false;
static bool g_connected = false;
static bool g_connecting = false;
static bool g_connect_issued = false;
static uint32_t g_connect_started;
static esp_bd_addr_t g_pending_bda;
static char g_status_message[180] = "Put the remote in pairing mode, then select Connect.";
static char g_error[180] = "";
static media_map_t g_media_map;
static media_tracker_t g_tracker;
static uint8_t g_report_keys[256];
static uint32_t g_report_count, g_decoded_count;
static unsigned g_descriptor_bytes;
#define KEY_EVENTS 40
typedef struct { uint32_t seq, time_ms, duration; unsigned key; media_action_t action; } key_event_t;
static key_event_t g_key_events[KEY_EVENTS];
static uint32_t g_event_seq;
static size_t g_event_write;
static esp_bd_addr_t g_connected_bda = {0};
static char g_connected_name[64] = "";
static char g_last_key[64] = "Waiting for key...";
static char g_last_raw[96] = "-";
static char g_logs[LOG_LINES][LOG_LINE_LEN];
static uint32_t g_log_seq[LOG_LINES];
static uint32_t g_next_log_seq = 1;
static size_t g_log_write = 0;
static SemaphoreHandle_t g_lock;
static httpd_handle_t g_http = NULL;

static void state_lock(void)   { if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY); }
static void state_unlock(void) { if (g_lock) xSemaphoreGive(g_lock); }

static void addr_to_str(const uint8_t *bda, char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Called while holding g_lock. */
static void append_log_locked(const char *line) {
    strlcpy(g_logs[g_log_write], line, LOG_LINE_LEN);
    g_log_seq[g_log_write] = g_next_log_seq++;
    g_log_write = (g_log_write + 1) % LOG_LINES;
}

static void key_event(unsigned key, media_action_t action, uint32_t duration, void *ctx) {
    (void)ctx;
    /* Live indicators use tracker state; history contains one completed gesture. */
    if (action == MEDIA_DOWN || action == MEDIA_HOLD) return;
    g_key_events[g_event_write] = (key_event_t){++g_event_seq, now_ms(), duration, key, action};
    g_event_write = (g_event_write + 1) % KEY_EVENTS;
    snprintf(g_last_key, sizeof(g_last_key), "%s: %s (%lu ms)", media_key_names[key],
             action == MEDIA_HOLD_END ? "HOLD" : media_action_names[action], (unsigned long)duration);
    append_log_locked(g_last_key);
    ESP_LOGI(TAG, "KEY %s", g_last_key);
}

static void add_log(const char *fmt, ...) {
    char line[LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    state_lock();
    strlcpy(g_logs[g_log_write], line, LOG_LINE_LEN);
    g_log_seq[g_log_write] = g_next_log_seq++;
    g_log_write = (g_log_write + 1) % LOG_LINES;
    state_unlock();
    ESP_LOGI(TAG, "%s", line);
}

static void clear_devices(void) {
    state_lock();
    memset(g_devices, 0, sizeof(g_devices));
    state_unlock();
}

static int find_device_locked(const uint8_t *bda) {
    for (int i = 0; i < MAX_DEVICES; ++i) {
        if (g_devices[i].used && memcmp(g_devices[i].bda, bda, ESP_BD_ADDR_LEN) == 0) return i;
    }
    return -1;
}

static int get_or_add_device_locked(const uint8_t *bda) {
    int idx = find_device_locked(bda);
    if (idx >= 0) return idx;
    for (int i = 0; i < MAX_DEVICES; ++i) {
        if (!g_devices[i].used) {
            g_devices[i].used = true;
            memcpy(g_devices[i].bda, bda, ESP_BD_ADDR_LEN);
            strlcpy(g_devices[i].name, "Unknown", sizeof(g_devices[i].name));
            g_devices[i].rssi = -127;
            return i;
        }
    }
    return -1;
}

static bool name_from_eir(uint8_t *eir, char *name, size_t name_size) {
    if (!eir || !name || name_size < 2) return false;
    uint8_t len = 0;
    uint8_t *p = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
    if (!p) p = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
    if (!p || !len) return false;
    size_t n = len < name_size - 1 ? len : name_size - 1;
    memcpy(name, p, n);
    name[n] = 0;
    return true;
}

static void handle_hid_bytes(const uint8_t *data, uint16_t len) {
    if (!data || !len) return;
    char raw[96] = {0};
    size_t pos = 0;
    for (uint16_t i = 0; i < len && pos + 4 < sizeof(raw); ++i)
        pos += snprintf(raw + pos, sizeof(raw) - pos, "%02X%s", data[i], i + 1 < len ? " " : "");
    state_lock();
    strlcpy(g_last_raw, raw, sizeof(g_last_raw));
    g_report_count++;
    uint8_t down, covered;
    bool decoded = media_decode(&g_media_map, data, len, &down, &covered);
    if (decoded) {
        g_decoded_count++;
        g_report_keys[g_media_map.report_ids ? data[0] : 0] = down;
        uint8_t combined = 0;
        for (unsigned i = 0; i < 256; i++) combined |= g_report_keys[i];
        media_update(&g_tracker, combined, 0x1f, now_ms(), key_event, NULL);
    }
    state_unlock();
    add_log("HID %s [%u]: %s", decoded ? "input" : "unmapped", len, raw);
}

static void hidh_cb(esp_hidh_cb_event_t event, esp_hidh_cb_param_t *param) {
    switch (event) {
        case ESP_HIDH_INIT_EVT:
            state_lock(); g_bt_ready = (param->init.status == ESP_HIDH_OK); state_unlock();
            add_log("Bluetooth HID host init: %s", param->init.status == ESP_HIDH_OK ? "OK" : "FAILED");
            break;
        case ESP_HIDH_OPEN_EVT: {
            if (param->open.status == ESP_HIDH_OK && param->open.conn_status == ESP_HIDH_CONN_STATE_CONNECTING) {
                add_log("HID link is connecting; waiting for pairing and service discovery");
                break;
            }
            char mac[18]; addr_to_str(param->open.bd_addr, mac);
            bool ok = (param->open.status == ESP_HIDH_OK && param->open.conn_status == ESP_HIDH_CONN_STATE_CONNECTED);
            state_lock();
            g_connected = ok;
            g_connecting = g_connect_issued = false;
            memset(&g_media_map, 0, sizeof(g_media_map));
            memset(g_report_keys, 0, sizeof(g_report_keys));
            media_cancel(&g_tracker, now_ms(), key_event, NULL);
            g_descriptor_bytes = 0;
            if (ok) {
                g_error[0] = 0;
                strlcpy(g_status_message, "Connected. Waiting for the remote's report descriptor.", sizeof(g_status_message));
                memcpy(g_connected_bda, param->open.bd_addr, ESP_BD_ADDR_LEN);
                int idx = find_device_locked(param->open.bd_addr);
                strlcpy(g_connected_name, idx >= 0 ? g_devices[idx].name : "Bluetooth HID", sizeof(g_connected_name));
            } else {
                snprintf(g_error, sizeof(g_error), "Connection failed (HID status %d). Put the remote in pairing mode and disconnect it from other hosts.", param->open.status);
                strlcpy(g_status_message, g_error, sizeof(g_status_message));
            }
            state_unlock();
            if (ok) {
                esp_err_t err = esp_bt_hid_host_set_protocol(param->open.bd_addr, ESP_HIDH_REPORT_MODE);
                if (err != ESP_OK) add_log("Could not request report protocol: %s", esp_err_to_name(err));
            }
            add_log("HID connection %s: %s (status=%d)", ok ? "OPEN" : "FAILED", mac, param->open.status);
            break;
        }
        case ESP_HIDH_CLOSE_EVT:
            state_lock();
            g_connected = false;
            g_connecting = g_connect_issued = false;
            media_cancel(&g_tracker, now_ms(), key_event, NULL);
            memset(g_report_keys, 0, sizeof(g_report_keys));
            memset(&g_media_map, 0, sizeof(g_media_map));
            snprintf(g_status_message, sizeof(g_status_message), "Disconnected (reason %u). Wake the remote and reconnect.", param->close.reason);
            g_connected_name[0] = 0;
            memset(g_connected_bda, 0, ESP_BD_ADDR_LEN);
            state_unlock();
            add_log("HID disconnected (status=%d reason=%u)", param->close.status, param->close.reason);
            break;
        case ESP_HIDH_DATA_IND_EVT:
            if (param->data_ind.status == ESP_HIDH_OK) handle_hid_bytes(param->data_ind.data, param->data_ind.len);
            break;
        case ESP_HIDH_GET_DSCP_EVT: {
            state_lock();
            g_descriptor_bytes = param->dscp.dl_len;
            bool parsed = param->dscp.status == ESP_HIDH_OK && media_parse_descriptor(&g_media_map, param->dscp.dsc_list, param->dscp.dl_len);
            unsigned fields = parsed ? g_media_map.count : 0;
            strlcpy(g_status_message, fields ? "Connected. Press a media key; hold for 650 ms to detect HOLD." :
                    "Connected, but the media report format is unsupported. Raw input is still captured.", sizeof(g_status_message));
            state_unlock();
            add_log("Media descriptor: %s, %u mapped fields", parsed ? "parsed" : "unsupported", fields);
            for (unsigned offset = 0; param->dscp.dsc_list && offset < param->dscp.dl_len; offset += 32) {
                char hex[97] = {0}; size_t pos = 0;
                for (unsigned i = offset; i < offset + 32 && i < param->dscp.dl_len; i++)
                    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", param->dscp.dsc_list[i]);
                add_log("Descriptor %u: %s", offset, hex);
            }
            add_log("HID descriptor: vendor=0x%04X product=0x%04X version=0x%04X len=%u",
                    param->dscp.vendor_id, param->dscp.product_id, param->dscp.version, param->dscp.dl_len);
            break;
        }
        case ESP_HIDH_SET_PROTO_EVT:
            add_log("Report protocol result: %d", param->set_proto.status);
            break;
        default:
            break;
    }
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            char tmp_name[64] = "";
            int8_t rssi = -127;
            uint32_t cod = 0;
            for (int i = 0; i < param->disc_res.num_prop; ++i) {
                esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
                if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME && p->val) {
                    size_t n = p->len > 0 && (size_t)p->len < sizeof(tmp_name) - 1 ? (size_t)p->len : sizeof(tmp_name) - 1;
                    memcpy(tmp_name, p->val, n); tmp_name[n] = 0;
                } else if (p->type == ESP_BT_GAP_DEV_PROP_RSSI && p->val) {
                    rssi = *(int8_t *)p->val;
                } else if (p->type == ESP_BT_GAP_DEV_PROP_COD && p->val) {
                    memcpy(&cod, p->val, sizeof(cod));
                } else if (p->type == ESP_BT_GAP_DEV_PROP_EIR && p->val && tmp_name[0] == 0) {
                    name_from_eir((uint8_t *)p->val, tmp_name, sizeof(tmp_name));
                }
            }
            state_lock();
            int idx = get_or_add_device_locked(param->disc_res.bda);
            if (idx >= 0) {
                if (tmp_name[0]) strlcpy(g_devices[idx].name, tmp_name, sizeof(g_devices[idx].name));
                if (rssi != -127) g_devices[idx].rssi = rssi;
                g_devices[idx].cod = cod;
            }
            state_unlock();
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            state_lock(); g_scanning = (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED); state_unlock();
            add_log("Bluetooth scan %s", param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED ? "started" : "finished");
            break;
        case ESP_BT_GAP_AUTH_CMPL_EVT: {
            char mac[18]; addr_to_str(param->auth_cmpl.bda, mac);
            if (param->auth_cmpl.stat != ESP_BT_STATUS_SUCCESS) {
                state_lock();
                snprintf(g_error, sizeof(g_error), "Pairing failed (BT status %d). Re-enter pairing mode; forget its old pairing on your phone.", param->auth_cmpl.stat);
                state_unlock();
            }
            add_log("Pairing/auth %s with %s (%s)",
                    param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS ? "SUCCESS" : "FAILED",
                    param->auth_cmpl.device_name[0] ? (char *)param->auth_cmpl.device_name : "device", mac);
            break;
        }
        case ESP_BT_GAP_PIN_REQ_EVT: {
            esp_bt_pin_code_t pin = {'0','0','0','0'};
            if (param->pin_req.min_16_digit) {
                esp_bt_pin_code_t pin16;
                memset(pin16, '0', sizeof(pin16));
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin16);
                add_log("Pairing requested 16-digit PIN; replied with zeros");
            } else {
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
                add_log("Pairing PIN requested; replied 0000");
            }
            break;
        }
        case ESP_BT_GAP_CFM_REQ_EVT:
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            add_log("SSP confirmation accepted (passkey %lu)", (unsigned long)param->cfm_req.num_val);
            break;
        case ESP_BT_GAP_KEY_NOTIF_EVT:
            add_log("Bluetooth passkey shown: %06lu", (unsigned long)param->key_notif.passkey);
            break;
        default:
            break;
    }
}

static esp_err_t start_scan(void) {
    state_lock();
    bool ready = g_bt_ready, busy = g_connecting || g_connected, scanning = g_scanning;
    if (ready && !busy && !scanning) g_scanning = true;
    state_unlock();
    if (!ready || busy) return ESP_ERR_INVALID_STATE;
    if (scanning) return ESP_OK;
    clear_devices();
    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, SCAN_INQ_LEN, 0);
    if (err != ESP_OK) {
        state_lock(); g_scanning = false; state_unlock();
        add_log("Could not start scan: %s", esp_err_to_name(err));
    }
    return err;
}

static void control_task(void *arg) {
    (void)arg;
    for (;;) {
        esp_bd_addr_t target;
        bool issue = false, timeout = false;
        uint32_t now = now_ms();
        state_lock();
        for (unsigned k = 0; k < MEDIA_KEY_COUNT; k++)
            if (g_tracker.keys[k].down && now - g_tracker.keys[k].started >= MEDIA_STALE_MS)
                for (unsigned r = 0; r < 256; r++) g_report_keys[r] &= ~(1u << k);
        media_tick(&g_tracker, now, key_event, NULL);
        if (g_connecting) {
            memcpy(target, g_pending_bda, sizeof(target));
            if (now - g_connect_started > 30000) {
                timeout = true;
                g_connecting = g_connect_issued = false;
                strlcpy(g_error, "Connection timed out. Put the remote in pairing mode and try again.", sizeof(g_error));
                strlcpy(g_status_message, g_error, sizeof(g_status_message));
            } else if (!g_scanning && !g_connect_issued) {
                issue = true; g_connect_issued = true;
            }
        }
        state_unlock();
        if (issue) {
            char mac[18]; addr_to_str(target, mac);
            add_log("Connecting to %s ...", mac);
            esp_err_t err = esp_bt_hid_host_connect(target);
            if (err != ESP_OK) {
                state_lock(); g_connecting = g_connect_issued = false;
                snprintf(g_error, sizeof(g_error), "Connect failed: %s", esp_err_to_name(err));
                state_unlock();
                add_log("Connect call failed: %s", esp_err_to_name(err));
            }
        }
        if (timeout) { esp_bt_hid_host_disconnect(target); add_log("Connection timed out"); }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void wifi_ap_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, WIFI_SSID, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, WIFI_PASSWORD, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(WIFI_SSID);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    add_log("Wi-Fi AP ready: %s / %s | dashboard http://192.168.4.1", WIFI_SSID, WIFI_PASSWORD);
}

static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)dashboard_html, sizeof(dashboard_html) - 1);
}

static esp_err_t json_reply(httpd_req_t *req, const char *status, bool ok, const char *message) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(obj, "ok", ok);
    cJSON_AddStringToObject(obj, "message", message);
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return ESP_ERR_NO_MEM;
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json); return err;
}

static esp_err_t scan_handler(httpd_req_t *req) {
    esp_err_t err = start_scan();
    return json_reply(req, err == ESP_OK ? "200 OK" : "409 Conflict", err == ESP_OK,
                      err == ESP_OK ? "Scan started" : "Disconnect first, or wait for Bluetooth to be ready.");
}

static esp_err_t connect_handler(httpd_req_t *req) {
    char query[128] = {0}, mac[64] = {0};
    esp_bd_addr_t bda;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "mac", mac, sizeof(mac)) != ESP_OK ||
        !media_parse_mac_query(mac, bda))
        return json_reply(req, "400 Bad Request", false, "Invalid Bluetooth address");
    state_lock();
    if (!g_bt_ready || g_connecting || g_connected) {
        state_unlock();
        return json_reply(req, "409 Conflict", false, "Bluetooth is busy. Disconnect or wait before connecting.");
    }
    g_connecting = true; g_connect_issued = false; g_connect_started = now_ms();
    memcpy(g_pending_bda, bda, sizeof(bda));
    g_error[0] = 0;
    strlcpy(g_status_message, "Connecting and pairing. Keep the remote awake and in pairing mode.", sizeof(g_status_message));
    bool scanning = g_scanning;
    state_unlock();
    char canonical[18]; addr_to_str(bda, canonical);
    add_log("Connect requested: %s", canonical);
    if (scanning) {
        esp_err_t err = esp_bt_gap_cancel_discovery();
        if (err != ESP_OK) add_log("Scan cancellation: %s", esp_err_to_name(err));
    }
    return json_reply(req, "202 Accepted", true, "Connection requested");
}

static esp_err_t disconnect_handler(httpd_req_t *req) {
    esp_bd_addr_t bda;
    state_lock();
    bool connected = g_connected, connecting = g_connecting, issued = g_connect_issued;
    memcpy(bda, connected ? g_connected_bda : g_pending_bda, sizeof(bda));
    g_connecting = g_connect_issued = false;
    state_unlock();
    esp_err_t err = (connected || (connecting && issued)) ? esp_bt_hid_host_disconnect(bda) : ESP_OK;
    return json_reply(req, err == ESP_OK ? "200 OK" : "500 Internal Server Error", err == ESP_OK,
                      err == ESP_OK ? "Disconnected or connection cancelled" : esp_err_to_name(err));
}

static esp_err_t favicon_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t state_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    uint32_t now = now_ms();
    state_lock();
    cJSON_AddBoolToObject(root, "connected", g_connected);
    cJSON_AddBoolToObject(root, "connecting", g_connecting);
    cJSON_AddBoolToObject(root, "scanning", g_scanning);
    cJSON_AddBoolToObject(root, "bt_ready", g_bt_ready);
    cJSON_AddStringToObject(root, "peer", g_connected_name);
    cJSON_AddStringToObject(root, "message", g_status_message);
    cJSON_AddStringToObject(root, "error", g_error);
    cJSON_AddStringToObject(root, "last_key", g_last_key);
    cJSON_AddStringToObject(root, "last_raw", g_last_raw);
    cJSON_AddNumberToObject(root, "hold_ms", MEDIA_HOLD_MS);
    cJSON_AddNumberToObject(root, "reports", g_report_count);
    cJSON_AddNumberToObject(root, "decoded_reports", g_decoded_count);
    cJSON_AddNumberToObject(root, "descriptor_bytes", g_descriptor_bytes);
    cJSON_AddNumberToObject(root, "mapped_fields", g_media_map.valid ? g_media_map.count : 0);
    cJSON_AddNumberToObject(root, "uptime_ms", now);
    cJSON *buttons = cJSON_AddArrayToObject(root, "buttons");
    for (unsigned i = 0; i < MEDIA_KEY_COUNT; i++) {
        media_key_t *key = &g_tracker.keys[i];
        cJSON *button = cJSON_CreateObject();
        cJSON_AddItemToArray(buttons, button);
        cJSON_AddStringToObject(button, "id", media_key_ids[i]);
        cJSON_AddStringToObject(button, "name", media_key_names[i]);
        cJSON_AddBoolToObject(button, "down", key->down);
        cJSON_AddBoolToObject(button, "held", key->held);
        cJSON_AddNumberToObject(button, "duration_ms", key->down ? now - key->started : key->duration);
        cJSON_AddNumberToObject(button, "presses", key->presses);
        cJSON_AddNumberToObject(button, "holds", key->holds);
    }
    cJSON *events = cJSON_AddArrayToObject(root, "events");
    for (unsigned i = 0; i < KEY_EVENTS; i++) {
        key_event_t *event = &g_key_events[(g_event_write + i) % KEY_EVENTS];
        if (!event->seq) continue;
        cJSON *item = cJSON_CreateObject();
        cJSON_AddItemToArray(events, item);
        cJSON_AddNumberToObject(item, "seq", event->seq);
        cJSON_AddNumberToObject(item, "time_ms", event->time_ms);
        cJSON_AddStringToObject(item, "key", media_key_ids[event->key]);
        cJSON_AddStringToObject(item, "name", media_key_names[event->key]);
        cJSON_AddStringToObject(item, "action", media_action_names[event->action]);
        cJSON_AddNumberToObject(item, "duration_ms", event->duration);
    }
    cJSON *devices = cJSON_AddArrayToObject(root, "devices");
    for (unsigned i = 0; i < MAX_DEVICES; i++) if (g_devices[i].used) {
        char mac[18]; addr_to_str(g_devices[i].bda, mac);
        cJSON *device = cJSON_CreateObject();
        cJSON_AddItemToArray(devices, device);
        cJSON_AddStringToObject(device, "name", g_devices[i].name);
        cJSON_AddStringToObject(device, "mac", mac);
        cJSON_AddNumberToObject(device, "rssi", g_devices[i].rssi);
    }
    cJSON *logs = cJSON_AddArrayToObject(root, "logs");
    for (unsigned i = 0; i < LOG_LINES; i++) {
        unsigned index = (g_log_write + i) % LOG_LINES;
        if (g_log_seq[index]) cJSON_AddItemToArray(logs, cJSON_CreateString(g_logs[index]));
    }
    state_unlock();
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json); return err;
}

static void web_start(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 8192;
    ESP_ERROR_CHECK(httpd_start(&g_http, &cfg));

    httpd_uri_t u_root = {.uri="/", .method=HTTP_GET, .handler=root_handler};
    httpd_uri_t u_state = {.uri="/api/state", .method=HTTP_GET, .handler=state_handler};
    httpd_uri_t u_scan = {.uri="/api/scan", .method=HTTP_POST, .handler=scan_handler};
    httpd_uri_t u_conn = {.uri="/api/connect", .method=HTTP_POST, .handler=connect_handler};
    httpd_uri_t u_disc = {.uri="/api/disconnect", .method=HTTP_POST, .handler=disconnect_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_state));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_scan));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_conn));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_disc));
    httpd_uri_t u_icon = {.uri="/favicon.ico", .method=HTTP_GET, .handler=favicon_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_icon));
}

static void bluetooth_init(void) {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_bt_hid_host_register_callback(hidh_cb));

    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code = {'0','0','0','0'};
    esp_bt_gap_set_pin(pin_type, 4, pin_code);
    esp_bt_gap_set_device_name("Lumiac ESP32 HID Host");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    ESP_ERROR_CHECK(esp_bt_hid_host_init());
    add_log("Classic Bluetooth started. Put Satechi POP Multimedia in pairing mode.");
}

void app_main(void) {
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) abort();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    wifi_ap_init();
    web_start();
    bluetooth_init();
    if (xTaskCreate(control_task, "media_control", 4096, NULL, 2, NULL) != pdPASS) abort();
}

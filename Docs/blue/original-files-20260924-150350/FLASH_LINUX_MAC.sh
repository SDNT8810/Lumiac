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

static bool parse_mac(const char *s, esp_bd_addr_t out) {
    unsigned int b[6];
    if (!s || sscanf(s, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
    for (int i = 0; i < 6; ++i) {
        if (b[i] > 255) return false;
        out[i] = (uint8_t)b[i];
    }
    return true;
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

static void json_escape(const char *src, char *dst, size_t dst_len) {
    size_t j = 0;
    if (!dst_len) return;
    for (size_t i = 0; src && src[i] && j + 2 < dst_len; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= dst_len) break;
            dst[j++] = '\\'; dst[j++] = (char)c;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            if (j + 2 >= dst_len) break;
            dst[j++] = '\\';
            dst[j++] = (c == '\n') ? 'n' : (c == '\r' ? 'r' : 't');
        } else if (c >= 32) {
            dst[j++] = (char)c;
        }
    }
    dst[j] = 0;
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

static const char *decode_consumer_usage(uint16_t u) {
    switch (u) {
        case 0x00CD: return "Play / Pause";
        case 0x00B5: return "Next Track";
        case 0x00B6: return "Previous Track";
        case 0x00B7: return "Stop";
        case 0x00E2: return "Mute";
        case 0x00E9: return "Volume +";
        case 0x00EA: return "Volume -";
        case 0x006F: return "Brightness +";
        case 0x0070: return "Brightness -";
        case 0x0183: return "Media Select";
        default: return NULL;
    }
}

static void handle_hid_bytes(const uint8_t *data, uint16_t len) {
    if (!data || !len) return;

    char raw[96];
    size_t pos = 0;
    for (uint16_t i = 0; i < len && pos + 4 < sizeof(raw); ++i) {
        pos += snprintf(raw + pos, sizeof(raw) - pos, "%02X%s", data[i], (i + 1 < len) ? " " : "");
    }

    const char *friendly = NULL;
    uint16_t usage = 0;
    /* Consumer-control reports are commonly a 16-bit LE usage, sometimes after a report ID. */
    for (uint16_t i = 0; i + 1 < len; ++i) {
        uint16_t u = (uint16_t)data[i] | ((uint16_t)data[i + 1] << 8);
        const char *d = decode_consumer_usage(u);
        if (d) { friendly = d; usage = u; break; }
    }
    if (!friendly) {
        for (uint16_t i = 0; i < len; ++i) {
            const char *d = decode_consumer_usage(data[i]);
            if (d) { friendly = d; usage = data[i]; break; }
        }
    }

    char key[64];
    if (friendly) snprintf(key, sizeof(key), "%s  (0x%04X)", friendly, usage);
    else snprintf(key, sizeof(key), "HID input (%u byte%s)", len, len == 1 ? "" : "s");

    state_lock();
    strlcpy(g_last_key, key, sizeof(g_last_key));
    strlcpy(g_last_raw, raw, sizeof(g_last_raw));
    state_unlock();
    add_log("KEY: %s | raw: %s", key, raw);
}

static void hidh_cb(esp_hidh_cb_event_t event, esp_hidh_cb_param_t *param) {
    switch (event) {
        case ESP_HIDH_INIT_EVT:
            state_lock(); g_bt_ready = (param->init.status == ESP_HIDH_OK); state_unlock();
            add_log("Bluetooth HID host init: %s", param->init.status == ESP_HIDH_OK ? "OK" : "FAILED");
            break;
        case ESP_HIDH_OPEN_EVT: {
            char mac[18]; addr_to_str(param->open.bd_addr, mac);
            bool ok = (param->open.status == ESP_HIDH_OK && param->open.conn_status == ESP_HIDH_CONN_STATE_CONNECTED);
            state_lock();
            g_connected = ok;
            if (ok) {
                memcpy(g_connected_bda, param->open.bd_addr, ESP_BD_ADDR_LEN);
                int idx = find_device_locked(param->open.bd_addr);
                strlcpy(g_connected_name, idx >= 0 ? g_devices[idx].name : "Bluetooth HID", sizeof(g_connected_name));
            }
            state_unlock();
            add_log("HID connection %s: %s (status=%d)", ok ? "OPEN" : "FAILED", mac, param->open.status);
            break;
        }
        case ESP_HIDH_CLOSE_EVT:
            state_lock();
            g_connected = false;
            g_connected_name[0] = 0;
            memset(g_connected_bda, 0, ESP_BD_ADDR_LEN);
            state_unlock();
            add_log("HID disconnected (status=%d reason=%u)", param->close.status, param->close.reason);
            break;
        case ESP_HIDH_DATA_IND_EVT:
            if (param->data_ind.status == ESP_HIDH_OK) handle_hid_bytes(param->data_ind.data, param->data_ind.len);
            break;
        case ESP_HIDH_GET_DSCP_EVT:
            add_log("HID descriptor: vendor=0x%04X product=0x%04X version=0x%04X len=%u",
                    param->dscp.vendor_id, param->dscp.product_id, param->dscp.version, param->dscp.dl_len);
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
                    strlcpy(tmp_name, (const char *)p->val, sizeof(tmp_name));
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
            add_log("Pairing/auth %s with %s (%s)",
                    param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS ? "SUCCESS" : "FAILED",
                    param->auth_cmpl.device_name[0] ? (char *)param->auth_cmpl.device_name : "device", mac);
            break;
        }
        case ESP_BT_GAP_PIN_REQ_EVT: {
            esp_bt_pin_code_t pin = {'0','0','0','0'};
            if (param->pin_req.min_16_digit) {
                esp_bt_pin_code_t pin16 = {0};
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
    state_lock(); bool scanning = g_scanning; state_unlock();
    if (scanning) return ESP_OK;
    clear_devices();
    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, SCAN_INQ_LEN, 0);
    if (err != ESP_OK) add_log("Could not start scan: %s", esp_err_to_name(err));
    return err;
}

static void auto_scan_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(1800));
    start_scan();
    vTaskDelete(NULL);
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

static const char INDEX_HTML[] =
"<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Lumiac Bluetooth HID</title><style>"
"body{font-family:system-ui,-apple-system,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:18px}"
".wrap{max-width:900px;margin:auto}.card{background:#161b22;border:1px solid #30363d;border-radius:14px;padding:16px;margin:12px 0}"
"h1{font-size:24px;margin:0 0 6px}h2{font-size:17px;margin:0 0 12px}.muted{color:#8b949e}.good{color:#3fb950}.bad{color:#f85149}"
"button{background:#238636;color:white;border:0;border-radius:9px;padding:10px 14px;font-weight:700;margin:4px;cursor:pointer}"
"button.alt{background:#1f6feb}button.danger{background:#da3633}table{width:100%;border-collapse:collapse}td,th{padding:8px;border-bottom:1px solid #30363d;text-align:left;font-size:14px}"
"code,pre{font-family:ui-monospace,monospace}.big{font-size:25px;font-weight:800}.raw{word-break:break-all}"
"#logs{height:270px;overflow:auto;background:#010409;padding:10px;border-radius:9px;font-size:12px;white-space:pre-wrap}"
"</style></head><body><div class='wrap'>"
"<h1>🎛️ Lumiac · Satechi HID Monitor</h1><div class='muted'>ESP32 Wi-Fi: <b>Lumiac</b> · 192.168.4.1</div>"
"<div class='card'><h2>Status</h2><div id='status'>Loading…</div><p>Last key</p><div class='big' id='key'>-</div><p>Raw HID</p><code class='raw' id='raw'>-</code></div>"
"<div class='card'><h2>Bluetooth devices</h2><button onclick='scan()'>Scan</button><button class='danger' onclick='disconnectBT()'>Disconnect</button>"
"<div class='muted'>Put the Satechi button in pairing mode, press Scan, then Connect.</div><div id='devices'></div></div>"
"<div class='card'><h2>Live log</h2><div id='logs'></div></div>"
"</div><script>"
"const esc=s=>String(s??'').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));"
"async function api(u){let r=await fetch(u,{cache:'no-store'});return r.json()}"
"async function scan(){await api('/api/scan');refresh()}"
"async function conn(m){await api('/api/connect?mac='+encodeURIComponent(m));refresh()}"
"async function disconnectBT(){await api('/api/disconnect');refresh()}"
"async function refresh(){try{let s=await api('/api/state');"
"document.getElementById('status').innerHTML='<b class=\"'+(s.connected?'good':'bad')+'\">'+(s.connected?'CONNECTED':'NOT CONNECTED')+'</b> · BT '+(s.bt_ready?'ready':'starting')+' · '+(s.scanning?'scanning…':'idle')+(s.peer?' · '+esc(s.peer):'');"
"document.getElementById('key').textContent=s.last_key;document.getElementById('raw').textContent=s.last_raw;"
"let h='<table><tr><th>Name</th><th>MAC</th><th>RSSI</th><th></th></tr>';for(let d of s.devices){h+='<tr><td>'+esc(d.name)+'</td><td><code>'+esc(d.mac)+'</code></td><td>'+d.rssi+'</td><td><button class=\"alt\" onclick=\"conn(\\\''+d.mac+'\\\')\">Connect</button></td></tr>'}h+='</table>';document.getElementById('devices').innerHTML=h;"
"document.getElementById('logs').textContent=s.logs.join('\\n');let l=document.getElementById('logs');l.scrollTop=l.scrollHeight;"
"}catch(e){document.getElementById('status').textContent='Dashboard connection error: '+e}}"
"setInterval(refresh,700);refresh();</script></body></html>";

static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t json_ok(httpd_req_t *req, const char *msg) {
    char out[160];
    snprintf(out, sizeof(out), "{\"ok\":true,\"message\":\"%s\"}", msg ? msg : "OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, out);
}

static esp_err_t scan_handler(httpd_req_t *req) {
    esp_err_t err = start_scan();
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return json_ok(req, esp_err_to_name(err));
    }
    return json_ok(req, "scan started");
}

static esp_err_t connect_handler(httpd_req_t *req) {
    char query[96] = {0}, mac[32] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "mac", mac, sizeof(mac)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return json_ok(req, "missing mac");
    }
    esp_bd_addr_t bda;
    if (!parse_mac(mac, bda)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return json_ok(req, "bad mac");
    }
    esp_bt_gap_cancel_discovery();
    vTaskDelay(pdMS_TO_TICKS(100));
    add_log("Connecting to %s ...", mac);
    esp_err_t err = esp_bt_hid_host_connect(bda);
    if (err != ESP_OK) add_log("Connect call failed: %s", esp_err_to_name(err));
    return json_ok(req, err == ESP_OK ? "connecting" : esp_err_to_name(err));
}

static esp_err_t disconnect_handler(httpd_req_t *req) {
    esp_bd_addr_t bda;
    state_lock(); bool c = g_connected; memcpy(bda, g_connected_bda, ESP_BD_ADDR_LEN); state_unlock();
    if (c) {
        esp_err_t err = esp_bt_hid_host_disconnect(bda);
        if (err != ESP_OK) add_log("Disconnect call failed: %s", esp_err_to_name(err));
    }
    return json_ok(req, c ? "disconnecting" : "not connected");
}

static esp_err_t state_handler(httpd_req_t *req) {
    char *buf = malloc(12288);
    if (!buf) return ESP_ERR_NO_MEM;
    size_t p = 0;
    state_lock();
    char key[128], raw[192], peer[128];
    json_escape(g_last_key, key, sizeof(key));
    json_escape(g_last_raw, raw, sizeof(raw));
    json_escape(g_connected_name, peer, sizeof(peer));
    p += snprintf(buf + p, 12288 - p,
        "{\"connected\":%s,\"scanning\":%s,\"bt_ready\":%s,\"peer\":\"%s\",\"last_key\":\"%s\",\"last_raw\":\"%s\",\"devices\":[",
        g_connected ? "true" : "false", g_scanning ? "true" : "false", g_bt_ready ? "true" : "false", peer, key, raw);

    bool first = true;
    for (int i = 0; i < MAX_DEVICES && p < 11000; ++i) {
        if (!g_devices[i].used) continue;
        char mac[18], name[140];
        addr_to_str(g_devices[i].bda, mac);
        json_escape(g_devices[i].name, name, sizeof(name));
        p += snprintf(buf + p, 12288 - p, "%s{\"name\":\"%s\",\"mac\":\"%s\",\"rssi\":%d}", first ? "" : ",", name, mac, g_devices[i].rssi);
        first = false;
    }
    p += snprintf(buf + p, 12288 - p, "],\"logs\":[");
    first = true;
    /* Oldest -> newest. Empty slots have sequence 0. */
    uint32_t min_seq = (g_next_log_seq > LOG_LINES) ? g_next_log_seq - LOG_LINES : 1;
    for (uint32_t seq = min_seq; seq < g_next_log_seq && p < 11800; ++seq) {
        for (int i = 0; i < LOG_LINES; ++i) {
            if (g_log_seq[i] == seq) {
                char line[320]; json_escape(g_logs[i], line, sizeof(line));
                p += snprintf(buf + p, 12288 - p, "%s\"%s\"", first ? "" : ",", line);
                first = false;
                break;
            }
        }
    }
    p += snprintf(buf + p, 12288 - p, "]}");
    state_unlock();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, buf, p);
    free(buf);
    return err;
}

static void web_start(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 8192;
    ESP_ERROR_CHECK(httpd_start(&g_http, &cfg));

    httpd_uri_t u_root = {.uri="/", .method=HTTP_GET, .handler=root_handler};
    httpd_uri_t u_state = {.uri="/api/state", .method=HTTP_GET, .handler=state_handler};
    httpd_uri_t u_scan = {.uri="/api/scan", .method=HTTP_GET, .handler=scan_handler};
    httpd_uri_t u_conn = {.uri="/api/connect", .method=HTTP_GET, .handler=connect_handler};
    httpd_uri_t u_disc = {.uri="/api/disconnect", .method=HTTP_GET, .handler=disconnect_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_state));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_scan));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_conn));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http, &u_disc));
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
    xTaskCreate(auto_scan_task, "auto_scan", 3072, NULL, 2, NULL);
}

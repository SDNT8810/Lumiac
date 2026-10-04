#include "bluetooth_remote.h"
#include "remote_feedback.h"
#include "bluetooth_reconnect.h"

#if defined(ESP32)
#include <Preferences.h>
#include <esp32-hal-bt.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_hidh_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "media_keys.h"

namespace bluetooth_remote {
namespace {
struct Device { uint8_t address[6]; char name[64]; int rssi; };
struct Input { uint32_t time; uint8_t down; bool cancel; };
constexpr unsigned kMaxDevices = 16;
SemaphoreHandle_t mutex;
QueueHandle_t inputs;
Preferences preferences;
Device devices[kMaxDevices] = {};
unsigned deviceCount = 0;
bool ready = false, scanning = false, connecting = false, connected = false;
bool connectIssued = false, saved = false, persist = false;
bool inputOverflow = false, acceptInputs = false;
uint32_t connectStarted = 0;
uint8_t target[6] = {}, savedAddress[6] = {}, reportKeys[256] = {};
uint8_t activeHandle = 0xff;
char remoteName[64] = "", savedName[64] = "", status[180] = "Starting Bluetooth...";
media_map_t map = {};
remote_control::Gestures gestures;
remote_control::KeyFeedback feedback;
Reconnect reconnect;
struct Lock {
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
};

String addressText(const uint8_t* address) {
  char text[18];
  snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(text);
}
bool same(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }
int findDevice(const uint8_t* address) {
  for (unsigned i = 0; i < deviceCount; ++i) if (same(devices[i].address, address)) return i;
  return -1;
}
void cancelInputs() {
  memset(reportKeys, 0, sizeof(reportKeys));
  memset(&map, 0, sizeof(map));
  acceptInputs = false;
  Input input{millis(), 0, true};
  if (xQueueSend(inputs, &input, 0) != pdTRUE) inputOverflow = true;
}
void fail(const char* operation, esp_err_t err) {
  Serial.printf("[Bluetooth] %s: %s\n", operation, esp_err_to_name(err));
  snprintf(status, sizeof(status), "%s. Try again.", operation);
}
bool allowed(const uint8_t* address) {
  return ((connecting || connected) && same(address, target)) || (reconnect.enabled() && saved && same(address, savedAddress));
}

void hidCallback(esp_hidh_cb_event_t event, esp_hidh_cb_param_t* p) {
  Lock lock;
  switch (event) {
    case ESP_HIDH_INIT_EVT:
      ready = p->init.status == ESP_HIDH_OK;
      strlcpy(status, ready ? saved ? "Reconnecting..." : "Ready to pair" : "Bluetooth unavailable", sizeof(status));
      break;
    case ESP_HIDH_OPEN_EVT: {
      const bool authorized = allowed(p->open.bd_addr);
      if (p->open.status == ESP_HIDH_OK && p->open.conn_status == ESP_HIDH_CONN_STATE_CONNECTING) {
        if (!authorized) esp_bt_hid_host_disconnect(p->open.bd_addr);
        break;
      }
      if (!authorized) {
        if (p->open.status == ESP_HIDH_OK) esp_bt_hid_host_disconnect(p->open.bd_addr);
        break;
      }
      connected = p->open.status == ESP_HIDH_OK && p->open.conn_status == ESP_HIDH_CONN_STATE_CONNECTED;
      connecting = connectIssued = false;
      reconnect.wait(millis());
      cancelInputs();
      if (connected) {
        activeHandle = p->open.handle;
        memcpy(target, p->open.bd_addr, 6);
        const int i = findDevice(target);
        strlcpy(remoteName, i >= 0 ? devices[i].name : savedName[0] ? savedName : "Bluetooth remote", sizeof(remoteName));
        memcpy(savedAddress, target, 6);
        strlcpy(savedName, remoteName, sizeof(savedName));
        saved = persist = true;
        reconnect.resume();
        strlcpy(status, "Connecting...", sizeof(status));
        esp_bt_hid_host_set_protocol(target, ESP_HIDH_REPORT_MODE);
      } else {
        Serial.printf("[Bluetooth] Connection failed: HID %d\n", p->open.status);
        strlcpy(status, saved && reconnect.enabled() ? "Waiting for remote. Retrying..." :
          "Connection failed. Put remote in pairing mode.", sizeof(status));
      }
      break;
    }
    case ESP_HIDH_CLOSE_EVT:
      if (p->close.conn_status != ESP_HIDH_CONN_STATE_DISCONNECTED) break;
      if (!connected && !connecting) break; // A late close must not postpone the next retry.
      if (connected && p->close.handle != activeHandle) break;
      connected = connecting = connectIssued = false;
      reconnect.wait(millis());
      cancelInputs();
      strlcpy(status, saved && reconnect.enabled() ? "Waiting for remote. Retrying..." : "Disconnected", sizeof(status));
      break;
    case ESP_HIDH_GET_DSCP_EVT:
      if (!connected || p->dscp.handle != activeHandle) break;
      acceptInputs = p->dscp.status == ESP_HIDH_OK && media_parse_descriptor(&map, p->dscp.dsc_list, p->dscp.dl_len) && map.count > 0;
      strlcpy(status, acceptInputs ? "Connected" : "Unsupported remote", sizeof(status));
      break;
    case ESP_HIDH_DATA_IND_EVT: {
      if (!connected || !acceptInputs || p->data_ind.handle != activeHandle || p->data_ind.status != ESP_HIDH_OK) break;
      uint8_t down, covered;
      if (!media_decode(&map, p->data_ind.data, p->data_ind.len, &down, &covered)) break;
      reportKeys[map.report_ids ? p->data_ind.data[0] : 0] = down;
      uint8_t combined = 0;
      for (uint8_t keys : reportKeys) combined |= keys;
      Input input{millis(), combined, false};
      if (xQueueSend(inputs, &input, 0) != pdTRUE) inputOverflow = true;
      break;
    }
    default: break;
  }
}

void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* p) {
  Lock lock;
  switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
      int index = findDevice(p->disc_res.bda);
      if (index < 0) {
        if (deviceCount == kMaxDevices) break;
        index = deviceCount++;
        memcpy(devices[index].address, p->disc_res.bda, 6);
        strlcpy(devices[index].name, "Unknown remote", sizeof(devices[index].name));
        devices[index].rssi = -127;
      }
      Device& device = devices[index];
      for (int i = 0; i < p->disc_res.num_prop; ++i) {
        const auto& prop = p->disc_res.prop[i];
        if (!prop.val || prop.len <= 0) continue;
        if (prop.type == ESP_BT_GAP_DEV_PROP_BDNAME) {
          size_t n = min(static_cast<size_t>(prop.len), sizeof(device.name) - 1);
          memcpy(device.name, prop.val, n); device.name[n] = 0;
        } else if (prop.type == ESP_BT_GAP_DEV_PROP_RSSI) {
          device.rssi = *static_cast<int8_t*>(prop.val);
        } else if (prop.type == ESP_BT_GAP_DEV_PROP_EIR) {
          uint8_t length = 0;
          uint8_t* name = esp_bt_gap_resolve_eir_data(static_cast<uint8_t*>(prop.val), ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &length);
          if (!name) name = esp_bt_gap_resolve_eir_data(static_cast<uint8_t*>(prop.val), ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &length);
          if (name && length) {
            size_t n = min(static_cast<size_t>(length), sizeof(device.name) - 1);
            memcpy(device.name, name, n); device.name[n] = 0;
          }
        }
      }
      break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      scanning = p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED;
      if (!scanning) reconnect.wait(millis(), 10000); // Allow selection before resuming the saved remote.
      if (!connecting) strlcpy(status, scanning ? "Scanning..." : deviceCount ? "Select a remote" : "No remotes found", sizeof(status));
      break;
    case ESP_BT_GAP_CFM_REQ_EVT:
      esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, allowed(p->cfm_req.bda));
      break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
      esp_bt_pin_code_t pin;
      memset(pin, '0', sizeof(pin));
      esp_bt_gap_pin_reply(p->pin_req.bda, allowed(p->pin_req.bda), p->pin_req.min_16_digit ? 16 : 4, pin);
      break;
    }
    case ESP_BT_GAP_AUTH_CMPL_EVT:
      if (allowed(p->auth_cmpl.bda) && p->auth_cmpl.stat != ESP_BT_STATUS_SUCCESS)
        strlcpy(status, "Pairing failed. Forget the remote on its old host and re-enter pairing mode.", sizeof(status));
      break;
    default: break;
  }
}
} // namespace

void begin() {
  if (mutex && inputs) return;
  mutex = xSemaphoreCreateMutex();
  inputs = xQueueCreate(32, sizeof(Input));
  if (!mutex || !inputs) { strlcpy(status, "Not enough memory for Bluetooth.", sizeof(status)); return; }
  if (preferences.begin("lumiac-remote", false)) {
    saved = preferences.getBytesLength("address") == 6;
    if (saved) {
      preferences.getBytes("address", savedAddress, 6);
      preferences.getString("name", savedName, sizeof(savedName));
      reconnect.resume();
    }
  }
  auto initialize = [](const char* stage, esp_err_t err) {
    if (err == ESP_OK) return true;
    Lock lock;
    Serial.printf("[Bluetooth] Initialization failed at %s: %s\n", stage, esp_err_to_name(err));
    strlcpy(status, "Bluetooth unavailable. Restart ESP32.", sizeof(status));
    return false;
  };
  // Calling the Arduino HAL anchors esp32-hal-bt.c in the final ELF. Its strong
  // btInUse() keeps initArduino() from permanently releasing all BT memory
  // before setup(). Direct IDF calls alone leave the weak false stub linked.
  if (!btStarted()) {
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
      esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
      esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
      if (!initialize("controller init", esp_bt_controller_init(&config))) return;
    }
    if (!initialize("controller enable", esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT))) return;
  }
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED &&
      !initialize("host init", esp_bluedroid_init())) return;
  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED &&
      !initialize("host enable", esp_bluedroid_enable())) return;
  if (!initialize("GAP callback", esp_bt_gap_register_callback(gapCallback))) return;
  if (!initialize("HID callback", esp_bt_hid_host_register_callback(hidCallback))) return;
  esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
  if (!initialize("pairing setup", esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability)))) return;
  if (!initialize("device name", esp_bt_dev_set_device_name("Lumiac"))) return;
  if (!initialize("connectable mode", esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE))) return;
  initialize("HID host", esp_bt_hid_host_init());
}

void poll(Handler handler, KeyHandler keys) {
  if (!mutex || !inputs) return;
  bool overflow;
  {
    Lock lock;
    overflow = inputOverflow;
    inputOverflow = false;
    if (overflow) xQueueReset(inputs);
  }
  static bool waitForRelease = false;
  if (overflow) {
    gestures.cancel(); waitForRelease = true;
    feedback.update(0, millis(), keys);
  }
  Input input;
  while (xQueueReceive(inputs, &input, 0) == pdTRUE) {
    if (input.cancel) { gestures.cancel(); waitForRelease = false; }
    else if (waitForRelease) { if (!input.down) waitForRelease = false; }
    else gestures.update(input.down, input.time, handler);
    // Publish every edge, even when a quick tap's down and up were queued together.
    feedback.update(gestures.down(), input.time, keys);
  }
  gestures.tick(millis(), handler);
  feedback.update(gestures.down(), millis(), keys);

  Lock lock;
  const uint32_t now = millis();
  if (persist) {
    preferences.putBytes("address", savedAddress, 6);
    preferences.putString("name", savedName);
    persist = false;
  }
  if (reconnect.due(now, ready, saved, connected, connecting, scanning)) {
    memcpy(target, savedAddress, 6);
    connecting = true; connectIssued = false; connectStarted = now;
    strlcpy(status, "Reconnecting...", sizeof(status));
  }
  if (!connecting) return;
  if (now - connectStarted >= 30000) {
    connecting = connectIssued = false;
    reconnect.wait(now);
    esp_bt_hid_host_disconnect(target);
    strlcpy(status, saved && reconnect.enabled() ? "Waiting for remote. Retrying..." :
      "Timed out. Put remote in pairing mode.", sizeof(status));
  } else if (!scanning && !connectIssued) {
    connectIssued = true;
    esp_err_t err = esp_bt_hid_host_connect(target);
    if (err != ESP_OK) {
      connecting = connectIssued = false;
      reconnect.wait(now);
      fail("Connection failed", err);
      if (saved && reconnect.enabled()) strlcpy(status, "Waiting for remote. Retrying...", sizeof(status));
    }
  }
}

void keyState(JsonObject out) {
  out["down"] = feedback.down();
  out["held"] = feedback.held();
}

void state(JsonObject out) {
  out["supported"] = true;
  if (!mutex || !inputs) { out["message"] = status; return; }
  Lock lock;
  out["ready"] = ready; out["scanning"] = scanning;
  out["connecting"] = connecting; out["connected"] = connected;
  out["controlsReady"] = acceptInputs;
  out["autoReconnect"] = saved && reconnect.enabled();
  out["message"] = status;
  out["name"] = connected ? remoteName : savedName;
  out["savedAddress"] = saved ? addressText(savedAddress) : String();
  JsonArray list = out["devices"].to<JsonArray>();
  for (unsigned i = 0; i < deviceCount; ++i) {
    JsonObject item = list.add<JsonObject>();
    item["address"] = addressText(devices[i].address);
    item["name"] = devices[i].name;
    item["rssi"] = devices[i].rssi;
  }
}

bool command(const String& action, const String& address, String& message) {
  if (!mutex || !inputs) { message = status; return false; }
  Lock lock;
  if (!ready) { message = status; return false; }
  esp_err_t err = ESP_OK;
  if (action == "scan") {
    if (connecting || connected) { message = "Disconnect the remote before scanning."; return false; }
    if (scanning) { message = "Scan already running."; return true; }
    deviceCount = 0; scanning = true;
    err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0);
    if (err != ESP_OK) { scanning = false; reconnect.wait(millis()); }
    else strlcpy(status, "Scanning...", sizeof(status));
  } else if (action == "connect") {
    uint8_t parsed[6];
    if (!media_parse_mac_query(address.c_str(), parsed)) { message = "Invalid Bluetooth address."; return false; }
    if (connected || connecting) { message = "A remote is already connected or connecting."; return false; }
    if (findDevice(parsed) < 0 && !(saved && same(parsed, savedAddress))) { message = "Scan and select a discovered remote first."; return false; }
    memcpy(target, parsed, 6);
    connecting = true; connectIssued = false; connectStarted = millis();
    if (saved && same(parsed, savedAddress)) reconnect.resume();
    else reconnect.suspend(); // Do not switch back to the old remote while pairing its replacement.
    if (scanning) err = esp_bt_gap_cancel_discovery();
    if (err != ESP_OK) { connecting = false; reconnect.wait(millis()); }
    else strlcpy(status, "Pairing...", sizeof(status));
  } else if (action == "disconnect" || action == "forget") {
    reconnect.suspend();
    if (scanning) esp_bt_gap_cancel_discovery();
    if (connected || connecting) err = esp_bt_hid_host_disconnect(target);
    connected = false; // Reject late opens after an explicit disconnect / forget.
    connecting = connectIssued = false;
    cancelInputs();
    if (action == "forget") {
      if (saved) {
        esp_bt_gap_remove_bond_device(savedAddress);
        esp_bt_hid_host_virtual_cable_unplug(savedAddress);
      }
      preferences.clear();
      saved = persist = false; savedName[0] = 0;
    }
    strlcpy(status, action == "forget" ? "Ready to pair" : "Disconnected", sizeof(status));
  } else { message = "Unknown Bluetooth command."; return false; }
  if (err != ESP_OK) { fail("Bluetooth request failed", err); message = status; return false; }
  message = status;
  return true;
}
} // namespace bluetooth_remote

#else
namespace bluetooth_remote {
void begin() {}
void poll(Handler, KeyHandler) {}
void keyState(JsonObject out) { out["down"] = 0; out["held"] = 0; }
void state(JsonObject out) { out["supported"] = false; }
bool command(const String&, const String&, String& message) { message = "Bluetooth requires an original ESP32."; return false; }
}
#endif

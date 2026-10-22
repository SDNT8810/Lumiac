#pragma once
// Event callbacks only publish a reason code. Flash writes and Strings stay in
// the controller loop, including on ESP32 where Wi-Fi has its own event task.
std::atomic<uint32_t> wifiDisconnectReason{0};
#if defined(ESP8266)
WiFiEventHandler wifiDisconnectHandler;
#endif
bool wifiConnectionPending = false;
uint32_t wifiConnectAfter = 0, wifiAttempts = 0;
int previousWifiStatus = -1;
uint32_t recordedWifiReason = 0;

const char* networkSettingsError(const String& ssid, const String& password, bool openNetwork) {
  if (ssid.isEmpty() || ssid.length() > 32) return "Use a Wi-Fi name of 1-32 bytes";
  if (openNetwork) return nullptr;
  // A blank form is not an instruction to erase a saved router password.
  if (password.isEmpty() && ssid == cloudSsid) return nullptr;
  if (password.length() < 8 || password.length() > 63)
    return "Enter an 8-63 character password, or select an open network. Leave blank to keep the password for the same Wi-Fi name";
  return nullptr;
}
const char* networkDisconnectHint(uint32_t reason) {
  switch (reason) {
    case 201: return "Wi-Fi network not found. Check the name, 2.4 GHz availability and signal";
    case 202: return "Wi-Fi authentication failed. Check the password and router security settings";
    case 15: case 204: return "Wi-Fi handshake timed out. Check the password, signal and router security settings";
    case 200: return "Router signal lost. Move the controller closer to the router";
    case 203: return "Router refused the connection. Check router access restrictions and security settings";
    default: return "Router connection unavailable. Check the 2.4 GHz network, password and signal";
  }
}
String networkWifiError() {
  const uint32_t reason = wifiDisconnectReason.load(std::memory_order_relaxed);
  if (reason) return String(networkDisconnectHint(reason)) + " (reason " + String(reason) + ")";
  if (WiFi.status() == WL_NO_SSID_AVAIL) return networkDisconnectHint(201);
  if (WiFi.status() == WL_CONNECT_FAILED) return "Wi-Fi connection failed. Check the password, security settings and signal";
  return "";
}
void configureNetworkEvents() {
#if defined(ESP8266)
  wifiDisconnectHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& event) {
    wifiDisconnectReason.store(event.reason, std::memory_order_relaxed);
  });
#else
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t event) {
    wifiDisconnectReason.store(event.wifi_sta_disconnected.reason, std::memory_order_relaxed);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
#endif
}
void scheduleWifiConnection() {
  // Return the save response before a station scan can move the hotspot channel.
  wifiConnectionPending = true; wifiConnectAfter = millis() + 1500;
  wifiDisconnectReason.store(0, std::memory_order_relaxed);
  recordedWifiReason = 0; cloudState = "Wi-Fi settings saved; connecting to router"; cloudError = "";
}
void tickNetworkConnection() {
  if (cloudSsid.isEmpty()) return;
  const int status = WiFi.status();
  if (status == WL_CONNECTED && !wifiConnectionPending) {
    if (previousWifiStatus != WL_CONNECTED) {
      wifiDisconnectReason.store(0, std::memory_order_relaxed); recordedWifiReason = 0;
      cloudState = "connecting to remote gateway"; cloudError = "";
      lastCloudPoll = 0; cloudPollInterval = 5000;
      persistDiagnostic("Router Wi-Fi connected");
    }
    previousWifiStatus = status; return;
  }
  previousWifiStatus = status;
  // Router scans can interrupt hotspot transfers. The cloud worker also owns
  // its current connection until its completion mailbox has been consumed.
  if (firmwareUpdating || cloudInFlight) return;
  const uint32_t now = millis();
  if (wifiConnectionPending) {
    if (int32_t(now - wifiConnectAfter) < 0) return;
  } else if (now - lastWifiAttempt < 30000) return;
  const uint32_t reason = wifiDisconnectReason.load(std::memory_order_relaxed);
  if (reason && reason != recordedWifiReason) {
    persistDiagnostic("Router Wi-Fi: " + networkWifiError()); recordedWifiReason = reason;
  }
  wifiConnectionPending = false; cloudState = "connecting to Wi-Fi"; cloudError = "";
  lastWifiAttempt = now; ++wifiAttempts;
  WiFi.begin(cloudSsid.c_str(), cloudPassword.c_str());
  previousWifiStatus = -1;
}

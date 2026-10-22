#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
class String : public std::string {
public:
  using std::string::string;
  using std::string::operator=;
  String(const std::string& value) : std::string(value) {}
  String(uint32_t value) : std::string(std::to_string(value)) {}
  bool isEmpty() const { return empty(); }
};
constexpr int WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4;
uint32_t nowMs = 100, lastWifiAttempt = 0, lastCloudPoll = 123, cloudPollInterval = 60000;
uint32_t millis() { return nowMs; }
String cloudSsid = "studio", cloudPassword = "saved-secret", cloudState, cloudError;
bool firmwareUpdating = false, cloudInFlight = false;
std::vector<String> diagnostics;
void persistDiagnostic(const String& message) { diagnostics.push_back(message); }
#if defined(ESP8266)
using WiFiEventHandler = int;
struct WiFiEventStationModeDisconnected { uint8_t reason; };
#else
using arduino_event_id_t = int;
struct arduino_event_info_t { struct { uint8_t reason; } wifi_sta_disconnected; };
constexpr int ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 5;
#endif
struct MockWiFi {
  int currentStatus = WL_IDLE_STATUS;
  unsigned attempts = 0;
  String ssid, password;
  std::function<void(uint8_t)> emitReason;
  int status() const { return currentStatus; }
  void begin(const char* name, const char* secret) {
    ++attempts; ssid = name; password = secret; currentStatus = WL_IDLE_STATUS;
  }
#if defined(ESP8266)
  int onStationModeDisconnected(std::function<void(const WiFiEventStationModeDisconnected&)> callback) {
    emitReason = [callback](uint8_t reason) { callback({reason}); }; return 1;
  }
#else
  void onEvent(std::function<void(arduino_event_id_t, arduino_event_info_t)> callback, int event) {
    assert(event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    emitReason = [callback, event](uint8_t reason) { callback(event, {{reason}}); };
  }
#endif
} WiFi;
#include "../ESP_RF_Octopus/src/network_connection.h"

int main() {
  assert(!networkSettingsError("studio", "", false));
  assert(!networkSettingsError("studio", "new-secret", false));
  assert(networkSettingsError("other-router", "", false));
  assert(networkSettingsError("other-router", "short", false));
  assert(networkSettingsError("other-router", String(64, 'x'), false));
  assert(!networkSettingsError("other-router", "", true));
  assert(networkSettingsError("", "", true));
  assert(networkSettingsError(String(33, 'x'), "", true));
  configureNetworkEvents();
  scheduleWifiConnection();
  assert(wifiConnectionPending && WiFi.attempts == 0);
  nowMs = 1599; tickNetworkConnection(); assert(WiFi.attempts == 0);
  nowMs = 1600; tickNetworkConnection();
  assert(WiFi.attempts == 1 && !wifiConnectionPending && wifiAttempts == 1);
  assert(WiFi.ssid == "studio" && WiFi.password == "saved-secret");
  WiFi.emitReason(202);
  assert(networkWifiError().find("authentication failed") != String::npos);
  nowMs = 31599; tickNetworkConnection(); assert(WiFi.attempts == 1);
  nowMs = 31600; tickNetworkConnection(); assert(WiFi.attempts == 2);
  assert(diagnostics.back().find("reason 202") != String::npos);
  WiFi.currentStatus = WL_CONNECTED; cloudError = "previous failure";
  tickNetworkConnection();
  assert(cloudError.empty() && lastCloudPoll == 0 && cloudPollInterval == 5000);
  assert(wifiDisconnectReason.load() == 0 && diagnostics.back() == "Router Wi-Fi connected");
  const size_t connectedWrites = diagnostics.size();
  tickNetworkConnection(); assert(diagnostics.size() == connectedWrites);
  scheduleWifiConnection(); nowMs += 2000;
  firmwareUpdating = true; tickNetworkConnection(); assert(WiFi.attempts == 2);
  firmwareUpdating = false; cloudInFlight = true;
  tickNetworkConnection(); assert(WiFi.attempts == 2);
  cloudInFlight = false; tickNetworkConnection(); assert(WiFi.attempts == 3);
  WiFi.emitReason(201); assert(networkWifiError().find("not found") != String::npos);
  WiFi.emitReason(204); assert(networkWifiError().find("handshake timed out") != String::npos);
  // A delayed attempt must also work when millis() wraps.
  nowMs = UINT32_MAX - 1000; scheduleWifiConnection();
  nowMs = 498; tickNetworkConnection(); assert(WiFi.attempts == 3);
  nowMs = 499; tickNetworkConnection(); assert(WiFi.attempts == 4);
  cloudSsid = ""; nowMs += 60000;
  tickNetworkConnection(); assert(WiFi.attempts == 4);
  std::cout << "Network setup: saved password, delayed join, event reasons, OTA ownership and rollover passed\n";
}

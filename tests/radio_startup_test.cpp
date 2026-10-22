#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>

enum wifi_ps_type_t { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM };
constexpr int WIFI_AP_STA = 3, WIFI_NONE_SLEEP = 0;
constexpr int kApIp = 1, kApGateway = 1, kApSubnet = 1;
constexpr int kApMaxConnections = 4, kDnsPort = 53;
constexpr char kApSsid[] = "Lumiac", kApPassword[] = "octopus123", kBoardName[] = "test";
bool httpReady = false, webSocketReady = false, bluetoothStarted = false, dnsReady = false;
struct Wifi {
  wifi_ps_type_t sleep = WIFI_PS_MIN_MODEM;
  bool visible = false, apStation = false;
  void persistent(bool) {}
  void setSleepMode(int mode) { assert(mode == WIFI_NONE_SLEEP); sleep = WIFI_PS_NONE; }
  bool setSleep(bool enabled) { sleep = enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE; return true; }
  bool setSleep(wifi_ps_type_t value) { sleep = value; return true; }
  void hostname(const char*) {}
  void setHostname(const char*) {}
  bool mode(int mode) { apStation = mode == WIFI_AP_STA; return apStation; }
  bool softAPdisconnect(bool) { visible = false; return true; }
  bool softAPConfig(int ip, int gateway, int subnet) { assert(apStation && ip == kApIp && gateway == kApGateway && subnet == kApSubnet); return true; }
  bool softAP(const char* ssid, const char* password, int channel, bool hidden, int) {
    assert(apStation && !std::strcmp(ssid, kApSsid) && !std::strcmp(password, kApPassword));
    assert(channel >= 1 && channel <= 11);
    visible = !hidden;
    return true;
  }
  int softAPIP() { return kApIp; }
} WiFi;
struct Dns {
  void start(int port, const char*, int ip) { assert(WiFi.visible && port == kDnsPort && ip == kApIp); dnsReady = true; }
} dnsServer;
void traceBoot(const char*) {}
void delay(unsigned) {}
void setupHttp() { assert(WiFi.visible && dnsReady); httpReady = true; }
void setupWebSocket() { assert(httpReady); webSocketReady = true; }
namespace bluetooth_remote {
void begin() {
  assert(WiFi.visible && httpReady && webSocketReady);
#if defined(ESP32)
  // ESP-IDF 4.4 aborts when Classic BT starts beside Wi-Fi with modem sleep off.
  if (WiFi.sleep == WIFI_PS_NONE)
    throw std::runtime_error("ESP-IDF coexistence abort: enable Wi-Fi modem sleep before Bluetooth");
  bluetoothStarted = true;
#endif
}
}
#include "radio_startup.h"
int main() {
  try {
    startRadios();
    assert(WiFi.visible && httpReady && dnsReady);
#if defined(ESP32)
    assert(bluetoothStarted && WiFi.sleep == WIFI_PS_MIN_MODEM);
#else
    assert(!bluetoothStarted && WiFi.sleep == WIFI_PS_NONE);
#endif
    std::puts("Production startup: visible AP, HTTP readiness and Wi-Fi/Bluetooth coexistence passed");
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 2;
  }
}

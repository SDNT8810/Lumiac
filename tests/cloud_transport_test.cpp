// Compile the actual transport for both ESP families. ESP8266 continuations use
// host fibers; ESP32 mailboxes use threads. The stalled HTTP implementation yields
// just as the SDK does, while the controller continues servicing acknowledgements.
#include <cassert>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <cstring>
#include "host/freertos/queue.h"
class String : public std::string {
public:
  using std::string::string;
  String() = default;
  String(const std::string& s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
  bool isEmpty() const { return empty(); }
};
#define LUMIAC_CLOUD_URL "https://example.test"
#define LUMIAC_CLOUD_DEVICE_ID "lumiac"
#define LUMIAC_CLOUD_DEVICE_KEY "test-key"
const char kCloudRunRootCa[] = "test-public-ca";
std::atomic<uint32_t> clockMs{0};
uint32_t millis() { return clockMs; }
std::atomic<bool> started{false}, released{false};
std::atomic<unsigned> liveTls{0};
unsigned code = 503;
std::string responseBody = "{\"action\":null}";

#if defined(ESP8266)
#ifdef _WIN32
#include <windows.h>
struct cont_t { void* fiber = nullptr; void* caller = nullptr; void (*entry)() = nullptr; bool done = false; };
void CALLBACK runFiber(void* arg) {
  auto* c = static_cast<cont_t*>(arg);
  for (;;) { c->entry(); c->done = true; SwitchToFiber(c->caller); }
}
void cont_init(cont_t* c) { c->fiber = CreateFiber(65536, runFiber, c); assert(c->fiber); }
void cont_run(cont_t* c, void (*entry)()) { c->entry = entry; c->caller = GetCurrentFiber(); SwitchToFiber(c->fiber); }
void cont_suspend(cont_t* c) { SwitchToFiber(c->caller); }
#else
#include <ucontext.h>
struct cont_t { ucontext_t context{}, caller{}; char stack[65536]; void (*entry)() = nullptr; };
cont_t* initializing;
void runFiber() { auto* c = initializing; for (;;) { c->entry(); swapcontext(&c->context, &c->caller); } }
void cont_init(cont_t* c) {
  getcontext(&c->context); c->context.uc_stack.ss_sp = c->stack; c->context.uc_stack.ss_size = sizeof(c->stack);
  makecontext(&c->context, runFiber, 0);
}
void cont_run(cont_t* c, void (*entry)()) { c->entry = entry; initializing = c; swapcontext(&c->caller, &c->context); }
void cont_suspend(cont_t* c) { swapcontext(&c->context, &c->caller); }
#endif
void cont_check(cont_t*) {}
cont_t mainContext, *g_pcont = &mainContext;
void networkYield() { assert(g_pcont != &mainContext); cont_suspend(g_pcont); }
namespace BearSSL {
struct Session {}; struct X509List {};
struct WiFiClientSecure {
  WiFiClientSecure() { ++liveTls; } ~WiFiClientSecure() { --liveTls; }
  void setTrustAnchors(X509List*) {} void setSession(Session*) {}
  void setTimeout(unsigned ms) { assert(ms >= 15000); } void stop() {}
  int getLastSSLError(char* text, size_t size) { std::strncpy(text, "certificate verification failed", size); return -1000; }
};
}
BearSSL::Session cloudTlsSession;
BearSSL::X509List cloudRoots;
using WiFiClientSecure = BearSSL::WiFiClientSecure;
#else
void networkYield() { std::this_thread::yield(); }
struct WiFiClientSecure {
  WiFiClientSecure() { ++liveTls; } ~WiFiClientSecure() { --liveTls; }
  void setCACert(const char*) {} void setHandshakeTimeout(unsigned seconds) { assert(seconds >= 20); }
  int lastError(char* text, size_t size) { std::strncpy(text, "certificate verification failed", size); return -1000; }
  void stop() {}
};
#endif
void delay(unsigned) { networkYield(); }
struct HTTPClient {
  size_t offset = 0;
  void setConnectTimeout(unsigned ms) { assert(ms >= 10000); }
  void setTimeout(unsigned ms) { assert(ms >= 15000); }
  static String errorToString(int) { return "connection failed"; }
  void setReuse(bool reuse) { assert(!reuse); } void useHTTP10(bool use) { assert(use); }
  void setUserAgent(const char* agent) { assert(std::string(agent) == "Lumiac-Controller/1.0"); }
  bool begin(WiFiClientSecure&, const String&) { return true; }
  void addHeader(const char*, const char*) {}
  int POST(uint8_t* payload, size_t size) {
    assert(std::string(reinterpret_cast<char*>(payload), size) == "{\"status\":{}}"); started = true;
    while (!released) networkYield();
    return code;
  }
  int getSize() { return responseBody.size(); }
  HTTPClient* getStreamPtr() { return this; }
  bool connected() { return offset < responseBody.size(); }
  int available() { return responseBody.size() - offset; }
  int read() { return responseBody[offset++]; }
  void end() {}
};
#include "../ESP_RF_Octopus/src/cloud_transport.h"
struct Uart {
  std::string input; size_t offset = 0;
  size_t available() { return input.size() - offset; }
  char read() { return input[offset++]; }
  void feed(const std::string& text) { assert(!available()); input = text; offset = 0; }
} octopusSerial;
String serialLine, lastSerialLine;
unsigned receivedLines = 0;
void handleOctopusLine(const String& line) { ++receivedLines; lastSerialLine = line; }
#include "serial_reader_functions.h"

int main() {
#if defined(ESP8266) && defined(_WIN32)
  assert(ConvertThreadToFiber(nullptr));
#endif
  assert(configureCloudTransport());
  auto* exchange = new CloudExchange; exchange->payload = "{\"status\":{}}";
  assert(submitCloudExchange(exchange));
  // The hardware loop remains free to consume UART and service RF stop while
  // HTTPS is unavailable for 24 simulated hours. Ownership stays in transport.
  for (unsigned tick = 0; tick < 86400; ++tick) {
    clockMs = tick * 1000;
    serviceCloudTransport(); assert(!takeCloudExchange());
    octopusSerial.feed("echo:LUMIAC_ACK_42\r\n"); readOctopusSerial();
    assert(receivedLines == tick + 1 && lastSerialLine == "echo:LUMIAC_ACK_42");
#if defined(ESP8266)
    assert(g_pcont == &mainContext);
#endif
  }
  octopusSerial.feed(std::string(kSerialLineMax + 40, 'x') + "\nnext-complete-line\n");
  readOctopusSerial();
  assert(receivedLines == 86401 && lastSerialLine == "next-complete-line");
  assert(started); released = true;
  CloudExchange* reply = nullptr;
  for (unsigned n = 0; n < 100000 && !reply; ++n) { serviceCloudTransport(); reply = takeCloudExchange(); std::this_thread::yield(); }
  assert(reply == exchange && reply->error == "Gateway HTTP 503" && !liveTls); delete reply;
  // Recover, and then reject a response that would otherwise allocate without limit.
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    code = 200; if (attempt) responseBody.assign(10000, 'x');
    exchange = new CloudExchange; exchange->payload = "{\"status\":{}}";
    assert(submitCloudExchange(exchange)); reply = nullptr;
    for (unsigned n = 0; n < 100000 && !reply; ++n) { serviceCloudTransport(); reply = takeCloudExchange(); std::this_thread::yield(); }
    assert(reply == exchange && !liveTls);
    if (!attempt) assert(reply->error.isEmpty() && reply->response == responseBody);
    else assert(reply->error == "Gateway response is too large");
    delete reply;
  }
  // Negative HTTP-client errors must retain the TLS reason, rather than claiming
  // the gateway sent a negative HTTP status. No certificate bypass is permitted.
  code = unsigned(-1);
  exchange = new CloudExchange; exchange->payload = "{\"status\":{}}";
  assert(submitCloudExchange(exchange)); reply = nullptr;
  for (unsigned n = 0; n < 100000 && !reply; ++n) { serviceCloudTransport(); reply = takeCloudExchange(); std::this_thread::yield(); }
  assert(reply == exchange && !liveTls);
  assert(reply->error.find("Secure connection failed (-1)") != std::string::npos);
  assert(reply->error.find("TLS -1000: certificate verification failed") != std::string::npos);
  delete reply;
#if !defined(ESP8266)
  { std::lock_guard<std::mutex> lock(cloudRequests->mutex); cloudRequests->closed = true; cloudRequests->ready.notify_all(); }
  cloudTask->join(); delete cloudTask; vQueueDelete(cloudRequests); vQueueDelete(cloudResponses);
#elif defined(_WIN32)
  DeleteFiber(cloudContext.fiber);
#endif
  std::cout << "Production cloud transport: stalled HTTPS, retry, bounded response and TLS ownership passed\n";
}

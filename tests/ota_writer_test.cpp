// Exercise the production activation boundary with flash/FS/hash SDK adapters.
// Actual SHA-256/HMAC are supplied by mbedTLS/BearSSL on the compiled devices.
#define LUMIAC_HOST_OTA_TEST
#define LUMIAC_FIRMWARE_ID "LUMIAC1-test"
#include "../shared/firmware_transfer.h"
#include <cassert>
#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>
using String = std::string;
std::map<String, std::vector<uint8_t>> stored;
bool mountFail = false, writeFail = false;
struct File {
  String name;
  explicit operator bool() const { return !name.empty(); }
  void close() { name.clear(); }
  size_t write(const uint8_t* bytes, size_t count) {
    if (writeFail) return 0;
    stored[name].insert(stored[name].end(), bytes, bytes + count); return count;
  }
};
struct MockFS {
  bool mounted = true;
  bool begin() { mounted = !mountFail; return mounted; }
  void end() { mounted = false; }
  File open(const char* name, const char*) { stored[name].clear(); File file; file.name = name; return file; }
} LittleFS;
constexpr unsigned U_FLASH = 0;
struct MockFlash {
  std::vector<uint8_t> bytes;
  size_t size = 0;
  bool activated = false, badMd5 = false;
  bool begin(size_t count, unsigned) { size = count; bytes.clear(); activated = badMd5 = false; return count < 2000000; }
  size_t write(const uint8_t* input, size_t count) { if (writeFail) return 0; bytes.insert(bytes.end(), input, input + count); return count; }
  bool end() { activated = !badMd5 && bytes.size() == size; return activated; }
  void abort() { activated = false; }
  void runAsync(bool) {}
  bool setMD5(const char*) { badMd5 = true; return true; }
} Update;
String mockHash(const uint8_t* bytes, size_t size) {
  const auto crc = lumiac::firmwareCrc(0xFFFFFFFFUL, bytes, size);
  std::ostringstream result; result << std::setfill('0') << std::setw(64) << std::hex << crc; return result.str();
}
struct OtaSha256 {
  std::vector<uint8_t> bytes;
  void begin() { bytes.clear(); }
  void write(const uint8_t* input, size_t count) { bytes.insert(bytes.end(), input, input + count); }
  String finish() { return mockHash(bytes.data(), bytes.size()); }
};
bool otaHashValid(const String& hash) { return hash.size() == 64; }
bool otaEqual(const String& a, const String& b) { return a == b; }
#include "../ESP_RF_Octopus/src/ota_writer.h"
int main() {
  std::vector<uint8_t> app(2048, 17); app[0] = 0xE9;
  const String hash = mockHash(app.data(), app.size());
  OtaWriter writer;
  assert(writer.begin("app", app.size(), hash));
  assert(writer.write(app.data(), 1)); assert(writer.write(app.data() + 1, app.size() - 1));
  assert(!Update.activated && writer.finish() && Update.activated);
  assert(writer.begin("app", app.size(), String(64, 'f')));
  assert(writer.write(app.data(), app.size()));
  assert(!writer.finish() && !Update.activated); // A complete bad hash must never activate.
  assert(writer.begin("app", app.size(), hash)); assert(writer.write(app.data(), 100));
  assert(!writer.finish() && !Update.activated); // Truncated image.
  assert(writer.begin("app", app.size(), hash)); app[0] = 0;
  assert(!writer.write(app.data(), app.size())); writer.abort(); assert(!Update.activated); app[0] = 0xE9;
  assert(writer.begin("app", app.size(), hash)); writeFail = true;
  assert(!writer.write(app.data(), app.size())); writer.abort(); assert(!Update.activated); writeFail = false;
  assert(writer.begin("app", app.size(), hash));
  assert(writer.write(app.data(), app.size()) && writer.finish()); // A failed attempt must not poison the next one.
  std::vector<uint8_t> octopus(2048, 23);
  const uint8_t header[] = {0,0,2,0x20,1,0x81,0,8}; memcpy(octopus.data(), header, 8);
  assert(writer.begin("octopus", octopus.size(), mockHash(octopus.data(), octopus.size())));
  assert(writer.write(octopus.data(), 1)); assert(writer.write(octopus.data() + 1, 3));
  assert(writer.write(octopus.data() + 4, octopus.size() - 4) && writer.finish());
  assert(stored["/octopus.update"] == octopus); // TCP may split the vector-table header anywhere.
  assert(!writer.begin("octopus", lumiac::octopusFirmwareLimit + 1, hash));
#if defined(ARDUINO_ARCH_ESP32)
  assert(writer.begin("filesystem", app.size(), hash) && !LittleFS.mounted);
  assert(writer.write(app.data(), app.size()) && writer.finish() && LittleFS.mounted);
  assert(writer.begin("filesystem", app.size(), hash)); writer.abort(); assert(LittleFS.mounted);
#else
  assert(!writer.begin("filesystem", app.size(), hash));
#endif
  for (unsigned fault = 0; fault < 5; ++fault) {
    std::vector<uint8_t> marked(2048, 17); marked[0] = 0xE9;
    const char* fields[] = {fault == 1 ? "LUMIAC_OTA_ROLE:wrong" : "LUMIAC_OTA_ROLE:esp12",
      fault == 2 ? "LUMIAC1-wrong" : "LUMIAC1-test",
      fault == 3 ? "2026-10-19.10" : "2026-10-19.1",
      fault == 4 ? "removed-recovery" : "LUMIAC_LOCAL_OTA_V1"};
    size_t offset = 64;
    for (const auto* field : fields) { memcpy(marked.data()+offset, field, strlen(field)+1); offset += strlen(field)+1; }
    assert(writer.begin("app", marked.size(), mockHash(marked.data(), marked.size()), "esp12", "2026-10-19.1"));
    for (offset = 0; offset < marked.size(); offset += 17)
      assert(writer.write(marked.data()+offset, std::min(size_t(17), marked.size()-offset)));
    assert(!Update.activated);
    if (fault) assert(!writer.finish() && !Update.activated);
    else { assert(writer.verified() && !Update.activated); assert(writer.finish() && Update.activated); }
  }
  std::cout << "Production image writer: fragmented headers, bad hashes, truncation, failed writes and activation guards passed\n";
}

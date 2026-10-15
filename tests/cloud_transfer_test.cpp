#include "../shared/firmware_transfer.h"
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <iostream>
class String : public std::string {
public:
  using std::string::string;
  using std::string::operator=;
  String(const std::string& value) : std::string(value) {}
  explicit String(uint32_t value) : std::string(std::to_string(value)) {}
  bool startsWith(const char* prefix) const { return find(prefix) == 0; }
  String substring(size_t position) const { return substr(position); }
  void remove(size_t start, size_t count) { erase(start, count); }
  void trim() { while (!empty() && (back() == '\r' || back() == '\n' || back() == ' ')) pop_back(); }
};
using std::min;
#define LUMIAC_FIRMWARE_ID "LUMIAC1-test"
uint32_t nowMs = 0, sdSentAt = 0, sdOffset = 0, sdExpectedOffset = 0, updateSince = 0;
uint32_t millis() { return nowMs; }
unsigned sdPhase = 0, sdRetries = 0, completions = 0;
bool sdWaiting = false, firmwareUpdating = true, updateIdleConfirmed = false;
bool octopusFirmwareReady = true, octopusOnline = true, octopusSdOta = true, octopusHomed = true;
bool octopusRealtime = true, octopusMotionFan = true;
String sdLastCommand, sdExpectedReply, updateIdleMarker, octopusCloudRevision, octopusCloudIdentity, failure;
struct Update { String revision = "2026-10-15.1", phase; uint32_t size = 128, done = 0, crc32 = 123; } remoteUpdate;
struct Serial { std::vector<String> lines; void println(const String& line) { lines.push_back(line); } } octopusSerial;
struct File {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(128, 1); unsigned position = 0;
  bool seek(unsigned offset) { position = offset; return offset <= bytes.size(); }
  int read(uint8_t* output, unsigned count) {
    count = min(count, unsigned(bytes.size() - position)); memcpy(output, bytes.data() + position, count); position += count; return count;
  }
} octopusUpdateFile;
String otaHex(const uint8_t* bytes, unsigned count) {
  String value; const char* digits = "0123456789abcdef";
  while (count--) { value += digits[*bytes >> 4]; value += digits[*bytes++ & 15]; } return value;
}
void failRemoteUpdate(const String& reason) { failure = reason; firmwareUpdating = false; }
void completeRemoteUpdate() { ++completions; firmwareUpdating = false; }
#include "cloud_transfer_functions.h"

unsigned activations() {
  return std::count(octopusSerial.lines.begin(), octopusSerial.lines.end(), "M997 P3");
}
int main() {
  tickOctopusUpdate(); assert(sdPhase == 1 && sdWaiting && octopusSerial.lines.back() == "M997 P4");
  nowMs += 2001; tickOctopusUpdate(); assert(sdRetries == 1 && octopusSerial.lines.back() == "M997 P4");
  handleFirmwareLine("LUMIAC_FW_ABORTED"); tickOctopusUpdate();
  assert(sdPhase == 2 && sdWaiting && octopusSerial.lines.back().startsWith("M997 P0"));
  handleFirmwareLine("LUMIAC_FW_ABORTED"); assert(sdWaiting);
  handleFirmwareLine("LUMIAC_FW_OFFSET 0"); tickOctopusUpdate();
  assert(sdOffset == 64 && sdWaiting && octopusSerial.lines.back().startsWith("M997 P1 S0 "));
  handleFirmwareLine("LUMIAC_FW_OFFSET 0"); assert(sdWaiting); // Late old acknowledgements do not advance.
  nowMs += 2001; tickOctopusUpdate(); assert(sdRetries == 1);
  handleFirmwareLine("echo:LUMIAC_FW_OFFSET 64"); tickOctopusUpdate();
  assert(sdOffset == 128 && sdWaiting && octopusSerial.lines.back().startsWith("M997 P1 S64 "));
  handleFirmwareLine("LUMIAC_FW_OFFSET 128"); tickOctopusUpdate();
  assert(sdPhase == 3 && sdWaiting && octopusSerial.lines.back() == "M997 P2" && !activations());
  handleFirmwareLine("LUMIAC_FW_OFFSET 128"); tickOctopusUpdate(); assert(sdWaiting && !activations());
  handleFirmwareLine("LUMIAC_FW_VERIFIED"); tickOctopusUpdate(); assert(sdPhase == 5 && activations() == 1);
  for (unsigned i = 0; i < 5; ++i) { nowMs += 2501; tickOctopusUpdate(); }
  assert(activations() == 1 && !completions); // Lost reboot ACK never replays activation.
  handleFirmwareLine("LUMIAC_FW_VERSION:2026-10-15.1");
  handleFirmwareLine("LUMIAC_MACHINE:LUMIAC1-test");
  handleFirmwareLine("Cap:LUMIAC_SD_OTA:1"); octopusFirmwareReady = true;
  tickOctopusUpdate(); assert(completions == 1 && !firmwareUpdating && failure.empty());
  firmwareUpdating = true; sdPhase = 2; sdWaiting = false;
  sdCommand("M997 P1 S0 C1 D00", "offset", 1);
  for (unsigned i = 0; i < 5; ++i) { nowMs += 2001; tickOctopusUpdate(); }
  assert(!firmwareUpdating && failure.find("timed out") != std::string::npos && activations() == 1);
  std::cout << "Production SD sender: block retries, stale ACKs, verify-before-activate and no reset replay passed\n";
}

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
  String substring(size_t position, size_t end = std::string::npos) const { return substr(position, end == std::string::npos ? end : end - position); }
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
String sdLastCommand, sdExpectedReply, sdLastReply, updateIdleMarker, octopusCloudRevision, octopusCloudIdentity, failure;
struct Update { String revision = "2026-10-15.1", phase; uint32_t size = 128, done = 0, crc32 = 123; } remoteUpdate;
struct Serial { std::vector<String> lines; void println(const String& line = "") { lines.push_back(line); } } octopusSerial;
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
#define main nativeReceiverMain
#include "firmware_update_test.cpp"
#undef main

unsigned activations() {
  return std::count(octopusSerial.lines.begin(), octopusSerial.lines.end(), "M997 P3");
}
int main() {
  tickOctopusUpdate(); assert(sdPhase == 1 && sdWaiting && octopusSerial.lines.back() == "M997 P4");
  nowMs += 2001; tickOctopusUpdate(); assert(!sdRetries);
  nowMs += 8000; tickOctopusUpdate(); assert(sdRetries == 1 && octopusSerial.lines.back() == "M997 P4");
  handleFirmwareLine("LUMIAC_FW_ERROR invalid old partial command"); assert(failure.empty());
  handleFirmwareLine("LUMIAC_FW_ABORTED"); tickOctopusUpdate();
  assert(sdPhase == 2 && sdWaiting && octopusSerial.lines.back().startsWith("M997 P0"));
  handleFirmwareLine("LUMIAC_FW_ABORTED"); assert(sdWaiting);
  handleFirmwareLine("LUMIAC_FW_OFFSET 0"); tickOctopusUpdate();
  assert(sdOffset == 0 && remoteUpdate.done == 0 && sdWaiting && octopusSerial.lines.back().startsWith("M997 P1 0 "));
  handleFirmwareLine("LUMIAC_FW_OFFSET 0"); assert(sdWaiting); // Late old acknowledgements do not advance.
  nowMs += 10001; tickOctopusUpdate(); assert(sdRetries == 1);
  for (unsigned offset = 16; offset <= 128; offset += 16) {
    handleFirmwareLine("echo:LUMIAC_FW_OFFSET " + String(offset)); tickOctopusUpdate();
    assert(sdOffset == offset && remoteUpdate.done == offset);
  }
  assert(sdPhase == 3 && sdWaiting && octopusSerial.lines.back() == "M997 P2" && !activations());
  handleFirmwareLine("LUMIAC_FW_OFFSET 128"); tickOctopusUpdate(); assert(sdWaiting && !activations());
  nowMs += 10001; tickOctopusUpdate(); assert(sdWaiting && !sdRetries);
  handleFirmwareLine("LUMIAC_FW_VERIFIED"); tickOctopusUpdate(); assert(sdPhase == 5 && activations() == 1);
  for (unsigned i = 0; i < 5; ++i) { nowMs += 2501; tickOctopusUpdate(); }
  assert(activations() == 1 && !completions); // Lost reboot ACK never replays activation.
  handleFirmwareLine("LUMIAC_FW_VERSION:2026-10-15.1");
  handleFirmwareLine("LUMIAC_MACHINE:LUMIAC1-test");
  handleFirmwareLine("Cap:LUMIAC_SD_OTA:1"); octopusFirmwareReady = true;
  tickOctopusUpdate(); assert(completions == 1 && !firmwareUpdating && failure.empty());
  firmwareUpdating = true; sdPhase = 2; sdWaiting = false;
  sdCommand("M997 P1 0 1 00", "offset", 1);
  for (unsigned i = 0; i < 5; ++i) { nowMs += 10001; tickOctopusUpdate(); }
  assert(!firmwareUpdating && failure.find("timed out") != std::string::npos && activations() == 1);
  firmwareUpdating = true; failure = ""; remoteUpdate.done = 512; sdPhase = 2;
  handleFirmwareLine("start");
  assert(!firmwareUpdating && !octopusHomed && !octopusFirmwareReady && failure.find("byte 512") != std::string::npos);

  // Run both production protocol ends through a 63-byte receive queue. This
  // catches positional-vs-labelled parameter mismatches as well as long frames.
  reset(); failure = ""; firmwareUpdating = true; sdPhase = 0; sdWaiting = false;
  sdOffset = sdExpectedOffset = remoteUpdate.done = 0; octopusSerial.lines.clear();
  remoteUpdate.size = 32773; octopusUpdateFile.bytes.resize(remoteUpdate.size);
  for (unsigned i = 0; i < remoteUpdate.size; ++i) octopusUpdateFile.bytes[i] = i * 13;
  const uint8_t header[] = {0,0,2,0x20,1,0x81,0,8}; memcpy(octopusUpdateFile.bytes.data(), header, sizeof(header));
  remoteUpdate.crc32 = crc(octopusUpdateFile.bytes);
  const std::string oldFrame = block(octopusUpdateFile.bytes, 512) + '\n';
  assert(oldFrame.size() > 63 && oldFrame.substr(0,63).find('\n') == std::string::npos);
  size_t consumed = 0; unsigned blocks = 0;
  std::string partial = "M997 P1 512 123 broken";
  for (unsigned turn = 0; sdPhase != 5 && turn < 10000; ++turn) {
    tickOctopusUpdate();
    std::string burst;
    while (consumed < octopusSerial.lines.size()) burst += octopusSerial.lines[consumed++] + '\n';
    if (burst.empty()) continue;
    assert(burst.size() <= 63); // Entire command arrives while the CPU is busy.
    for (char c : burst) {
      if (c != '\n') { partial += c; continue; }
      if (partial.empty()) continue;
      lumiac::FirmwareCommand decoded;
      const bool valid = lumiac::parseFirmwareCommand(partial.c_str(), decoded);
      assert(valid || partial == "M997 P1 512 123 broken");
      if (valid && decoded.type == 1) ++blocks;
      std::istringstream reply(command(partial)); partial.clear(); std::string line;
      while (std::getline(reply,line)) handleFirmwareLine(line);
    }
    nowMs += 20;
  }
  assert(failure.empty() && sdPhase == 5 && rebooted && files["firmware.bin"] == octopusUpdateFile.bytes);
  assert(blocks == (remoteUpdate.size + 15) / 16 && remoteUpdate.done == remoteUpdate.size);
  std::cout << "Production SD sender + receiver: small positional frames, complete image/readback through CPU stalls, partial-line recovery, ACK progress and no reset replay passed\n";
}

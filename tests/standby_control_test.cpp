#include "../ESP_RF_Octopus/src/remote_gestures.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

// Arduino String and hardware boundaries; the handler and lamp functions below
// are extracted from main.cpp, so this exercises the real capability checks.
struct String : std::string {
  using std::string::string;
  String(const std::string& text) : std::string(text) {}
  explicit String(unsigned value) : std::string(std::to_string(value)) {}
};
int constrain(int value, int low, int high) { return std::max(low, std::min(high, value)); }
struct Light { bool on; uint8_t brightness, lastNonZeroBrightness; } lightState;
struct Snapshot { bool standby; Light light; } snapshot;
struct Program { bool loop; } randomProgram{true};
struct Playback { Program* program; bool waitingForHome, waitingForMarker; } embeddedPlayback;
bool standby, octopusRealtime, octopusFirmwareReady, octopusHomed, octopusMotionFan;
bool spiderProgramActive, spiderProgramPaused, startupHomeInProgress;
bool playTapHandled, manualSpiderPauseRequested, startupCanceled, loopCanceled;
unsigned selectedPreset = 0, motionRequests = 0, broadcasts = 0;
uint32_t nowMs = 0, lastOctopusRecoveryMs = 0;
constexpr uint32_t kOctopusRecoveryRetryMs = 3000;
uint32_t millis() { return nowMs; }
int8_t lastFanMode;
const String kBoardName = "ESP32";
std::vector<String> serial;
String lastAction;
void sendToOctopus(const String& command, const String&, bool = true) {
  serial.push_back(command);
  if (command == "M215 P") spiderProgramPaused = true;
  if (command == "M215 R") spiderProgramPaused = false;
}
void logMessage(const String&, const String&) {}
void broadcastStatus(const String&) {}
void broadcastState() { snapshot = {standby, lightState}; ++broadcasts; }
void clearDesiredSpiderLoop() { loopCanceled = true; }
void cancelStartupAutomation(const String&, const char*) { startupCanceled = true; }
void issueImmediateOverride(const String&, const char*) {
  embeddedPlayback.program = nullptr;
  spiderProgramActive = spiderProgramPaused = false;
}
void handleSimpleAction(const String& action, const String&) { ++motionRequests; lastAction = action; }
#include "standby_functions.h"

bool sent(const char* command) { return std::find(serial.begin(), serial.end(), command) != serial.end(); }

int main() {
  using remote_control::Event;
  // No Octopus, old firmware, current firmware, and a lost connection.
  for (unsigned capabilities = 0; capabilities < 4; ++capabilities) {
    octopusRealtime = capabilities & 1;
    octopusFirmwareReady = capabilities & 2;
    octopusMotionFan = octopusRealtime && octopusFirmwareReady;
    lastFanMode = -1;
    standby = startupCanceled = loopCanceled = playTapHandled = false;
    octopusHomed = true;
    spiderProgramActive = true;
    spiderProgramPaused = startupHomeInProgress = false;
    embeddedPlayback.program = &randomProgram;
    lightState = {true, 179, 179};
    serial.clear(); broadcasts = motionRequests = 0;
    remote_control::Gestures keys;
    keys.update(16, 100, handleBluetoothEvent);
    keys.tick(2599, handleBluetoothEvent);
    assert(!standby && lightState.on); // Short pause must preserve lamps.
    keys.tick(2600, handleBluetoothEvent);
    assert(standby && !lightState.on && lightState.brightness == 0);
    assert(broadcasts && snapshot.standby && !snapshot.light.on && snapshot.light.brightness == 0);
    assert(!octopusHomed && startupCanceled && loopCanceled);
    assert(sent(octopusRealtime ? ";L000" : "M355 S0") && sent("M18"));
    if (octopusMotionFan) assert(sent(";F000"));
    keys.update(0, 2700, handleBluetoothEvent);
    assert(standby && !lightState.on && motionRequests == 0);

    // Reconnecting must use the saved off state, not restore the old 70% level.
    octopusRealtime = octopusFirmwareReady = octopusMotionFan = true;
    serial.clear(); lastFanMode = -1;
    syncLampStateToOctopus(kBoardName);
    syncFanModeToOctopus();
    assert(sent(";L000") && sent(";F000"));
    handleBluetoothEvent(Event::Brighter);
    assert(standby && lightState.on && lightState.brightness == 5 && motionRequests == 0);
    assert(snapshot.standby && snapshot.light.brightness == 5);
  }
  // Double click routes to the existing random action in every motion state.
  // No resume of a paused POS or random start is sent between its two clicks.
  for (unsigned mode = 0; mode < 4; ++mode) {
    octopusRealtime = octopusFirmwareReady = true;
    standby = mode == 3;
    spiderProgramActive = mode == 1 || mode == 2;
    spiderProgramPaused = mode == 2;
    startupHomeInProgress = false;
    serial.clear(); motionRequests = 0; lastAction.clear();
    remote_control::Gestures keys;
    keys.update(16, 100, handleBluetoothEvent);
    if (mode == 1) assert(sent("M215 P") && spiderProgramPaused); // Immediate pause.
    keys.update(0, 150, handleBluetoothEvent);
    keys.update(16, 300, handleBluetoothEvent);
    assert(motionRequests == 0 && !sent("M215 R"));
    keys.update(0, 350, handleBluetoothEvent);
    assert(motionRequests == 1 && lastAction == "random_position" && !sent("M215 R"));
    keys.tick(1000, handleBluetoothEvent);
    assert(motionRequests == 1);
  }
  // A single click still resumes the existing paused program after the window.
  standby = false; spiderProgramActive = spiderProgramPaused = true;
  serial.clear(); motionRequests = 0;
  remote_control::Gestures keys;
  keys.update(16, 100, handleBluetoothEvent); keys.update(0, 150, handleBluetoothEvent);
  keys.tick(750, handleBluetoothEvent); assert(!sent("M215 R"));
  keys.tick(751, handleBluetoothEvent);
  assert(sent("M215 R") && !spiderProgramPaused && motionRequests == 0);

  // Headless startup: ordinary serial traffic says "ready", but the first
  // capabilities reply was lost. Retry must not depend on a browser request.
  startupHomeInProgress = spiderProgramActive = spiderProgramPaused = false;
  embeddedPlayback.program = nullptr;
  octopusFirmwareReady = true;
  octopusRealtime = octopusMotionFan = false;
  lastOctopusRecoveryMs = 0; nowMs = 2999;
  serial.clear(); motionRequests = 0;
  handleBluetoothEvent(Event::Next); assert(motionRequests == 0);
  pollOctopusCapabilities(); assert(!sent("M115"));
  nowMs = 3000; pollOctopusCapabilities(); assert(sent("M115"));
  serial.clear(); nowMs = 5999; pollOctopusCapabilities(); assert(serial.empty());
  nowMs = 6000; pollOctopusCapabilities(); assert(sent("M115"));
  // The next reply advertises the capabilities. Motion works without any HTTP,
  // WebSocket or Wi-Fi event, and successful discovery stops the retries.
  octopusRealtime = octopusMotionFan = true;
  serial.clear(); nowMs = 9000; pollOctopusCapabilities(); assert(serial.empty());
  handleBluetoothEvent(Event::Next);
  assert(motionRequests == 1 && lastAction == "pos1");
  handleBluetoothEvent(Event::PlayDoubleTap);
  assert(motionRequests == 2 && lastAction == "random_position");

  // Missing fan capability is recovered too, but queries must not pile up
  // during homing, a paused move, or an unacknowledged program command.
  octopusMotionFan = false;
  embeddedPlayback.program = &randomProgram; spiderProgramActive = true;
  for (bool* busy : {&startupHomeInProgress, &spiderProgramPaused,
                    &embeddedPlayback.waitingForHome, &embeddedPlayback.waitingForMarker}) {
    *busy = true; serial.clear(); nowMs += 3000;
    pollOctopusCapabilities(); assert(serial.empty());
    *busy = false;
  }
  pollOctopusCapabilities(); assert(sent("M115")); // Between random segments.
  serial.clear(); lastOctopusRecoveryMs = UINT32_MAX - 999;
  nowMs = 1999; pollOctopusCapabilities(); assert(serial.empty());
  nowMs = 2000; pollOctopusCapabilities(); assert(sent("M115"));
  puts("PASS standby, lamp state, headless capability recovery, double-click random and single-click resume");
}

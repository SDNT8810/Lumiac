#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

#include "web_page.h"
#include "logo_jpg.h"

namespace {

constexpr char kBoardName[] = "ESP_RF_Octopus";
constexpr char kRemoteBoardName[] = "RF Remote";

constexpr char kApSsid[] = "ESP_RF_Octopus";
constexpr char kApPassword[] = "octopus123";
constexpr byte kDnsPort = 53;
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);
constexpr uint8_t kApMaxConnections = 8;

constexpr int kOctopusTxPin = 17;
constexpr int kOctopusRxPin = 16;
constexpr uint32_t kOctopusBaud = 115200;
constexpr int kOctopusResetPin = 23;
constexpr bool kOctopusResetActiveLow = true;
constexpr uint32_t kOctopusResetPulseMs = 250;
constexpr int kMinFeedRate = 10;
constexpr int kMaxFeedRate = 400;
constexpr int kMaxAxisPosition = 119;

constexpr uint8_t kLightOnPin = 13;
constexpr uint8_t kLightOffPin = 14;
constexpr uint8_t kPos1Pin = 25;
constexpr uint8_t kPos2Pin = 26;
constexpr uint8_t kRandomPin = 27;
constexpr uint8_t kDimmerPin = 32;
constexpr uint8_t kPos3Pin = 33;
constexpr uint8_t kHomePin = 4;

constexpr uint32_t kStatePollMs = 1500;
constexpr uint32_t kRandomCodePollMs = 15000;
constexpr uint32_t kRemoteOfflineMs = 20000;
constexpr uint32_t kOctopusOfflineMs = 5000;
constexpr uint32_t kOctopusRecoveryRetryMs = 3000;
constexpr uint32_t kOctopusSerialReinitMs = 30000;
constexpr uint32_t kOctopusHardwareResetTimeoutMs = 120000;
constexpr uint32_t kOctopusResetCooldownMs = 180000;
constexpr uint32_t kHealthCheckMs = 60000;
constexpr uint32_t kLowHeapThresholdBytes = 30000;
constexpr uint8_t kLowHeapStrikeLimit = 3;
constexpr uint32_t kDebounceMs = 35;
constexpr uint32_t kDimmerStepMs = 180;
constexpr uint8_t kDimmerStep = 16;
constexpr uint32_t kStartupHomeDelayMs = 4000;
constexpr uint32_t kResetHomeDelayMs = 1200;
constexpr uint32_t kStartupAutoplayDelayMs = 500;
constexpr uint8_t kMotionStopRepeatCount = 3;
constexpr uint32_t kMotionStopRepeatDelayMs = 20;
constexpr uint32_t kWebSocketHeartbeatMs = 10000;
constexpr uint32_t kWebSocketPongTimeoutMs = 3000;
constexpr uint8_t kWebSocketDisconnectCount = 2;
constexpr uint32_t kManualResetVerifyTimeoutMs = 15000;
constexpr size_t kSerialLineMax = 256;
constexpr size_t kMaxRandomCodes = 16;
constexpr size_t kLogHistorySize = 120;
constexpr size_t kLogReplayLimit = 30;

constexpr char kAxes[] = { 'X', 'Y', 'Z', 'A', 'B', 'C' };
constexpr size_t kAxisCount = sizeof(kAxes) / sizeof(kAxes[0]);

struct LightState {
  bool on = true;
  uint8_t brightness = 179;
  uint8_t lastNonZeroBrightness = 179;
};

enum ButtonIndex : size_t {
  kButtonLightOn = 0,
  kButtonLightOff,
  kButtonPos1,
  kButtonPos2,
  kButtonRandom,
  kButtonDimmer,
  kButtonPos3,
  kButtonHome,
  kButtonCount
};

struct ButtonState {
  const char* name;
  uint8_t pin;
  bool stableLevel;
  bool lastRead;
  uint32_t lastChangeMs;

  ButtonState(const char* buttonName, const uint8_t buttonPin)
    : name(buttonName), pin(buttonPin), stableLevel(HIGH), lastRead(HIGH), lastChangeMs(0) {}
};

DNSServer dnsServer;
WebServer server(80);
WebSocketsServer webSocket(81);
HardwareSerial octopusSerial(2);

float axisPositions[kAxisCount] = { 0, 0, 0, 0, 0, 0 };
uint16_t randomCodes[kMaxRandomCodes] = {};
size_t randomCodeCount = 0;
bool waitingForRandomCodeList = false;
ButtonState buttons[kButtonCount] = {
  { "LIGHT_ON", kLightOnPin },
  { "LIGHT_OFF", kLightOffPin },
  { "POS1", kPos1Pin },
  { "POS2", kPos2Pin },
  { "RANDOM", kRandomPin },
  { "DIMMER", kDimmerPin },
  { "POS3", kPos3Pin },
  { "HOME", kHomePin },
};

LightState lightState;
bool octopusOnline = false;
bool remoteOnline = false;
bool spiderProgramActive = false;
bool spiderProgramPaused = false;
int currentFeedRate = 200;
bool dimmerPressed = false;
int8_t dimmerDirection = 1;
bool startupHomePending = true;
bool startupHomeInProgress = false;
bool startupAutoRunPending = true;
bool startupAutoRunArmed = false;
bool octopusResetVerifyPending = false;
bool spiderPauseForAdjustment = false;
bool manualSpiderPauseRequested = false;
bool spiderLoopRestartPending = false;

uint32_t lastStatePollMs = 0;
uint32_t lastRandomCodeQueryMs = 0;
uint32_t lastOctopusRxMs = 0;
uint32_t lastOctopusRecoveryMs = 0;
uint32_t lastOctopusSerialReinitMs = 0;
uint32_t lastOctopusHardwareResetMs = 0;
uint32_t lastRemoteActivityMs = 0;
uint32_t lastHealthCheckMs = 0;
uint32_t lastDimmerStepMs = 0;
uint32_t startupHomeReadyMs = 0;
uint32_t startupAutoRunReadyMs = 0;
uint32_t spiderLoopRestartReadyMs = 0;
uint32_t octopusResetVerifyDeadlineMs = 0;
uint8_t lowHeapStrikeCount = 0;
uint8_t connectedStationCount = 0;

String serialLine;
String remoteIpString = "RF 8CH";
String desiredSpiderLoopCommand;
String logHistory[kLogHistorySize];
size_t logHistoryStart = 0;
size_t logHistoryCount = 0;

void sendToOctopus(const String& line, const String& source = kBoardName, const bool logTx = true);
bool pauseSpiderProgramForAdjustment(const String& source, const String& reason);
void resumeSpiderProgramAfterAdjustment(const String& source, const String& reason);
void cancelStartupAutomation(const String& source, const char* reason);
void runStartupLoopIfReady();
void clearDesiredSpiderLoop();
void setDesiredSpiderLoop(const String& gcode);
void scheduleSpiderLoopRestart(const String& source, const String& reason, const uint32_t delayMs = 150);
void runSpiderLoopRestartIfReady();

void initStringStorage() {
  serialLine.reserve(kSerialLineMax);
  remoteIpString.reserve(16);
  desiredSpiderLoopCommand.reserve(16);
  for (size_t i = 0; i < kLogHistorySize; ++i)
    logHistory[i].reserve(128);
}

int axisIndexForLabel(const char axis) {
  for (size_t i = 0; i < kAxisCount; ++i) {
    if (kAxes[i] == axis) return static_cast<int>(i);
  }
  return -1;
}

uint8_t clampBrightness(const int value) {
  return static_cast<uint8_t>(constrain(value, 0, 255));
}

String formatLogLine(const String& source, const String& message) {
  return "[" + source + "] " + message;
}

void appendLogHistory(const String& line) {
  const size_t slot = (logHistoryStart + logHistoryCount) % kLogHistorySize;
  logHistory[slot] = line;

  if (logHistoryCount < kLogHistorySize) {
    ++logHistoryCount;
    return;
  }

  logHistoryStart = (logHistoryStart + 1) % kLogHistorySize;
}

bool isTelemetryOnlyLine(const String& line) {
  return line == "ok" || line.startsWith("X:");
}

bool isBenignSpiderStatusLine(const String& line) {
  return line == "No active spider SD file to abort.";
}

void sendJsonToClient(const uint8_t clientNum, const JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);
  webSocket.sendTXT(clientNum, payload);
}

void broadcastJson(const JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);
  webSocket.broadcastTXT(payload);
}

void broadcastLogPayload(const String& line) {
  JsonDocument doc;
  doc["type"] = "log";
  doc["line"] = line;
  broadcastJson(doc);
}

void logMessage(const String& source, const String& message) {
  const String line = formatLogLine(source, message);
  Serial.println(line);
  appendLogHistory(line);
  broadcastLogPayload(line);
}

void broadcastStatus(const String& message) {
  JsonDocument doc;
  doc["type"] = "status";
  doc["message"] = message;
  broadcastJson(doc);
}

String lampStateCommand(const bool on, const uint8_t brightness) {
  if (!(on && brightness > 0))
    return "M355 S0";

  String gcode = "M355 P";
  gcode += String(brightness);
  gcode += " S1";
  return gcode;
}

void queryLampState(const String& source, const bool logTx = false) {
  sendToOctopus("M355", source, logTx);
}

void fillStateDocument(JsonDocument& doc) {
  doc["type"] = "state";
  doc["board"] = kBoardName;
  doc["octopusOnline"] = octopusOnline;
  doc["remoteOnline"] = remoteOnline;
  doc["feed"] = currentFeedRate;
  doc["remoteIp"] = remoteIpString;

  JsonObject accessPoint = doc["accessPoint"].to<JsonObject>();
  accessPoint["ssid"] = kApSsid;
  accessPoint["ip"] = kApIp.toString();

  JsonObject positions = doc["positions"].to<JsonObject>();
  for (size_t i = 0; i < kAxisCount; ++i)
    positions[String(kAxes[i])] = axisPositions[i];

  JsonObject lights = doc["lights"].to<JsonObject>();
  lights["on"] = lightState.on;
  lights["brightness"] = lightState.brightness;
  lights["lastNonZeroBrightness"] = lightState.lastNonZeroBrightness;
  lights["driver"] = "octopus_m355";
  lights["output"] = "configured_bed_or_heater";

  JsonArray codes = doc["randomCodes"].to<JsonArray>();
  for (size_t i = 0; i < randomCodeCount; ++i)
    codes.add(randomCodes[i]);
}

void broadcastState() {
  JsonDocument doc;
  fillStateDocument(doc);
  broadcastJson(doc);
}

void markRemoteActivity(const char* reason) {
  lastRemoteActivityMs = millis();

  if (remoteOnline) return;

  remoteOnline = true;
  logMessage(kBoardName, String("RF remote active") + (reason ? String(" (") + reason + ")" : ""));
  broadcastState();
}

void sendLogHistoryToClient(const uint8_t clientNum) {
  const size_t replayCount = min(logHistoryCount, kLogReplayLimit);
  const size_t replayStart = (logHistoryStart + logHistoryCount + kLogHistorySize - replayCount) % kLogHistorySize;

  for (size_t i = 0; i < replayCount; ++i) {
    const size_t index = (replayStart + i) % kLogHistorySize;
    JsonDocument doc;
    doc["type"] = "log";
    doc["line"] = logHistory[index];
    sendJsonToClient(clientNum, doc);
  }
}

void sendStateResponse() {
  JsonDocument doc;
  fillStateDocument(doc);

  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}

void sendToOctopus(const String& line, const String& source, const bool logTx) {
  if (line.isEmpty()) return;

  if (logTx)
    logMessage(source, String("TX -> Octopus: ") + line);
  octopusSerial.print(line);
  octopusSerial.print('\n');
}

void setFeedRate(const int feed, const String& source, const String& reason) {
  const int nextFeedRate = constrain(feed, kMinFeedRate, kMaxFeedRate);
  if (nextFeedRate == currentFeedRate) return;

  const bool resumeSpiderProgram = pauseSpiderProgramForAdjustment(source, reason);
  currentFeedRate = nextFeedRate;
  logMessage(source, reason + " -> F" + String(currentFeedRate));
  if (spiderProgramActive)
    sendToOctopus("M220 S" + String(static_cast<int>((currentFeedRate * 100L + (kMaxFeedRate / 2)) / kMaxFeedRate)), source, false);
  else {
    sendToOctopus("M220 S100", source, false);
    sendToOctopus("G1 F" + String(currentFeedRate), source, false);
  }
  if (resumeSpiderProgram)
    resumeSpiderProgramAfterAdjustment(source, reason);
  broadcastState();
}

void setLightState(const bool on, const uint8_t brightness, const String& source, const String& reason, const bool syncToOctopus = true) {
  const bool nextOn = on && brightness > 0;
  const bool changed = nextOn != lightState.on || brightness != lightState.brightness;
  const bool resumeSpiderProgram = syncToOctopus && changed && pauseSpiderProgramForAdjustment(source, reason);

  lightState.on = nextOn;
  lightState.brightness = brightness;
  if (brightness > 0)
    lightState.lastNonZeroBrightness = brightness;

  if (syncToOctopus)
    sendToOctopus(lampStateCommand(nextOn, brightness), source);
  if (resumeSpiderProgram)
    resumeSpiderProgramAfterAdjustment(source, reason);

  if (changed) {
    logMessage(source, reason + " -> lights " + (lightState.on ? String("ON") : String("OFF")) + ", brightness=" + String(lightState.brightness));
    broadcastState();
  }
}

void turnLightsOn(const String& source, const String& reason) {
  const uint8_t brightness = lightState.brightness > 0
    ? lightState.brightness
    : static_cast<uint8_t>(lightState.lastNonZeroBrightness > 0 ? lightState.lastNonZeroBrightness : 1);
  setLightState(true, brightness, source, reason);
}

void turnLightsOff(const String& source, const String& reason) {
  setLightState(false, lightState.brightness, source, reason);
}

void setBrightness(const int brightness, const String& source, const String& reason) {
  const uint8_t nextBrightness = clampBrightness(brightness);
  setLightState(nextBrightness > 0, nextBrightness, source, reason);
}

void parseLampStateLine(const String& line) {
  if (!line.startsWith("Case light:")) return;

  String value = line.substring(11);
  value.trim();

  if (value.equalsIgnoreCase("off")) {
    setLightState(false, lightState.brightness, "Octopus", "Lamp state report", false);
    return;
  }

  if (value.equalsIgnoreCase("on")) {
    const uint8_t restored = lightState.lastNonZeroBrightness > 0 ? lightState.lastNonZeroBrightness : 160;
    setLightState(true, restored, "Octopus", "Lamp state report", false);
    return;
  }

  const int reportedBrightness = value.toInt();
  if (reportedBrightness < 0 || reportedBrightness > 255) return;
  setLightState(true, static_cast<uint8_t>(reportedBrightness), "Octopus", "Lamp state report", false);
}

void resetRandomCodes() {
  randomCodeCount = 0;
  waitingForRandomCodeList = true;
  broadcastState();
}

void addRandomCode(const uint16_t code) {
  for (size_t i = 0; i < randomCodeCount; ++i) {
    if (randomCodes[i] == code) return;
  }

  if (randomCodeCount >= kMaxRandomCodes) {
    logMessage(kBoardName, "Random code cache full. Increase kMaxRandomCodes if more M215 S codes are needed.");
    return;
  }

  randomCodes[randomCodeCount++] = code;
  broadcastState();
}

void queryRandomCodes(const String& source) {
  lastRandomCodeQueryMs = millis();
  resetRandomCodes();
  sendToOctopus("M215", source);
}

String normalizedGcode(const String& rawGcode) {
  String gcode = rawGcode;
  gcode.trim();
  gcode.toUpperCase();
  return gcode;
}

bool gcodeNeedsMotionStop(const String& rawGcode) {
  const String gcode = normalizedGcode(rawGcode);
  if (!gcode.length()) return false;

  if (gcode.startsWith("M410")) return false;
  if (gcode.startsWith("M114")) return false;
  if (gcode.startsWith("M115")) return false;
  if (gcode.startsWith("M355")) return false;

  return gcode.startsWith("G0")
      || gcode.startsWith("G1")
      || gcode.startsWith("G28")
      || gcode.startsWith("M215");
}

bool gcodeIsImmediateStop(const String& rawGcode) {
  const String gcode = normalizedGcode(rawGcode);
  return gcode == "M410";
}

bool gcodeStartsSpiderProgram(const String& rawGcode) {
  const String gcode = normalizedGcode(rawGcode);
  if (gcode.startsWith("M215 S")) return true;
  return gcode == "M215 P1" || gcode == "M215 P2" || gcode == "M215 P3";
}

bool gcodeStopsSpiderProgram(const String& rawGcode) {
  const String gcode = normalizedGcode(rawGcode);
  return gcode == "M410" || gcode == "M215 X" || gcode == "M215 H";
}

void abortActiveSpiderProgram(const String& source, const char* reason = nullptr) {
  if (!spiderProgramActive) return;

  if (reason && *reason)
    logMessage(source, String("Aborting active spider program before ") + reason + ".");

  sendToOctopus("M215 X", source, false);
  spiderProgramActive = false;
  spiderProgramPaused = false;
  delay(kMotionStopRepeatDelayMs);
}

void sendMotionStopBurst(const String& source, const char* reason = nullptr) {
  if (reason && *reason)
    logMessage(source, String("Issuing stop burst before ") + reason + ".");

  for (uint8_t i = 0; i < kMotionStopRepeatCount; ++i) {
    sendToOctopus("M410", source, false);
    delay(kMotionStopRepeatDelayMs);
  }
}

void clearOctopusPausedState(const String& source, const char* reason = nullptr) {
  if (reason && *reason)
    logMessage(source, String("Clearing Octopus pause state before ") + reason + ".");

  sendToOctopus("M108", source, false);
  delay(kMotionStopRepeatDelayMs);
}

void forceAbortSpiderProgram(const String& source, const char* reason = nullptr) {
  if (reason && *reason)
    logMessage(source, String("Forcing spider abort before ") + reason + ".");

  sendToOctopus("M215 X", source, false);
  spiderProgramActive = false;
  spiderProgramPaused = false;
  delay(kMotionStopRepeatDelayMs);
}

void syncMotionSpeedToOctopus(const String& source, const bool spiderJob = false) {
  if (spiderJob || spiderProgramActive) {
    sendToOctopus("G1 F" + String(kMaxFeedRate), source, false);
    sendToOctopus("M220 S" + String(static_cast<int>((currentFeedRate * 100L + (kMaxFeedRate / 2)) / kMaxFeedRate)), source, false);
  }
  else {
    sendToOctopus("M220 S100", source, false);
    sendToOctopus("G1 F" + String(currentFeedRate), source, false);
  }
}

void syncLampStateToOctopus(const String& source) {
  sendToOctopus(lampStateCommand(lightState.on, lightState.brightness), source, false);
}

bool pauseSpiderProgramForAdjustment(const String& source, const String& reason) {
  if (!(spiderProgramActive && !spiderProgramPaused)) return false;

  logMessage(source, reason + " -> pausing active spider program");
  spiderPauseForAdjustment = true;
  sendToOctopus("M215 P", source, false);
  spiderProgramPaused = true;
  delay(kMotionStopRepeatDelayMs);
  return true;
}

void resumeSpiderProgramAfterAdjustment(const String& source, const String& reason) {
  if (!(spiderProgramActive && spiderProgramPaused)) return;

  sendToOctopus("M215 R", source, false);
  spiderProgramPaused = false;
  spiderPauseForAdjustment = false;
  logMessage(source, reason + " -> resuming spider program");
}

void prepareMotionCommand(const String& source, const char* reason, const bool spiderJob = false) {
  clearOctopusPausedState(source, reason);
  forceAbortSpiderProgram(source, reason);
  sendMotionStopBurst(source, reason);
  syncMotionSpeedToOctopus(source, spiderJob);
}

void clearDesiredSpiderLoop() {
  desiredSpiderLoopCommand = "";
  manualSpiderPauseRequested = false;
  spiderPauseForAdjustment = false;
  spiderLoopRestartPending = false;
  spiderLoopRestartReadyMs = 0;
}

void setDesiredSpiderLoop(const String& gcode) {
  desiredSpiderLoopCommand = normalizedGcode(gcode);
  manualSpiderPauseRequested = false;
  spiderLoopRestartPending = false;
  spiderLoopRestartReadyMs = 0;
}

void scheduleSpiderLoopRestart(const String& source, const String& reason, const uint32_t delayMs) {
  if (!desiredSpiderLoopCommand.length()) return;
  if (manualSpiderPauseRequested || spiderPauseForAdjustment) return;
  if (spiderLoopRestartPending) return;

  spiderLoopRestartPending = true;
  spiderLoopRestartReadyMs = millis() + delayMs;
  logMessage(source, String("Scheduling spider loop restart after ") + reason + ".");
}

void runSpiderLoopRestartIfReady() {
  if (!spiderLoopRestartPending) return;
  if (millis() < spiderLoopRestartReadyMs) return;
  if (spiderProgramActive && !spiderProgramPaused) return;

  spiderLoopRestartPending = false;
  prepareMotionCommand(kBoardName, "spider loop restart", true);
  sendToOctopus(desiredSpiderLoopCommand, kBoardName);
  spiderProgramActive = true;
  spiderProgramPaused = false;
  broadcastStatus("Restarting spider loop.");
}

void cancelStartupAutomation(const String& source, const char* reason) {
  if (!(startupHomePending || startupHomeInProgress || startupAutoRunPending || startupAutoRunArmed)) return;

  startupHomePending = false;
  startupHomeInProgress = false;
  startupAutoRunPending = false;
  startupAutoRunArmed = false;
  startupAutoRunReadyMs = 0;
  clearDesiredSpiderLoop();
  logMessage(source, String("Cancelling startup automation before ") + reason + ".");
}

bool handleAction(const String& action, JsonVariantConst payload, const String& source);

bool handleSimpleAction(const String& action, const String& source) {
  JsonDocument doc;
  return handleAction(action, doc.as<JsonVariantConst>(), source);
}

void stepDimmer(const String& source, const String& reason) {
  const int baseBrightness = lightState.on
    ? lightState.brightness
    : max<int>(lightState.lastNonZeroBrightness, 1);
  const int nextBrightness = clampBrightness(baseBrightness + (dimmerDirection * kDimmerStep));
  setBrightness(nextBrightness, source, reason);
}

void handleRfButtonPressed(const ButtonIndex index) {
  markRemoteActivity("button press");

  switch (index) {
    case kButtonLightOn:
      turnLightsOn(kRemoteBoardName, "RF LIGHT_ON");
      return;
    case kButtonLightOff:
      turnLightsOff(kRemoteBoardName, "RF LIGHT_OFF");
      return;
    case kButtonPos1:
      handleSimpleAction("pos1", kRemoteBoardName);
      return;
    case kButtonPos2:
      handleSimpleAction("pos2", kRemoteBoardName);
      return;
    case kButtonRandom:
      handleSimpleAction("random_position", kRemoteBoardName);
      return;
    case kButtonDimmer:
      dimmerPressed = true;
      lastDimmerStepMs = millis();
      stepDimmer(kRemoteBoardName, "RF DIMMER");
      return;
    case kButtonPos3:
      handleSimpleAction("pos3", kRemoteBoardName);
      return;
    case kButtonHome:
      handleSimpleAction("home", kRemoteBoardName);
      return;
    default:
      return;
  }
}

void handleRfButtonReleased(const ButtonIndex index) {
  if (index != kButtonDimmer) return;

  dimmerPressed = false;
  dimmerDirection = -dimmerDirection;
  logMessage(kRemoteBoardName, String("DIMMER released. Next direction=") + (dimmerDirection > 0 ? "up" : "down"));
}

void pollRfButtons() {
  const uint32_t now = millis();

  for (size_t i = 0; i < kButtonCount; ++i) {
    ButtonState& button = buttons[i];
    const bool level = digitalRead(button.pin);

    if (level != button.lastRead) {
      button.lastRead = level;
      button.lastChangeMs = now;
    }

    if (now - button.lastChangeMs < kDebounceMs) continue;
    if (button.stableLevel == button.lastRead) continue;

    button.stableLevel = button.lastRead;
    if (button.stableLevel == LOW) {
      handleRfButtonPressed(static_cast<ButtonIndex>(i));
    } else {
      handleRfButtonReleased(static_cast<ButtonIndex>(i));
    }
  }
}

void updateDimmerHold() {
  if (!dimmerPressed) return;

  const uint32_t now = millis();
  if (now - lastDimmerStepMs < kDimmerStepMs) return;

  lastDimmerStepMs = now;
  markRemoteActivity("dimmer hold");
  stepDimmer(kRemoteBoardName, "RF DIMMER");
}

void runRandomPosition(const String& source) {
  if (!randomCodeCount) {
    logMessage(source, "Random position requested before M215 code list was available. Requesting M215 list.");
    broadcastStatus("Random position unavailable yet. Querying M215 list.");
    queryRandomCodes(source);
    return;
  }

  const uint16_t code = randomCodes[random(static_cast<long>(randomCodeCount))];
  prepareMotionCommand(source, "random position", true);
  setDesiredSpiderLoop("M215 S" + String(code));
  sendToOctopus("M215 S" + String(code), source);
  spiderProgramActive = true;
  spiderProgramPaused = false;
  broadcastStatus("Running random spider position S" + String(code) + ".");
}

void runStartupHomeIfReady() {
  if (!startupHomePending) return;
  if (!octopusOnline) return;

  const uint32_t now = millis();
  if (now < startupHomeReadyMs) return;

  startupHomePending = false;
  startupHomeInProgress = true;
  broadcastStatus("Running startup home.");
  prepareMotionCommand(kBoardName, "startup home");
  sendToOctopus("M215 H", kBoardName);
}

void runStartupLoopIfReady() {
  if (!(startupAutoRunPending && startupAutoRunArmed)) return;
  if (millis() < startupAutoRunReadyMs) return;
  if (spiderProgramActive || spiderProgramPaused) return;

  startupAutoRunPending = false;
  startupAutoRunArmed = false;
  startupAutoRunReadyMs = 0;
  prepareMotionCommand(kBoardName, "startup S1", true);
  setDesiredSpiderLoop("M215 S1");
  sendToOctopus("M215 S1", kBoardName);
  spiderProgramActive = true;
  spiderProgramPaused = false;
  broadcastStatus("Startup home complete. Running S1.");
}

void parsePositionLine(const String& line) {
  bool found = false;

  for (size_t i = 0; i < kAxisCount; ++i) {
    String token;
    token += kAxes[i];
    token += ':';
    const int start = line.indexOf(token);
    if (start < 0) continue;

    int valueStart = start + token.length();
    int valueEnd = valueStart;
    while (valueEnd < line.length()) {
      const char c = line[valueEnd];
      if (!(isDigit(c) || c == '-' || c == '+' || c == '.')) break;
      ++valueEnd;
    }

    axisPositions[i] = line.substring(valueStart, valueEnd).toFloat();
    found = true;
  }

  if (found) {
    octopusOnline = true;
    lastOctopusRxMs = millis();
    broadcastState();
  }
}

void parseRandomCodeLine(const String& line) {
  if (line == "Spider SD file codes:") {
    resetRandomCodes();
    return;
  }

  const int marker = line.indexOf("M215 S");
  if (marker < 0) {
    if (waitingForRandomCodeList && line.startsWith("ok")) {
      waitingForRandomCodeList = false;
      broadcastState();
    }
    return;
  }

  int valueStart = marker + 6;
  int valueEnd = valueStart;
  while (valueEnd < line.length() && isDigit(line[valueEnd]))
    ++valueEnd;

  if (valueEnd <= valueStart) return;

  addRandomCode(static_cast<uint16_t>(line.substring(valueStart, valueEnd).toInt()));
}

void handleOctopusLine(const String& rawLine) {
  String line = rawLine;
  line.trim();
  if (!line.length()) return;

  if (!isTelemetryOnlyLine(line))
    if (!isBenignSpiderStatusLine(line))
      logMessage("Octopus", line);
  lastOctopusRxMs = millis();
  parseLampStateLine(line);
  parsePositionLine(line);
  parseRandomCodeLine(line);

  if (line.startsWith("Running spider ") || line == "Resumed spider SD file." || line == "Paused spider SD file.")
    spiderProgramActive = true;

  if (line.startsWith("Running spider ") || line == "Resumed spider SD file.")
    spiderProgramPaused = false;
  else if (line == "Paused spider SD file.")
    spiderProgramPaused = true;
  else if (line.indexOf("busy: paused for user") >= 0) {
    spiderProgramPaused = true;
    scheduleSpiderLoopRestart("Octopus", "unexpected pause");
  }
  else if (line == "Aborted spider SD file." || line == "No active spider SD file to abort." || line == "Spider SD file finished.") {
    spiderProgramActive = false;
    spiderProgramPaused = false;
    if (line == "Spider SD file finished.")
      scheduleSpiderLoopRestart("Octopus", "completed cycle");
  }

  if (line == "Paused spider SD file." && !spiderPauseForAdjustment && !manualSpiderPauseRequested)
    scheduleSpiderLoopRestart("Octopus", "unsolicited pause");

  if (line.startsWith("Running spider ") || line == "Resumed spider SD file.") {
    spiderLoopRestartPending = false;
    spiderLoopRestartReadyMs = 0;
    manualSpiderPauseRequested = false;
    if (line == "Resumed spider SD file.")
      spiderPauseForAdjustment = false;
  }

  if ((line == "Spider homing complete." || line == "Spider grouped homing complete.") && startupHomeInProgress && startupAutoRunPending) {
    startupHomeInProgress = false;
    startupAutoRunArmed = true;
    startupAutoRunReadyMs = millis() + kStartupAutoplayDelayMs;
    broadcastStatus("Startup home complete. Preparing S1.");
  }

  if (line.startsWith("FIRMWARE_NAME:")) {
    octopusOnline = true;
    spiderProgramActive = false;
    spiderProgramPaused = false;
    startupHomeInProgress = false;
    startupAutoRunArmed = false;
    startupAutoRunReadyMs = 0;
    spiderPauseForAdjustment = false;
    if (octopusResetVerifyPending) {
      octopusResetVerifyPending = false;
      logMessage(kBoardName, "Octopus reset confirmed by firmware restart banner.");
      syncMotionSpeedToOctopus(kBoardName);
      startupHomePending = true;
      startupAutoRunPending = true;
      startupHomeReadyMs = millis() + kResetHomeDelayMs;
      broadcastStatus("Octopus reset complete. Restoring speed and home.");
    }
    syncLampStateToOctopus(kBoardName);
  }
}

void pollOctopusState() {
  const uint32_t now = millis();
  if (now - lastStatePollMs < kStatePollMs) return;
  lastStatePollMs = now;
  sendToOctopus("M114", kBoardName, false);
}

void pollRandomCodes() {
  const uint32_t now = millis();
  if (randomCodeCount || now - lastRandomCodeQueryMs < kRandomCodePollMs) return;
  queryRandomCodes(kBoardName);
}

void pulseOctopusResetLine(const String& reason) {
  logMessage(kBoardName, "Pulsing Octopus reset line: " + reason);

  octopusOnline = false;
  spiderProgramActive = false;
  spiderProgramPaused = false;
  waitingForRandomCodeList = false;
  serialLine = "";
  broadcastState();

  digitalWrite(kOctopusResetPin, kOctopusResetActiveLow ? LOW : HIGH);
  delay(kOctopusResetPulseMs);
  digitalWrite(kOctopusResetPin, kOctopusResetActiveLow ? HIGH : LOW);

  lastOctopusHardwareResetMs = millis();
  lastOctopusRecoveryMs = lastOctopusHardwareResetMs;
  lastOctopusSerialReinitMs = lastOctopusHardwareResetMs;

  octopusSerial.end();
  delay(50);
  octopusSerial.begin(kOctopusBaud, SERIAL_8N1, kOctopusRxPin, kOctopusTxPin);
}

bool requestManualOctopusReset(const String& source) {
  const uint32_t now = millis();

  if (lastOctopusHardwareResetMs && now - lastOctopusHardwareResetMs < kOctopusResetCooldownMs) {
    const uint32_t secondsRemaining = (kOctopusResetCooldownMs - (now - lastOctopusHardwareResetMs) + 999) / 1000;
    const String message = "Octopus reset blocked by cooldown. Wait " + String(secondsRemaining) + "s.";
    logMessage(source, message);
    broadcastStatus(message);
    return false;
  }

  logMessage(source, "Manual Octopus reset requested.");
  broadcastStatus("Resetting Octopus board...");
  octopusResetVerifyPending = true;
  octopusResetVerifyDeadlineMs = now + kManualResetVerifyTimeoutMs;
  pulseOctopusResetLine("Manual reset requested from UI");
  return true;
}

void recoverOctopusLink() {
  const uint32_t now = millis();

  if (!octopusOnline && now - lastOctopusRecoveryMs >= kOctopusRecoveryRetryMs) {
    lastOctopusRecoveryMs = now;
    sendToOctopus("M115", kBoardName, false);
  }

  if (now - lastOctopusRxMs < kOctopusSerialReinitMs) return;
  if (now - lastOctopusSerialReinitMs < kOctopusSerialReinitMs) return;

  lastOctopusSerialReinitMs = now;
  logMessage(kBoardName, "Octopus serial RX timeout persisted. Reinitializing UART2.");
  octopusSerial.end();
  delay(20);
  octopusSerial.begin(kOctopusBaud, SERIAL_8N1, kOctopusRxPin, kOctopusTxPin);
  sendToOctopus("M115", kBoardName, false);

  if (now - lastOctopusRxMs < kOctopusHardwareResetTimeoutMs) return;
  if (now - lastOctopusHardwareResetMs < kOctopusResetCooldownMs) return;

  pulseOctopusResetLine("No serial RX after staged retry and UART reinit");
}

void healthCheck() {
  const uint32_t now = millis();
  if (now - lastHealthCheckMs < kHealthCheckMs) return;
  lastHealthCheckMs = now;

  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap >= kLowHeapThresholdBytes) {
    lowHeapStrikeCount = 0;
    return;
  }

  ++lowHeapStrikeCount;
  logMessage(kBoardName, "Low heap detected: free=" + String(freeHeap) + " bytes.");
  if (lowHeapStrikeCount < kLowHeapStrikeLimit) return;

  logMessage(kBoardName, "Heap remained critically low. Restarting ESP_RF_Octopus for self-recovery.");
  delay(100);
  ESP.restart();
}

void readOctopusSerial() {
  while (octopusSerial.available()) {
    const char c = static_cast<char>(octopusSerial.read());
    if (c == '\r') continue;

    if (c == '\n') {
      handleOctopusLine(serialLine);
      serialLine = "";
      continue;
    }

    if (serialLine.length() < kSerialLineMax)
      serialLine += c;
  }
}

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", OCTOPUS_WEB_PAGE);
}

void handleLogo() {
  server.send_P(200, "image/jpeg", reinterpret_cast<PGM_P>(OCTOPUS_LOGO_JPG), OCTOPUS_LOGO_JPG_LEN);
}

void handleManifest() {
  static const char manifest[] PROGMEM =
    "{"
      "\"name\":\"WebApp\","
      "\"short_name\":\"WebApp\","
      "\"start_url\":\"/\","
      "\"scope\":\"/\","
      "\"display\":\"standalone\","
      "\"orientation\":\"portrait\","
      "\"background_color\":\"#e9e4da\","
      "\"theme_color\":\"#bfb7aa\","
      "\"icons\":["
        "{"
          "\"src\":\"/logo.jpg\","
          "\"type\":\"image/jpeg\","
          "\"sizes\":\"320x211\","
          "\"purpose\":\"any\""
        "}"
      "]"
    "}";
  server.send_P(200, "application/manifest+json; charset=utf-8", manifest);
}

void handleNotFound() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

void handleMoveCommand(JsonObject axes, const int feed, const String& source) {
  String gcode = "G1";
  bool hasAxis = false;

  currentFeedRate = constrain(feed, kMinFeedRate, kMaxFeedRate);

  for (JsonPair kv : axes) {
    const String axisName = kv.key().c_str();
    if (axisName.length() != 1) continue;

    const int axisIndex = axisIndexForLabel(axisName[0]);
    if (axisIndex < 0) continue;

    const float value = constrain(kv.value().as<float>(), 0.0f, static_cast<float>(kMaxAxisPosition));
    axisPositions[axisIndex] = value;
    gcode += ' ';
    gcode += axisName;
    gcode += String(value, 0);
    hasAxis = true;
  }

  if (!hasAxis) return;

  gcode += " F";
  gcode += String(currentFeedRate);

  prepareMotionCommand(source, "manual move");
  sendToOctopus("G90", source);
  sendToOctopus(gcode, source);
  broadcastState();
}

void handleGcodeCommand(const String& gcode, const String& source) {
  if (!gcode.length()) return;
  const String normalized = normalizedGcode(gcode);
  const bool spiderStart = gcodeStartsSpiderProgram(gcode);
  const bool motionCommand = gcodeNeedsMotionStop(gcode) || gcodeIsImmediateStop(gcode) || normalized == "M215 H";

  if (source != kBoardName && motionCommand)
    cancelStartupAutomation(source, "manual command");

  if (normalized == "M215 P")
    manualSpiderPauseRequested = true;
  else if (normalized == "M215 R")
    manualSpiderPauseRequested = false;
  else if (normalized.startsWith("M215 S"))
    setDesiredSpiderLoop(normalized);
  else if (motionCommand || normalized == "M215 P1" || normalized == "M215 P2" || normalized == "M215 P3")
    clearDesiredSpiderLoop();

  if (gcodeIsImmediateStop(gcode)) {
    clearOctopusPausedState(source, "stop command");
    forceAbortSpiderProgram(source, "stop command");
    sendMotionStopBurst(source, "stop command");
  }
  else if (gcodeNeedsMotionStop(gcode)) {
    prepareMotionCommand(source, "terminal command", spiderStart);
  }

  sendToOctopus(gcode, source);

  if (spiderStart) {
    spiderProgramActive = true;
    spiderProgramPaused = false;
  }
  else if (gcodeStopsSpiderProgram(gcode)) {
    spiderProgramActive = false;
    spiderProgramPaused = false;
  }
}

bool handleAction(const String& action, JsonVariantConst payload, const String& source) {
  if (action == "light_on") {
    turnLightsOn(source, "Light ON request");
    return true;
  }

  if (action == "light_off") {
    turnLightsOff(source, "Light OFF request");
    return true;
  }

  if (action == "set_brightness") {
    setBrightness(payload["brightness"] | lightState.brightness, source, "Brightness update");
    return true;
  }

  if (action == "set_feed") {
    setFeedRate(payload["feed"] | currentFeedRate, source, "Speed update");
    return true;
  }

  if (action == "home") {
    clearDesiredSpiderLoop();
    cancelStartupAutomation(source, "home");
    prepareMotionCommand(source, "home");
    sendToOctopus("M215 H", source);
    spiderProgramActive = false;
    spiderProgramPaused = false;
    return true;
  }

  if (action == "pos1") {
    clearDesiredSpiderLoop();
    cancelStartupAutomation(source, "POS1");
    prepareMotionCommand(source, "POS1", true);
    sendToOctopus("M215 P1", source);
    spiderProgramActive = true;
    spiderProgramPaused = false;
    return true;
  }

  if (action == "pos2") {
    clearDesiredSpiderLoop();
    cancelStartupAutomation(source, "POS2");
    prepareMotionCommand(source, "POS2", true);
    sendToOctopus("M215 P2", source);
    spiderProgramActive = true;
    spiderProgramPaused = false;
    return true;
  }

  if (action == "pos3") {
    clearDesiredSpiderLoop();
    cancelStartupAutomation(source, "POS3");
    prepareMotionCommand(source, "POS3", true);
    sendToOctopus("M215 P3", source);
    spiderProgramActive = true;
    spiderProgramPaused = false;
    return true;
  }

  if (action == "random_position") {
    cancelStartupAutomation(source, "random position");
    runRandomPosition(source);
    return true;
  }

  if (action == "stop_motion") {
    clearDesiredSpiderLoop();
    cancelStartupAutomation(source, "stop");
    clearOctopusPausedState(source, "stop request");
    forceAbortSpiderProgram(source, "stop request");
    sendMotionStopBurst(source, "stop request");
    spiderProgramActive = false;
    spiderProgramPaused = false;
    broadcastStatus("Motion stopped.");
    broadcastState();
    return true;
  }

  if (action == "refresh_positions") {
    sendToOctopus("M114", source);
    return true;
  }

  if (action == "refresh_random_codes") {
    queryRandomCodes(source);
    return true;
  }

  if (action == "send_gcode") {
    const String gcode = payload["gcode"] | "";
    handleGcodeCommand(gcode, source);
    return true;
  }

  if (action == "reset_octopus")
    return requestManualOctopusReset(source);

  return false;
}

void handleApiState() {
  sendStateResponse();
}

void handleApiCommand() {
  const String body = server.arg("plain");
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    server.send(400, "application/json", "{\"error\":\"invalid_json\"}");
    logMessage(kBoardName, "Rejected invalid /api/command payload.");
    return;
  }

  String source = doc["source"] | "HTTP";
  const String typeName = doc["type"] | "";

  if (typeName == "move") {
    handleMoveCommand(doc["axes"].as<JsonObject>(), doc["feed"] | currentFeedRate, source);
    sendStateResponse();
    return;
  }

  if (typeName == "cmd") {
    handleGcodeCommand(doc["gcode"] | "", source);
    sendStateResponse();
    return;
  }

  const String action = doc["action"] | "";
  if (!handleAction(action, doc.as<JsonVariantConst>(), source)) {
    server.send(400, "application/json", "{\"error\":\"unknown_action\"}");
    logMessage(kBoardName, String("Unknown API action: ") + action);
    return;
  }

  sendStateResponse();
}

void handleApiLog() {
  const String body = server.arg("plain");
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    server.send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }

  String source = doc["source"] | "Remote";

  const String message = doc["message"] | "";
  if (message.length())
    logMessage(source, message);

  server.send(204, "text/plain", "");
}

void handleWsEvent(const uint8_t clientNum, const WStype_t type, uint8_t* payload, const size_t length) {
  if (type == WStype_CONNECTED) {
    broadcastStatus("Browser connected.");
    JsonDocument doc;
    fillStateDocument(doc);
    sendJsonToClient(clientNum, doc);
    sendLogHistoryToClient(clientNum);
    return;
  }

  if (type == WStype_DISCONNECTED) {
    logMessage(kBoardName, String("Browser disconnected from WebSocket client ") + clientNum + ".");
    return;
  }

  if (type != WStype_TEXT) return;

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    logMessage(kBoardName, "Invalid WebSocket JSON payload.");
    return;
  }

  const String typeName = doc["type"] | "";
  if (typeName == "cmd") {
    handleGcodeCommand(doc["gcode"] | "", "Web UI");
    return;
  }

  if (typeName == "move") {
    handleMoveCommand(doc["axes"].as<JsonObject>(), doc["feed"] | currentFeedRate, "Web UI");
    return;
  }

  if (typeName == "action") {
    const String action = doc["action"] | "";
    if (!handleAction(action, doc.as<JsonVariantConst>(), "Web UI"))
      logMessage(kBoardName, String("Unknown WebSocket action: ") + action);
    return;
  }
}

void setupHttp() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/logo.jpg", HTTP_GET, handleLogo);
  server.on("/manifest.webmanifest", HTTP_GET, handleManifest);
  server.on("/api/state", HTTP_GET, handleApiState);
  server.on("/api/command", HTTP_POST, handleApiCommand);
  server.on("/api/log", HTTP_POST, handleApiLog);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setupWebSocket() {
  webSocket.begin();
  webSocket.enableHeartbeat(kWebSocketHeartbeatMs, kWebSocketPongTimeoutMs, kWebSocketDisconnectCount);
  webSocket.onEvent(handleWsEvent);
}

void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  (void)info;

#if defined(ARDUINO_EVENT_WIFI_AP_STACONNECTED)
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
    ++connectedStationCount;
    logMessage(kBoardName, String("Wi-Fi client connected. Total stations=") + connectedStationCount);
    return;
  }
#endif

#if defined(ARDUINO_EVENT_WIFI_AP_STADISCONNECTED)
  if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
    if (connectedStationCount > 0) --connectedStationCount;
    logMessage(kBoardName, String("Wi-Fi client disconnected. Total stations=") + connectedStationCount);
    return;
  }
#endif

#if defined(SYSTEM_EVENT_AP_STACONNECTED)
  if (event == SYSTEM_EVENT_AP_STACONNECTED) {
    ++connectedStationCount;
    logMessage(kBoardName, String("Wi-Fi client connected. Total stations=") + connectedStationCount);
    return;
  }
#endif

#if defined(SYSTEM_EVENT_AP_STADISCONNECTED)
  if (event == SYSTEM_EVENT_AP_STADISCONNECTED) {
    if (connectedStationCount > 0) --connectedStationCount;
    logMessage(kBoardName, String("Wi-Fi client disconnected. Total stations=") + connectedStationCount);
    return;
  }
#endif
}

void setupRfInputs() {
  for (size_t i = 0; i < kButtonCount; ++i) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    const bool level = digitalRead(buttons[i].pin);
    buttons[i].stableLevel = level;
    buttons[i].lastRead = level;
    buttons[i].lastChangeMs = millis();
  }

  logMessage(kBoardName, "RF 8-channel input map:");
  for (size_t i = 0; i < kButtonCount; ++i)
    logMessage(kBoardName, String("  ") + buttons[i].name + " -> GPIO " + buttons[i].pin);
}

} // namespace

void setup() {
  Serial.begin(115200);
  initStringStorage();
  randomSeed(micros());

  pinMode(kOctopusResetPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(kOctopusResetPin, kOctopusResetActiveLow ? HIGH : LOW);
  setupRfInputs();

  octopusSerial.begin(kOctopusBaud, SERIAL_8N1, kOctopusRxPin, kOctopusTxPin);

  WiFi.onEvent(handleWiFiEvent);
  WiFi.setSleep(false);
  WiFi.setHostname(kBoardName);
  WiFi.mode(WIFI_AP);
  WiFi.softAPdisconnect(true);
  delay(50);
  WiFi.softAPConfig(kApIp, kApGateway, kApSubnet);
  WiFi.softAP(kApSsid, kApPassword, 1, false, kApMaxConnections);
  dnsServer.start(kDnsPort, "*", WiFi.softAPIP());

  setupHttp();
  setupWebSocket();

  logMessage(kBoardName, String("Access point ready at http://") + WiFi.softAPIP().toString());
  logMessage(kBoardName, "Lamp output delegated to Octopus M355 on the configured bed/heater output.");
  logMessage(kBoardName, String("Octopus reset line ready on GPIO ") + kOctopusResetPin);
  logMessage(kBoardName, String("SoftAP allows up to ") + kApMaxConnections + " simultaneous clients.");

  delay(300);
  sendToOctopus("M115");
  sendToOctopus("M114");
  queryRandomCodes(kBoardName);
  startupHomeReadyMs = millis() + kStartupHomeDelayMs;
  broadcastState();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  webSocket.loop();

  readOctopusSerial();
  pollRfButtons();
  updateDimmerHold();
  pollOctopusState();
  pollRandomCodes();
  runStartupHomeIfReady();
  runStartupLoopIfReady();
  runSpiderLoopRestartIfReady();
  recoverOctopusLink();
  healthCheck();

  const uint32_t now = millis();
  if (octopusResetVerifyPending && now >= octopusResetVerifyDeadlineMs) {
    octopusResetVerifyPending = false;
    logMessage(kBoardName, "Octopus reset was not confirmed within the timeout window.");
    broadcastStatus("Octopus reset not confirmed.");
  }

  if (octopusOnline && now - lastOctopusRxMs > kOctopusOfflineMs) {
    octopusOnline = false;
    logMessage(kBoardName, "Octopus serial RX heartbeat timed out.");
    broadcastState();
  }

  if (remoteOnline && now - lastRemoteActivityMs > kRemoteOfflineMs) {
    remoteOnline = false;
    logMessage(kBoardName, "Remote heartbeat timed out.");
    broadcastState();
  }
}

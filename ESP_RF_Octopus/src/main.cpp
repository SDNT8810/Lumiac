#include <Arduino.h>
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
#else
  #include <WiFi.h>
  #include <WebServer.h>
#endif
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

#include "web_page.h"
#include "logo_jpg.h"
#include "board_config.h"
#include "embedded_gcodes.h"

namespace {

using namespace board;
constexpr const char* kBoardName = board::kName;
constexpr char kRemoteBoardName[] = "RF Remote";

constexpr char kApSsid[] = "ESP_RF_Octopus";
constexpr char kApPassword[] = "octopus123";
constexpr byte kDnsPort = 53;
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);
constexpr uint32_t kOctopusBaud = 115200;
constexpr bool kOctopusResetActiveLow = true;
constexpr uint32_t kOctopusResetPulseMs = 250;
constexpr int kMinFeedRate = 10;
constexpr int kMaxFeedRate = 600;
constexpr int kMaxAxisPosition = 120;

constexpr bool kRfButtonsActiveLow = false;

constexpr uint32_t kStatePollMs = 1500;
constexpr uint32_t kRandomCodePollMs = 15000;
constexpr uint32_t kRemoteOfflineMs = 20000;
constexpr uint32_t kOctopusOfflineMs = 5000;
constexpr uint32_t kOctopusRecoveryRetryMs = 3000;
constexpr uint32_t kOctopusSerialReinitMs = 30000;
constexpr uint32_t kOctopusHardwareResetTimeoutMs = 120000;
constexpr uint32_t kOctopusResetCooldownMs = 180000;
constexpr uint32_t kHealthCheckMs = 60000;
constexpr uint8_t kLowHeapStrikeLimit = 3;
constexpr uint32_t kDebounceMs = 8;
constexpr uint8_t kDimmerStep = 16;
constexpr uint32_t kDimmerHoldStartMs = 180;
constexpr uint32_t kDimmerHoldRepeatMs = 120;
constexpr uint32_t kDimmerHoldSafetyMs = 1500;
constexpr uint32_t kStartupHomeDelayMs = 8000;
constexpr uint32_t kResetHomeDelayMs = 1200;
constexpr uint32_t kStartupAutoplayDelayMs = 500;
// G28 can remain silent for the full move from POS3. Apply one five-minute limit
// instead of declaring a healthy blocking home stalled after 18 or 45 seconds.
constexpr uint32_t kStartupHomeTimeoutMs = 300000;
constexpr uint32_t kStartupHomeRetryDelayMs = 2500;
constexpr uint32_t kStartupHomeRetryBackoffMs = 2500;
constexpr uint8_t kStartupHomeRetryLimit = 4;
constexpr uint32_t kSpiderControlEchoIgnoreMs = 2000;
constexpr uint8_t kMotionStopRepeatCount = 2;
constexpr uint32_t kWebSocketHeartbeatMs = 10000;
constexpr uint32_t kWebSocketPongTimeoutMs = 3000;
constexpr uint8_t kWebSocketDisconnectCount = 2;
constexpr uint32_t kManualResetVerifyTimeoutMs = 15000;
constexpr uint32_t kEmbeddedMarkerTimeoutMs = 300000;
constexpr uint32_t kRfInputArmDelayMs = 3000;
constexpr size_t kSerialLineMax = 256;
constexpr size_t kMaxRandomCodes = 16;

constexpr char kAxes[] = { 'X', 'Y', 'Z', 'A', 'B', 'C' };
constexpr size_t kAxisCount = sizeof(kAxes) / sizeof(kAxes[0]);
constexpr char kSpiderHomeCommand[] = "G28 X Y Z A B C";
constexpr char kSpiderHomeCompleteCommand[] = "M118 Spider homing complete.";

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
  kButtonReserve,
  kButtonCount
};

struct ButtonState {
  const char* name;
  uint8_t pin;
  bool stableLevel;
  bool lastRead;
  uint32_t lastChangeMs;

  ButtonState(const char* buttonName, const uint8_t buttonPin)
    : name(buttonName), pin(buttonPin), stableLevel(kRfButtonsActiveLow ? HIGH : LOW), lastRead(kRfButtonsActiveLow ? HIGH : LOW), lastChangeMs(0) {}
};

DNSServer dnsServer;
#if defined(ESP8266)
ESP8266WebServer server(80);
HardwareSerial& octopusSerial = Serial; // UART0 is reserved for Marlin, never debug logs.
#else
WebServer server(80);
HardwareSerial octopusSerial(2);
#endif
WebSocketsServer webSocket(81);

float axisPositions[kAxisCount] = { 0, 0, 0, 0, 0, 0 };
uint16_t randomCodes[kMaxRandomCodes] = {};
size_t randomCodeCount = 0;
ButtonState buttons[kButtonCount] = {
  { "LIGHT_ON", kLightOnPin },
  { "LIGHT_OFF", kLightOffPin },
  { "POS1", kPos1Pin },
  { "POS2", kPos2Pin },
  { "RANDOM", kRandomPin },
  { "DIMMER", kDimmerPin },
  { "POS3", kPos3Pin },
  { "RESERVE", kReservePin },
};

LightState lightState;
bool octopusOnline = false;
bool octopusFirmwareReady = false;
bool remoteOnline = false;
bool spiderProgramActive = false;
bool spiderProgramPaused = false;
bool octopusHomed = false;
struct EmbeddedPlayback {
  const embedded_gcodes::Program* program = nullptr;
  size_t offset = 0;
  bool waitingForHome = false;
  bool waitingForMarker = false;
  bool skipFirstMove = false;
  uint32_t homeStartedMs = 0;
  uint32_t markerSentMs = 0;
  String marker;
};
EmbeddedPlayback embeddedPlayback;
uint32_t embeddedMarkerSerial = 0;
int currentFeedRate = 305; // 50 on the 0-100 dashboard scale (10-600 mm/min).
int8_t dimmerDirection = 1;
bool startupHomePending = true;
bool startupHomeInProgress = false;
bool startupAutoRunPending = true;
bool startupAutoRunArmed = false;
bool octopusResetVerifyPending = false;
bool startupHomeRecoveryResetUsed = false;
bool spiderPauseForAdjustment = false;
bool manualSpiderPauseRequested = false;
bool spiderLoopRestartPending = false;
bool rfInputsArmed = false;

uint32_t lastStatePollMs = 0;
uint32_t lastRandomCodeQueryMs = 0;
uint32_t lastOctopusRxMs = 0;
uint32_t lastOctopusRecoveryMs = 0;
uint32_t lastOctopusSerialReinitMs = 0;
uint32_t lastOctopusHardwareResetMs = 0;
uint32_t lastRemoteActivityMs = 0;
uint32_t lastHealthCheckMs = 0;
uint32_t startupHomeReadyMs = 0;
uint32_t startupHomeStartedMs = 0;
uint32_t startupAutoRunReadyMs = 0;
uint32_t spiderLoopRestartReadyMs = 0;
uint32_t spiderControlEchoIgnoreUntilMs = 0;
uint32_t octopusResetVerifyDeadlineMs = 0;
uint32_t rfInputsArmReadyMs = 0;
uint8_t lowHeapStrikeCount = 0;
uint8_t connectedStationCount = 0;
uint8_t startupHomeRetryCount = 0;

String serialLine;
String remoteIpString = "RF 8CH";
String desiredSpiderLoopCommand;
String logHistory[kLogHistorySize];
size_t logHistoryStart = 0;
size_t logHistoryCount = 0;
volatile uint32_t rfInterruptMask = 0;

uint32_t dimmerPressedMs = 0;
uint32_t lastDimmerRepeatMs = 0;
bool dimmerRepeatBlockedUntilRelease = false;

void sendToOctopus(const String& line, const String& source = kBoardName, const bool logTx = true);
bool handleEmbeddedM215(const String& line, const String& source);
void pumpEmbeddedProgram();
void stopEmbeddedProgram();
bool pauseSpiderProgramForAdjustment(const String& source, const String& reason);
void resumeSpiderProgramAfterAdjustment(const String& source, const String& reason);
void cancelStartupAutomation(const String& source, const char* reason);
void runStartupLoopIfReady();
void clearDesiredSpiderLoop();
void setDesiredSpiderLoop(const String& gcode);
void scheduleSpiderLoopRestart(const String& source, const String& reason, const uint32_t delayMs = 150);
void runSpiderLoopRestartIfReady();
bool startupAutomationActive();
void updateDimmerHold(const uint32_t now);
void issueImmediateOverride(const String& source, const char* reason = nullptr);
void moveAllLegsToPosition(const String& source, const char* reason, const int target);
bool requestAutomaticOctopusReset(const String& source, const String& reason, const String& statusMessage);
void markOctopusFirmwareReady();
void syncMotionSpeedToOctopus(const String& source, const bool spiderJob);
void syncLampStateToOctopus(const String& source);
void sendSpiderHomeCommand(const String& source, const bool logTx = true, const bool emitCompletionMarker = true);

#if defined(ESP32)
void IRAM_ATTR onRfLightOnChange() { rfInterruptMask |= (1UL << kButtonLightOn); }
void IRAM_ATTR onRfLightOffChange() { rfInterruptMask |= (1UL << kButtonLightOff); }
void IRAM_ATTR onRfPos1Change() { rfInterruptMask |= (1UL << kButtonPos1); }
void IRAM_ATTR onRfPos2Change() { rfInterruptMask |= (1UL << kButtonPos2); }
void IRAM_ATTR onRfRandomChange() { rfInterruptMask |= (1UL << kButtonRandom); }
void IRAM_ATTR onRfDimmerChange() { rfInterruptMask |= (1UL << kButtonDimmer); }
void IRAM_ATTR onRfPos3Change() { rfInterruptMask |= (1UL << kButtonPos3); }
void IRAM_ATTR onRfReserveChange() { rfInterruptMask |= (1UL << kButtonReserve); }

using ButtonInterruptHandler = void (*)();
constexpr ButtonInterruptHandler kRfInterruptHandlers[kButtonCount] = {
  onRfLightOnChange,
  onRfLightOffChange,
  onRfPos1Change,
  onRfPos2Change,
  onRfRandomChange,
  onRfDimmerChange,
  onRfPos3Change,
  onRfReserveChange
};
#endif

void beginOctopusSerial() {
#if defined(ESP8266)
  octopusSerial.setRxBufferSize(1024);
  octopusSerial.begin(kOctopusBaud);
  octopusSerial.setDebugOutput(false);
  // Terminate any partial line left by the ESP8266 ROM's boot output.
  octopusSerial.print('\n');
#else
  octopusSerial.begin(kOctopusBaud, SERIAL_8N1, kOctopusRxPin, kOctopusTxPin);
#endif
}

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

bool isRfPressed(const bool level) {
  return kRfButtonsActiveLow ? level == LOW : level == HIGH;
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
  return line == "ok"
      || line.startsWith("X:")
      || line.startsWith("busy:")
      || line.startsWith("echo:busy:");
}

bool isBenignSpiderStatusLine(const String& line) {
  return line == "No active spider SD file to abort.";
}

bool octopusLineSuggestsFirmwareReady(const String& line) {
  if (line.startsWith("FIRMWARE_NAME:") || line.startsWith("Cap:")) return true;
  if (line.startsWith("echo:")) return true;
  if (line.startsWith("Case light:")) return true;
  if (line.startsWith("Testing ")) return true;
  if (line.startsWith("Running spider ")) return true;
  if (line.startsWith("Looping spider SD file:")) return true;
  if (line.startsWith("Spider ")) return true;
  if (line.startsWith("Paused spider SD file.")) return true;
  if (line.startsWith("Resumed spider SD file.")) return true;
  if (line.startsWith("Aborted spider SD file.")) return true;
  if (line.startsWith("No active spider SD file")) return true;
  if (line.startsWith("SD card ")) return true;
  return false;
}

bool shouldIgnoreSpiderControlEcho(const String& line) {
  if (millis() >= spiderControlEchoIgnoreUntilMs) return false;
  return line == "Paused spider SD file."
      || line == "Aborted spider SD file."
      || line == "No active spider SD file to abort."
      || line.indexOf("busy: paused for user") >= 0;
}

void suppressSpiderControlEchoes() {
  spiderControlEchoIgnoreUntilMs = millis() + kSpiderControlEchoIgnoreMs;
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
#if defined(ESP32)
  Serial.println(line);
#endif
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

  JsonObject remoteButtons = doc["remoteButtons"].to<JsonObject>();
  remoteButtons["lightOn"] = isRfPressed(buttons[kButtonLightOn].stableLevel);
  remoteButtons["lightOff"] = isRfPressed(buttons[kButtonLightOff].stableLevel);
  remoteButtons["pos1"] = isRfPressed(buttons[kButtonPos1].stableLevel);
  remoteButtons["pos2"] = isRfPressed(buttons[kButtonPos2].stableLevel);
  remoteButtons["random"] = isRfPressed(buttons[kButtonRandom].stableLevel);
  remoteButtons["dimmer"] = isRfPressed(buttons[kButtonDimmer].stableLevel);
  remoteButtons["pos3"] = isRfPressed(buttons[kButtonPos3].stableLevel);

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

  // M215 remains the UI/RF command vocabulary. Its programs now live in ESP
  // flash, so never forward M215 to Marlin's SD-card implementation.
  if (handleEmbeddedM215(line, source)) return;

  if (logTx)
    logMessage(source, String("TX -> Octopus: ") + line);
  octopusSerial.print(line);
  octopusSerial.print('\n');
}

void sendSpiderHomeCommand(const String& source, const bool logTx, const bool emitCompletionMarker) {
  sendToOctopus(kSpiderHomeCommand, source, logTx);
  if (emitCompletionMarker)
    sendToOctopus(kSpiderHomeCompleteCommand, source, false);
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

void queryRandomCodes(const String& source) {
  (void)source;
  lastRandomCodeQueryMs = millis();
  static_assert(sizeof(embedded_gcodes::randomPrograms) / sizeof(embedded_gcodes::randomPrograms[0]) <= kMaxRandomCodes, "Too many embedded random programs");
  randomCodeCount = sizeof(embedded_gcodes::randomPrograms) / sizeof(embedded_gcodes::randomPrograms[0]);
  for (size_t i = 0; i < randomCodeCount; ++i)
    randomCodes[i] = static_cast<uint16_t>(i + 1);
  broadcastState();
}

void stopEmbeddedProgram() {
  embeddedPlayback.program = nullptr;
  embeddedPlayback.offset = 0;
  embeddedPlayback.waitingForHome = false;
  embeddedPlayback.waitingForMarker = false;
  embeddedPlayback.skipFirstMove = false;
  embeddedPlayback.homeStartedMs = 0;
  embeddedPlayback.marker = "";
  spiderProgramActive = false;
  spiderProgramPaused = false;
}

void startEmbeddedProgram(const embedded_gcodes::Program& program, const String& source) {
  stopEmbeddedProgram();
  embeddedPlayback.program = &program;
  embeddedPlayback.waitingForHome = !octopusHomed;
  embeddedPlayback.homeStartedMs = millis();
  spiderProgramActive = true;

  // Match the acceleration limits formerly applied by Marlin's SD launcher.
  sendToOctopus("M201 X50 Y50 Z50 A50 B50 C50", source, false);
  sendToOctopus("M204 P15 T15", source, false);
  if (embeddedPlayback.waitingForHome)
    sendSpiderHomeCommand(source, false, true);

  logMessage(source, String("Running ESP flash program: ") + program.name);
  broadcastState();
}

bool handleEmbeddedM215(const String& line, const String& source) {
  String command = line;
  command.trim();
  command.toUpperCase();
  if (command != "M215" && !command.startsWith("M215 ")) return false;

  if (command == "M215 X") {
    stopEmbeddedProgram();
    broadcastState();
    return true;
  }
  if (command == "M215 P") {
    if (embeddedPlayback.program) {
      spiderProgramPaused = true;
      broadcastState();
    }
    return true;
  }
  if (command == "M215 R") {
    if (embeddedPlayback.program) {
      spiderProgramPaused = false;
      broadcastState();
    }
    return true;
  }
  if (command == "M215" || command == "M215 L") {
    queryRandomCodes(source);
    logMessage(source, "M215 programs are embedded in ESP flash: S1-S7, P1-P3; H homes, P pauses, R resumes, X stops.");
    return true;
  }
  if (command == "M215 H") {
    stopEmbeddedProgram();
    octopusHomed = false;
    sendSpiderHomeCommand(source, true, true);
    return true;
  }
  if (command.startsWith("M215 P") && command.length() == 7) {
    const int preset = command[6] - '1';
    if (preset >= 0 && preset < 3) {
      startEmbeddedProgram(embedded_gcodes::presetPrograms[preset], source);
      return true;
    }
  }
  if (command.startsWith("M215 S")) {
    const int code = command.substring(6).toInt();
    if (code >= 1 && code <= static_cast<int>(sizeof(embedded_gcodes::randomPrograms) / sizeof(embedded_gcodes::randomPrograms[0]))) {
      startEmbeddedProgram(embedded_gcodes::randomPrograms[code - 1], source);
      return true;
    }
  }
  logMessage(source, String("Unknown ESP flash program command: ") + line);
  return true;
}

bool nextEmbeddedCommand(String& command) {
  command = "";
  while (embeddedPlayback.program) {
    const char value = static_cast<char>(pgm_read_byte(embeddedPlayback.program->text + embeddedPlayback.offset));
    if (!value) {
      if (!embeddedPlayback.program->loop) return false;
      embeddedPlayback.offset = 0;
      embeddedPlayback.skipFirstMove = true;
      continue;
    }
    ++embeddedPlayback.offset;
    if (value != '\n') {
      if (value != '\r' && command.length() < 95) command += value;
      continue;
    }
    const int comment = command.indexOf(';');
    if (comment >= 0) command.remove(comment);
    command.trim();
    if (command == "@LOOP") {
      logMessage(kBoardName, String("Looping ESP flash program: ") + embeddedPlayback.program->name);
      embeddedPlayback.offset = 0;
      embeddedPlayback.skipFirstMove = true;
      command = "";
      continue;
    }
    if (embeddedPlayback.skipFirstMove && command.startsWith("G1 ")) {
      embeddedPlayback.skipFirstMove = false;
      command = "";
      continue;
    }
    if (command.length()) return true;
  }
  return false;
}

void pumpEmbeddedProgram() {
  if (!embeddedPlayback.program) return;
  if (embeddedPlayback.waitingForHome) {
    if (millis() - embeddedPlayback.homeStartedMs < kStartupHomeTimeoutMs) return;
    logMessage(kBoardName, "ESP G-code stream timed out waiting for homing; stopping motion.");
    clearDesiredSpiderLoop();
    stopEmbeddedProgram();
    sendToOctopus("M410", kBoardName, false);
    broadcastStatus("G-code stream stopped: homing did not complete.");
    broadcastState();
    return;
  }
  if (spiderProgramPaused || !octopusFirmwareReady) return;

  if (embeddedPlayback.waitingForMarker) {
    if (millis() - embeddedPlayback.markerSentMs < kEmbeddedMarkerTimeoutMs) return;
    logMessage(kBoardName, "ESP G-code stream lost its Octopus acknowledgement; stopping motion.");
    clearDesiredSpiderLoop();
    stopEmbeddedProgram();
    sendToOctopus("M410", kBoardName, false);
    broadcastStatus("G-code stream stopped: Octopus did not acknowledge a command.");
    broadcastState();
    return;
  }

  String command;
  command.reserve(96);
  if (!nextEmbeddedCommand(command)) {
    const String name = embeddedPlayback.program->name;
    stopEmbeddedProgram();
    logMessage(kBoardName, String("ESP flash program finished: ") + name);
    broadcastState();
    return;
  }

  embeddedPlayback.marker = "LUMIAC_ACK_" + String(++embeddedMarkerSerial);
  embeddedPlayback.waitingForMarker = true;
  embeddedPlayback.markerSentMs = millis();
  sendToOctopus(command, kBoardName, false);
  sendToOctopus("M118 " + embeddedPlayback.marker, kBoardName, false);
}

bool canQueryRandomCodesNow() {
  return !startupAutomationActive()
      && !spiderProgramActive
      && !spiderProgramPaused;
}

bool canPollOctopusStateNow() {
  return !startupAutomationActive()
      && !spiderProgramActive
      && !spiderProgramPaused;
}

bool octopusWatchdogSuspended() {
  return startupAutomationActive()
      || spiderProgramActive
      || spiderProgramPaused;
}

void scheduleStartupHomeRetry(const uint32_t now, const String& reason) {
  if (startupHomeRetryCount < kStartupHomeRetryLimit) {
    ++startupHomeRetryCount;
    startupHomePending = true;
    const uint32_t retryDelayMs = kStartupHomeRetryDelayMs
      + (static_cast<uint32_t>(startupHomeRetryCount - 1) * kStartupHomeRetryBackoffMs);
    startupHomeReadyMs = now + retryDelayMs;
    logMessage(
      kBoardName,
      reason + " Retrying startup home " + startupHomeRetryCount + "/" + kStartupHomeRetryLimit
      + " after " + retryDelayMs + "ms."
    );
    broadcastStatus(
      "Startup home retrying " + String(startupHomeRetryCount) + "/" + String(kStartupHomeRetryLimit) + "."
    );
    return;
  }

  startupHomeRetryCount = 0;
  if (!startupHomeRecoveryResetUsed) {
    const bool resetRequested = requestAutomaticOctopusReset(
      kBoardName,
      "Automatic reset after repeated startup home failures",
      "Startup home recovery reset"
    );
    if (resetRequested) {
      startupHomeRecoveryResetUsed = true;
      broadcastStatus("Startup home recovery reset requested.");
      return;
    }
  }

  logMessage(kBoardName, reason + " Waiting for manual Home or Reset.");
  broadcastStatus("Startup home stalled.");
}

void markOctopusFirmwareReady() {
  const bool wasFirmwareReady = octopusFirmwareReady;
  octopusOnline = true;
  octopusFirmwareReady = true;
  if (wasFirmwareReady) return;

  spiderProgramActive = false;
  spiderProgramPaused = false;
  startupHomeInProgress = false;
  startupAutoRunArmed = false;
  startupAutoRunReadyMs = 0;
  spiderPauseForAdjustment = false;

  if (octopusResetVerifyPending) {
    octopusResetVerifyPending = false;
    logMessage(kBoardName, "Octopus reset confirmed by firmware response.");
    syncMotionSpeedToOctopus(kBoardName, false);
    startupHomePending = true;
    startupAutoRunPending = true;
    startupHomeReadyMs = millis() + kResetHomeDelayMs;
    broadcastStatus("Octopus reset complete. Restoring speed and home.");
  }
  else if (startupHomePending && !startupHomeInProgress) {
    startupHomeReadyMs = millis() + kStartupHomeDelayMs;
    broadcastStatus("Octopus online. Waiting before startup home.");
  }

  if (canPollOctopusStateNow())
    sendToOctopus("M114", kBoardName, false);
  if (canQueryRandomCodesNow())
    queryRandomCodes(kBoardName);
  syncLampStateToOctopus(kBoardName);
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

  if (gcode == "M215" || gcode == "M215 L" || gcode == "M215 P" || gcode == "M215 R") return false;
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
  return gcode == "M410" || gcode == "M215 X" || gcode.startsWith("G28");
}

void abortActiveSpiderProgram(const String& source, const char* reason = nullptr) {
  if (!spiderProgramActive) return;

  if (reason && *reason)
    logMessage(source, String("Aborting active spider program before ") + reason + ".");

  sendToOctopus("M215 X", source, false);
  spiderProgramActive = false;
  spiderProgramPaused = false;
}

void sendMotionStopBurst(const String& source, const char* reason = nullptr) {
  if (reason && *reason)
    logMessage(source, String("Issuing stop burst before ") + reason + ".");

  for (uint8_t i = 0; i < kMotionStopRepeatCount; ++i)
    sendToOctopus("M410", source, false);
}

void clearOctopusPausedState(const String& source, const char* reason = nullptr) {
  if (reason && *reason)
    logMessage(source, String("Clearing Octopus pause state before ") + reason + ".");

  sendToOctopus("M108", source, false);
}

void forceAbortSpiderProgram(const String& source, const char* reason = nullptr) {
  if (reason && *reason)
    logMessage(source, String("Forcing spider abort before ") + reason + ".");

  sendToOctopus("M215 X", source, false);
  spiderProgramActive = false;
  spiderProgramPaused = false;
  spiderPauseForAdjustment = false;
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
  issueImmediateOverride(source, reason);
  syncMotionSpeedToOctopus(source, spiderJob);
}

void issueImmediateOverride(const String& source, const char* reason) {
  suppressSpiderControlEchoes();
  clearOctopusPausedState(source, reason);
  sendMotionStopBurst(source, reason);
  forceAbortSpiderProgram(source, reason);
  sendToOctopus("M410", source, false);
  spiderProgramActive = false;
  spiderProgramPaused = false;
  spiderPauseForAdjustment = false;
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
  suppressSpiderControlEchoes();
  sendToOctopus(desiredSpiderLoopCommand, kBoardName);
  spiderProgramActive = true;
  spiderProgramPaused = false;
  broadcastStatus("Restarting spider loop.");
}

void cancelStartupAutomation(const String& source, const char* reason) {
  if (!(startupHomePending || startupHomeInProgress || startupAutoRunPending || startupAutoRunArmed)) return;

  startupHomePending = false;
  startupHomeInProgress = false;
  startupHomeStartedMs = 0;
  startupHomeRetryCount = 0;
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

void runSpiderLoopS1(const String& source, const char* reason) {
  cancelStartupAutomation(source, reason);
  prepareMotionCommand(source, reason, true);
  setDesiredSpiderLoop("M215 S1");
  suppressSpiderControlEchoes();
  sendToOctopus("M215 S1", source);
  spiderProgramActive = true;
  spiderProgramPaused = false;
  broadcastStatus("Running spider loop S1.");
  broadcastState();
}

void stopMotionAndTurnLightsOff(const String& source, const char* reason) {
  clearDesiredSpiderLoop();
  cancelStartupAutomation(source, reason);
  clearOctopusPausedState(source, reason);
  forceAbortSpiderProgram(source, reason);
  sendMotionStopBurst(source, reason);
  spiderProgramActive = false;
  spiderProgramPaused = false;
  turnLightsOff(source, reason);
  broadcastStatus("Lights off. Motion stopped.");
  broadcastState();
}

void moveAllLegsToPosition(const String& source, const char* reason, const int target) {
  const int clampedTarget = constrain(target, 0, kMaxAxisPosition);
  clearDesiredSpiderLoop();
  cancelStartupAutomation(source, reason);
  prepareMotionCommand(source, reason);

  String gcode = "G1";
  for (size_t i = 0; i < kAxisCount; ++i) {
    axisPositions[i] = static_cast<float>(clampedTarget);
    gcode += ' ';
    gcode += kAxes[i];
    gcode += String(clampedTarget);
  }
  gcode += " F";
  gcode += String(currentFeedRate);

  sendToOctopus("G90", source, false);
  sendToOctopus(gcode, source);
  spiderProgramActive = false;
  spiderProgramPaused = false;
  broadcastStatus(String(reason) + " -> all legs to " + String(clampedTarget) + ".");
  broadcastState();
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
      runSpiderLoopS1(kRemoteBoardName, "RF LIGHT_ON");
      return;
    case kButtonLightOff:
      stopMotionAndTurnLightsOff(kRemoteBoardName, "RF LIGHT_OFF");
      return;
    case kButtonPos1:
      moveAllLegsToPosition(kRemoteBoardName, "RF P1", 0);
      return;
    case kButtonPos2:
      moveAllLegsToPosition(kRemoteBoardName, "RF P2", 55);
      return;
    case kButtonRandom:
      handleSimpleAction("random_position", kRemoteBoardName);
      return;
    case kButtonDimmer:
      dimmerPressedMs = millis();
      lastDimmerRepeatMs = dimmerPressedMs;
      dimmerRepeatBlockedUntilRelease = false;
      stepDimmer(kRemoteBoardName, "RF DIMMER");
      return;
    case kButtonPos3:
      moveAllLegsToPosition(kRemoteBoardName, "RF P3", 120);
      return;
    case kButtonReserve:
      logMessage(kRemoteBoardName, "RESERVE button pressed. No action assigned.");
      broadcastStatus("Reserve RF button pressed.");
      return;
    default:
      return;
  }
}

void handleRfButtonReleased(const ButtonIndex index) {
  if (index != kButtonDimmer) return;

  dimmerPressedMs = 0;
  lastDimmerRepeatMs = 0;
  dimmerRepeatBlockedUntilRelease = false;
  dimmerDirection = -dimmerDirection;
  logMessage(kRemoteBoardName, String("DIMMER released. Next direction=") + (dimmerDirection > 0 ? "up" : "down"));
}

void pollRfButtons() {
  const uint32_t now = millis();

  if (!rfInputsArmed) {
    for (size_t i = 0; i < kButtonCount; ++i) {
      ButtonState& button = buttons[i];
      if (button.pin == kUnusedPin) continue;
      const bool level = digitalRead(button.pin);
      button.stableLevel = level;
      button.lastRead = level;
      button.lastChangeMs = now;
    }

    if (now < rfInputsArmReadyMs) return;
    if (startupAutomationActive()) return;

    rfInputsArmed = true;
    noInterrupts();
    rfInterruptMask = 0;
    interrupts();
    logMessage(kBoardName, "RF inputs armed after startup automation completed.");
    return;
  }

  uint32_t interruptMask = 0;
  noInterrupts();
  interruptMask = rfInterruptMask;
  rfInterruptMask = 0;
  interrupts();

  bool stateChanged = false;
  bool debouncePending = false;

  for (size_t i = 0; i < kButtonCount; ++i) {
    ButtonState& button = buttons[i];
    if (button.pin == kUnusedPin) continue;
#if defined(ESP8266)
    // ESP8266 GPIO16 cannot trigger interrupts. Poll all seven inputs instead.
    const bool sampleInput = true;
#else
    const bool sampleInput = interruptMask & (1UL << i);
#endif
    if (sampleInput) {
      const bool level = digitalRead(button.pin);
      if (level != button.lastRead) {
        button.lastRead = level;
        button.lastChangeMs = now;
      }
    }

    if (button.stableLevel == button.lastRead) continue;
    if (now - button.lastChangeMs < kDebounceMs) {
      debouncePending = true;
      continue;
    }

    button.stableLevel = button.lastRead;
    if (isRfPressed(button.stableLevel)) {
      handleRfButtonPressed(static_cast<ButtonIndex>(i));
    } else {
      handleRfButtonReleased(static_cast<ButtonIndex>(i));
    }
    stateChanged = true;
  }

  updateDimmerHold(now);

  if (stateChanged)
    broadcastState();

  if (!stateChanged && !debouncePending && buttons[kButtonDimmer].stableLevel != LOW && !interruptMask)
    return;
}

void updateDimmerHold(const uint32_t now) {
  if (!isRfPressed(buttons[kButtonDimmer].stableLevel)) return;
  if (!dimmerPressedMs) return;
  if (dimmerRepeatBlockedUntilRelease) return;
  if (now - dimmerPressedMs < kDimmerHoldStartMs) return;

  if (now - dimmerPressedMs >= kDimmerHoldSafetyMs) {
    dimmerRepeatBlockedUntilRelease = true;
    logMessage(kRemoteBoardName, "DIMMER hold safety cutoff reached. Waiting for release.");
    broadcastState();
    return;
  }

  if (now - lastDimmerRepeatMs < kDimmerHoldRepeatMs) return;

  lastDimmerRepeatMs = now;
  markRemoteActivity("dimmer hold");
  stepDimmer(kRemoteBoardName, "RF DIMMER HOLD");
  broadcastState();
}

void runRandomPosition(const String& source) {
  if (!randomCodeCount) queryRandomCodes(source);

  const uint16_t code = randomCodes[random(static_cast<long>(randomCodeCount))];
  prepareMotionCommand(source, "random position", true);
  setDesiredSpiderLoop("M215 S" + String(code));
  suppressSpiderControlEchoes();
  sendToOctopus("M215 S" + String(code), source);
  spiderProgramActive = true;
  spiderProgramPaused = false;
  broadcastStatus("Running random spider position S" + String(code) + ".");
}

void runStartupHomeIfReady() {
  if (!startupHomePending) return;
  if (!octopusOnline) return;
  if (!octopusFirmwareReady) return;

  const uint32_t now = millis();
  if (now < startupHomeReadyMs) return;

  startupHomePending = false;
  startupHomeInProgress = true;
  startupHomeStartedMs = now;
  broadcastStatus("Running startup home.");
  // Startup home runs after a clean firmware boot or explicit reset, so avoid sending
  // extra stop / abort commands right before the blocking homing routine.
  syncMotionSpeedToOctopus(kBoardName);
  sendSpiderHomeCommand(kBoardName, true, true);
}

void recoverStartupHomeIfStalled() {
  if (!startupHomeInProgress) return;
  if (!startupHomeStartedMs) return;

  const uint32_t now = millis();
  if (now - startupHomeStartedMs < kStartupHomeTimeoutMs) return;

  startupHomeInProgress = false;
  startupHomeStartedMs = 0;
  issueImmediateOverride(kBoardName, "startup home recovery");
  scheduleStartupHomeRetry(
    now,
    "Startup home exceeded the five-minute limit."
  );
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
  suppressSpiderControlEchoes();
  sendToOctopus("M215 S1", kBoardName);
  spiderProgramActive = true;
  spiderProgramPaused = false;
  broadcastStatus("Startup home complete. Running S1.");
}

bool startupAutomationActive() {
  return startupHomePending
      || startupHomeInProgress
      || startupAutoRunPending
      || startupAutoRunArmed;
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

void handleOctopusLine(const String& rawLine) {
  String line = rawLine;
  line.trim();
  if (!line.length()) return;
  const String markerLine = line.startsWith("echo:") ? line.substring(5) : line;
  if (markerLine.startsWith("LUMIAC_ACK_")) {
    lastOctopusRxMs = millis();
    if (embeddedPlayback.waitingForMarker && markerLine == embeddedPlayback.marker)
      embeddedPlayback.waitingForMarker = false;
    return;
  }
  const bool spiderRunLine = line.startsWith("Running spider ") && line.indexOf(':') >= 0;
  const bool spiderLoopLine = line.startsWith("Looping spider SD file:");
  const bool explicitFirmwareReadyLine = line.startsWith("FIRMWARE_NAME:") || line.startsWith("Cap:");

  if (!isTelemetryOnlyLine(line))
    if (!isBenignSpiderStatusLine(line))
      logMessage("Octopus", line);
  lastOctopusRxMs = millis();
  parseLampStateLine(line);
  parsePositionLine(line);
  if (embeddedPlayback.program && (line.startsWith("Error:") || line.startsWith("Resend:"))) {
    clearDesiredSpiderLoop();
    stopEmbeddedProgram();
    sendToOctopus("M410", kBoardName, false);
    broadcastStatus("G-code stream stopped after an Octopus command error.");
    broadcastState();
  }

  if (!octopusFirmwareReady && octopusLineSuggestsFirmwareReady(line)) {
    if (!explicitFirmwareReadyLine)
      logMessage(kBoardName, "Octopus firmware readiness inferred from serial activity.");
    markOctopusFirmwareReady();
  }

  if (shouldIgnoreSpiderControlEcho(line))
    return;

  if (spiderRunLine || spiderLoopLine || line == "Resumed spider SD file." || line == "Paused spider SD file.")
    spiderProgramActive = true;

  if (spiderRunLine || spiderLoopLine || line == "Resumed spider SD file.")
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
  }

  if (line == "Paused spider SD file." && !spiderPauseForAdjustment && !manualSpiderPauseRequested)
    scheduleSpiderLoopRestart("Octopus", "unsolicited pause");

  if (spiderRunLine || spiderLoopLine || line == "Resumed spider SD file.") {
    spiderLoopRestartPending = false;
    spiderLoopRestartReadyMs = 0;
    manualSpiderPauseRequested = false;
    if (line == "Resumed spider SD file.")
      spiderPauseForAdjustment = false;
  }

  if (line == "Spider homing complete." || line == "Spider grouped homing complete.") {
    octopusHomed = true;
    embeddedPlayback.waitingForHome = false;
  }

  if ((line == "Spider homing complete." || line == "Spider grouped homing complete.") && startupHomeInProgress && startupAutoRunPending) {
    startupHomeInProgress = false;
    startupHomeStartedMs = 0;
    startupHomeRetryCount = 0;
    startupHomeRecoveryResetUsed = false;
    startupAutoRunArmed = true;
    startupAutoRunReadyMs = millis() + kStartupAutoplayDelayMs;
    broadcastStatus("Startup home complete. Preparing S1.");
  }

  if (explicitFirmwareReadyLine)
    markOctopusFirmwareReady();
}

void pollOctopusState() {
  const uint32_t now = millis();
  if (now - lastStatePollMs < kStatePollMs) return;
  if (!canPollOctopusStateNow()) return;
  lastStatePollMs = now;
  sendToOctopus("M114", kBoardName, false);
}

void pollRandomCodes() {
  const uint32_t now = millis();
  if (randomCodeCount || now - lastRandomCodeQueryMs < kRandomCodePollMs) return;
  if (!canQueryRandomCodesNow()) return;
  queryRandomCodes(kBoardName);
}

void pulseOctopusResetLine(const String& reason) {
  logMessage(kBoardName, "Pulsing Octopus reset line: " + reason);

  octopusOnline = false;
  octopusFirmwareReady = false;
  octopusHomed = false;
  stopEmbeddedProgram();
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
  beginOctopusSerial();
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

bool requestAutomaticOctopusReset(const String& source, const String& reason, const String& statusMessage) {
  const uint32_t now = millis();

  if (lastOctopusHardwareResetMs && now - lastOctopusHardwareResetMs < kOctopusResetCooldownMs) {
    const uint32_t secondsRemaining = (kOctopusResetCooldownMs - (now - lastOctopusHardwareResetMs) + 999) / 1000;
    logMessage(source, reason + " blocked by Octopus reset cooldown. Wait " + String(secondsRemaining) + "s.");
    return false;
  }

  logMessage(source, reason + ".");
  broadcastStatus(statusMessage);
  octopusResetVerifyPending = true;
  octopusResetVerifyDeadlineMs = now + kManualResetVerifyTimeoutMs;
  pulseOctopusResetLine(reason);
  return true;
}

void recoverOctopusLink() {
  const uint32_t now = millis();

  if (startupAutomationActive() && !octopusFirmwareReady) {
    if (now - lastOctopusRecoveryMs >= kOctopusRecoveryRetryMs) {
      lastOctopusRecoveryMs = now;
      sendToOctopus("M115", kBoardName, false);
    }
    return;
  }

  if (octopusWatchdogSuspended()) return;

  if (!octopusOnline && now - lastOctopusRecoveryMs >= kOctopusRecoveryRetryMs) {
    lastOctopusRecoveryMs = now;
    sendToOctopus("M115", kBoardName, false);
  }

  if (now - lastOctopusRxMs < kOctopusSerialReinitMs) return;
  if (now - lastOctopusSerialReinitMs < kOctopusSerialReinitMs) return;

  lastOctopusSerialReinitMs = now;
  logMessage(kBoardName, "Octopus serial RX timeout persisted. Reinitializing Octopus UART.");
  octopusSerial.end();
  delay(20);
  beginOctopusSerial();
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
  const String outgoing = normalized == "M215 H" ? String(kSpiderHomeCommand) : gcode;
  const String outgoingNormalized = normalizedGcode(outgoing);
  const bool spiderStart = gcodeStartsSpiderProgram(outgoing);
  const bool motionCommand = gcodeNeedsMotionStop(outgoing) || gcodeIsImmediateStop(outgoing);

  if (source != kBoardName && motionCommand)
    cancelStartupAutomation(source, "manual command");

  if (outgoingNormalized == "M215 P")
    manualSpiderPauseRequested = true;
  else if (outgoingNormalized == "M215 R")
    manualSpiderPauseRequested = false;
  else if (outgoingNormalized.startsWith("M215 S")) {
    setDesiredSpiderLoop(outgoingNormalized);
    suppressSpiderControlEchoes();
  }
  else if (motionCommand || outgoingNormalized == "M215 P1" || outgoingNormalized == "M215 P2" || outgoingNormalized == "M215 P3")
    clearDesiredSpiderLoop();

  if (gcodeIsImmediateStop(outgoing)) {
    issueImmediateOverride(source, "stop command");
  }
  else if (gcodeNeedsMotionStop(outgoing)) {
    prepareMotionCommand(source, "terminal command", spiderStart);
  }

  if (outgoingNormalized == kSpiderHomeCommand) {
    octopusHomed = false;
    sendSpiderHomeCommand(source);
  }
  else {
    if (outgoingNormalized.startsWith("G28")) octopusHomed = false;
    sendToOctopus(outgoing, source);
  }

  if (spiderStart) {
    spiderProgramActive = true;
    spiderProgramPaused = false;
  }
  else if (gcodeStopsSpiderProgram(outgoing)) {
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
    octopusHomed = false;
    sendSpiderHomeCommand(source);
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
    issueImmediateOverride(source, "stop request");
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

void pollWiFiStations() {
  // Keep networking/log allocation in loop(), using an API shared by both cores.
  static uint32_t lastPollMs = 0;
  const uint32_t now = millis();
  if (now - lastPollMs < 1000) return;
  lastPollMs = now;
  const uint8_t count = WiFi.softAPgetStationNum();
  if (count == connectedStationCount) return;
  connectedStationCount = count;
  logMessage(kBoardName, String("Wi-Fi clients connected: ") + count);
}

void setupRfInputs() {
  for (size_t i = 0; i < kButtonCount; ++i) {
    if (buttons[i].pin == kUnusedPin) continue;
    pinMode(buttons[i].pin, INPUT);
    const bool level = digitalRead(buttons[i].pin);
    buttons[i].stableLevel = level;
    buttons[i].lastRead = level;
    buttons[i].lastChangeMs = millis();
#if defined(ESP32)
    attachInterrupt(digitalPinToInterrupt(buttons[i].pin), kRfInterruptHandlers[i], CHANGE);
#endif
  }

  rfInputsArmed = false;
  rfInputsArmReadyMs = millis() + kRfInputArmDelayMs;
  dimmerPressedMs = 0;
  lastDimmerRepeatMs = 0;
  dimmerRepeatBlockedUntilRelease = false;
  noInterrupts();
  rfInterruptMask = 0;
  interrupts();

  logMessage(kBoardName, "RF input map:");
  for (size_t i = 0; i < kButtonCount; ++i) {
    if (buttons[i].pin != kUnusedPin)
      logMessage(kBoardName, String("  ") + buttons[i].name + " -> GPIO " + buttons[i].pin);
  }
  logMessage(kBoardName, String("RF inputs will arm after startup automation completes and at least ") + (kRfInputArmDelayMs / 1000.0f) + "s have passed.");
}

} // namespace

void setup() {
#if defined(ESP32)
  Serial.begin(115200);
#endif
  initStringStorage();
  randomSeed(micros());

  digitalWrite(kOctopusResetPin, kOctopusResetActiveLow ? HIGH : LOW);
  pinMode(kOctopusResetPin, OUTPUT_OPEN_DRAIN);
  setupRfInputs();

  beginOctopusSerial();

#if defined(ESP8266)
  WiFi.persistent(false);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.hostname(kBoardName);
#else
  WiFi.setSleep(false);
  WiFi.setHostname(kBoardName);
#endif
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
  startupHomeReadyMs = 0;
  broadcastState();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  webSocket.loop();
  pollWiFiStations();

  readOctopusSerial();
  pumpEmbeddedProgram();
  pollRfButtons();
  pollOctopusState();
  pollRandomCodes();
  runStartupHomeIfReady();
  recoverStartupHomeIfStalled();
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

  if (octopusOnline && !octopusWatchdogSuspended() && now - lastOctopusRxMs > kOctopusOfflineMs) {
    octopusOnline = false;
    octopusFirmwareReady = false;
    logMessage(kBoardName, "Octopus serial RX heartbeat timed out.");
    broadcastState();
  }

  if (remoteOnline && now - lastRemoteActivityMs > kRemoteOfflineMs) {
    remoteOnline = false;
    logMessage(kBoardName, "Remote heartbeat timed out.");
    broadcastState();
  }
  yield();
}

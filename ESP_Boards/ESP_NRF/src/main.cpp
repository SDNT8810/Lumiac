#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <RF24.h>

#include "NrfOctopusProtocol.h"
#include "web_page.h"

namespace {

using namespace NrfOctopus;

constexpr char kBoardName[] = "ESP_NRF";
constexpr char kRemoteBoardName[] = "NRF_Remote";

constexpr char kApSsid[] = "ESP_NRF";
constexpr char kApPassword[] = "octopus123";
constexpr byte kDnsPort = 53;
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);

constexpr int kOctopusTxPin = 17;
constexpr int kOctopusRxPin = 16;
constexpr uint32_t kOctopusBaud = 115200;
constexpr int kOctopusResetPin = 23;
constexpr bool kOctopusResetActiveLow = true;
constexpr uint32_t kOctopusResetPulseMs = 250;

constexpr int kNrfCePin = 33;
constexpr int kNrfCsnPin = 25;
constexpr int kNrfSckPin = 14;
constexpr int kNrfMisoPin = 19;
constexpr int kNrfMosiPin = 13;

constexpr uint32_t kStatePollMs = 1500;
constexpr uint32_t kRandomCodePollMs = 15000;
constexpr uint32_t kRemoteOfflineMs = 20000;
constexpr uint32_t kOctopusOfflineMs = 5000;
constexpr uint32_t kOctopusRecoveryRetryMs = 3000;
constexpr uint32_t kOctopusSerialReinitMs = 30000;
constexpr uint32_t kOctopusHardwareResetTimeoutMs = 120000;
constexpr uint32_t kOctopusResetCooldownMs = 180000;
constexpr uint32_t kHealthCheckMs = 60000;
constexpr uint32_t kNrfReinitMs = 2000;
constexpr uint32_t kDimmerKeepAliveTimeoutMs = 900;
constexpr uint32_t kDimmerStepMs = 180;
constexpr uint8_t kDimmerStep = 16;
constexpr uint32_t kLowHeapThresholdBytes = 30000;
constexpr uint8_t kLowHeapStrikeLimit = 3;
constexpr size_t kSerialLineMax = 256;
constexpr size_t kMaxRandomCodes = 16;
constexpr size_t kLogHistorySize = 120;
constexpr size_t kLogReplayLimit = 30;

constexpr char kAxes[] = { 'X', 'Y', 'Z', 'A', 'B', 'C' };
constexpr size_t kAxisCount = sizeof(kAxes) / sizeof(kAxes[0]);

struct LightState {
  bool on = false;
  uint8_t brightness = 160;
  uint8_t lastNonZeroBrightness = 160;
};

DNSServer dnsServer;
WebServer server(80);
WebSocketsServer webSocket(81);
HardwareSerial octopusSerial(2);
RF24 radio(kNrfCePin, kNrfCsnPin);

float axisPositions[kAxisCount] = { 0, 0, 0, 0, 0, 0 };
uint16_t randomCodes[kMaxRandomCodes] = {};
size_t randomCodeCount = 0;
bool waitingForRandomCodeList = false;

LightState lightState;
bool octopusOnline = false;
bool remoteOnline = false;
bool radioReady = false;
bool dimmerActive = false;
int8_t dimmerDirection = 1;
int currentFeedRate = 200;

uint32_t lastStatePollMs = 0;
uint32_t lastRandomCodeQueryMs = 0;
uint32_t lastOctopusRxMs = 0;
uint32_t lastOctopusRecoveryMs = 0;
uint32_t lastOctopusSerialReinitMs = 0;
uint32_t lastOctopusHardwareResetMs = 0;
uint32_t lastRemoteActivityMs = 0;
uint32_t lastHealthCheckMs = 0;
uint32_t lastNrfInitMs = 0;
uint32_t lastDimmerKeepAliveMs = 0;
uint32_t lastDimmerStepMs = 0;
uint8_t lowHeapStrikeCount = 0;

uint8_t lastRemoteSessionId = 0;
uint16_t lastRemoteSequence = 0;

String serialLine;
String remoteIpString = "NRF24";
String logHistory[kLogHistorySize];
size_t logHistoryStart = 0;
size_t logHistoryCount = 0;

void sendToOctopus(const String& line, const String& source = kBoardName, const bool logTx = true);

void initStringStorage() {
  serialLine.reserve(kSerialLineMax);
  remoteIpString.reserve(24);
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

void markRemoteSeen(const char* reason) {
  lastRemoteActivityMs = millis();
  if (remoteOnline) return;

  remoteOnline = true;
  logMessage(kBoardName, String("NRF remote reachable") + (reason ? String(" (") + reason + ")" : ""));
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
  const int nextFeedRate = constrain(feed, 10, 2000);
  if (nextFeedRate == currentFeedRate) return;

  currentFeedRate = nextFeedRate;
  logMessage(source, reason + " -> F" + String(currentFeedRate));
  broadcastState();
}

void setLightState(const bool on, const uint8_t brightness, const String& source, const String& reason, const bool logChange = true, const bool syncToOctopus = true) {
  const bool nextOn = on && brightness > 0;
  const bool changed = nextOn != lightState.on || brightness != lightState.brightness;

  lightState.on = nextOn;
  lightState.brightness = brightness;
  if (brightness > 0)
    lightState.lastNonZeroBrightness = brightness;

  if (syncToOctopus)
    sendToOctopus(lampStateCommand(nextOn, brightness), source);

  if (!changed) return;

  if (logChange)
    logMessage(source, reason + " -> lights " + (lightState.on ? String("ON") : String("OFF")) + ", brightness=" + String(lightState.brightness));
  broadcastState();
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

void setBrightness(const int brightness, const String& source, const String& reason, const bool logChange = true) {
  const uint8_t nextBrightness = clampBrightness(brightness);
  setLightState(nextBrightness > 0, nextBrightness, source, reason, logChange);
}

void parseLampStateLine(const String& line) {
  if (!line.startsWith("Case light:")) return;

  String value = line.substring(11);
  value.trim();

  if (value.equalsIgnoreCase("off")) {
    setLightState(false, lightState.brightness, "Octopus", "Lamp state report", false, false);
    return;
  }

  if (value.equalsIgnoreCase("on")) {
    const uint8_t restored = lightState.lastNonZeroBrightness > 0 ? lightState.lastNonZeroBrightness : 160;
    setLightState(true, restored, "Octopus", "Lamp state report", false, false);
    return;
  }

  const int reportedBrightness = value.toInt();
  if (reportedBrightness < 0 || reportedBrightness > 255) return;
  setLightState(true, static_cast<uint8_t>(reportedBrightness), "Octopus", "Lamp state report", false, false);
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

void runRandomPosition(const String& source) {
  if (!randomCodeCount) {
    logMessage(source, "Random position requested before M215 code list was available. Requesting M215 list.");
    broadcastStatus("Random position unavailable yet. Querying M215 list.");
    queryRandomCodes(source);
    return;
  }

  const uint16_t code = randomCodes[random(static_cast<long>(randomCodeCount))];
  sendToOctopus("M215 S" + String(code), source);
  broadcastStatus("Running random spider position S" + String(code) + ".");
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
    logMessage("Octopus", line);
  lastOctopusRxMs = millis();
  parseLampStateLine(line);
  parsePositionLine(line);
  parseRandomCodeLine(line);

  if (line.startsWith("FIRMWARE_NAME:")) {
    octopusOnline = true;
    queryLampState(kBoardName);
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

bool initializeRadio(const bool logFailure = true) {
  const uint32_t now = millis();
  if (now - lastNrfInitMs < kNrfReinitMs)
    return radioReady;

  lastNrfInitMs = now;
  SPI.begin(kNrfSckPin, kNrfMisoPin, kNrfMosiPin, kNrfCsnPin);

  radioReady = radio.begin();
  if (!radioReady) {
    if (logFailure)
      logMessage(kBoardName, "NRF24 radio init failed.");
    return false;
  }

  radio.setChannel(108);
  radio.setDataRate(RF24_250KBPS);
  radio.setPALevel(RF24_PA_LOW);
  radio.setRetries(5, 15);
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(true);
  radio.setPayloadSize(sizeof(CommandPacket));
  radio.openReadingPipe(1, kRadioAddress);
  radio.startListening();

  if (!radio.isChipConnected()) {
    radioReady = false;
    if (logFailure)
      logMessage(kBoardName, "NRF24 radio not detected on SPI bus.");
    return false;
  }

  logMessage(kBoardName, "NRF24 receiver ready.");
  return true;
}

bool ensureRadioReady() {
  if (radioReady && radio.isChipConnected())
    return true;

  radioReady = false;
  return initializeRadio(false);
}

void updateRemoteActivity() {
  lastRemoteActivityMs = millis();
  if (!remoteOnline)
    markRemoteSeen("radio traffic");
}

void handleRemoteCommand(const CommandPacket& packet) {
  if (!isProtocolPacketValid(packet))
    return;

  updateRemoteActivity();

  if (packet.sessionId == lastRemoteSessionId && packet.sequence == lastRemoteSequence)
    return;

  lastRemoteSessionId = packet.sessionId;
  lastRemoteSequence = packet.sequence;

  switch (static_cast<CommandType>(packet.command)) {
    case CMD_LIGHT_ON:
      turnLightsOn(kRemoteBoardName, "NRF light ON request");
      break;

    case CMD_LIGHT_OFF:
      turnLightsOff(kRemoteBoardName, "NRF light OFF request");
      break;

    case CMD_POS1:
      sendToOctopus("M215 P1", kRemoteBoardName);
      break;

    case CMD_POS2:
      sendToOctopus("M215 P2", kRemoteBoardName);
      break;

    case CMD_RANDOM:
      runRandomPosition(kRemoteBoardName);
      break;

    case CMD_DIMMER_START:
      if (!dimmerActive)
        logMessage(kRemoteBoardName, "Dimmer hold started.");
      dimmerActive = true;
      lastDimmerKeepAliveMs = millis();
      lastDimmerStepMs = 0;
      break;

    case CMD_DIMMER_KEEPALIVE:
      dimmerActive = true;
      lastDimmerKeepAliveMs = millis();
      break;

    case CMD_DIMMER_STOP:
      if (dimmerActive) {
        dimmerActive = false;
        dimmerDirection = -dimmerDirection;
        logMessage(kRemoteBoardName, "Dimmer hold stopped. Reversing direction for next hold.");
      }
      break;

    case CMD_STATE_REQUEST:
      broadcastState();
      break;

    case CMD_NONE:
    default:
      break;
  }
}

void handleRadio() {
  if (!ensureRadioReady())
    return;

  while (radio.available()) {
    CommandPacket packet;
    radio.read(&packet, sizeof(packet));
    handleRemoteCommand(packet);
  }
}

void handleDimmerLoop() {
  if (!dimmerActive)
    return;

  const uint32_t now = millis();
  if (now - lastDimmerKeepAliveMs > kDimmerKeepAliveTimeoutMs) {
    dimmerActive = false;
    dimmerDirection = -dimmerDirection;
    logMessage(kBoardName, "Dimmer keepalive timed out. Reversing direction for next hold.");
    return;
  }

  if (lastDimmerStepMs != 0 && now - lastDimmerStepMs < kDimmerStepMs)
    return;

  lastDimmerStepMs = now;
  const int baseBrightness = lightState.on
    ? lightState.brightness
    : (lightState.lastNonZeroBrightness > 0 ? lightState.lastNonZeroBrightness : 160);
  const int nextBrightness = constrain(baseBrightness + dimmerDirection * static_cast<int>(kDimmerStep), 0, 255);
  setBrightness(nextBrightness, kRemoteBoardName, "Dimmer step", false);
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

  logMessage(kBoardName, "Heap remained critically low. Restarting ESP_NRF for self-recovery.");
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

void handleNotFound() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

void handleMoveCommand(JsonObject axes, const int feed, const String& source) {
  String gcode = "G1";
  bool hasAxis = false;

  currentFeedRate = constrain(feed, 10, 2000);

  for (JsonPair kv : axes) {
    const String axisName = kv.key().c_str();
    if (axisName.length() != 1) continue;

    const int axisIndex = axisIndexForLabel(axisName[0]);
    if (axisIndex < 0) continue;

    const float value = kv.value().as<float>();
    axisPositions[axisIndex] = value;
    gcode += ' ';
    gcode += axisName;
    gcode += String(value, 0);
    hasAxis = true;
  }

  if (!hasAxis) return;

  gcode += " F";
  gcode += String(currentFeedRate);

  sendToOctopus("G90", source);
  sendToOctopus(gcode, source);
  broadcastState();
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
    setFeedRate(payload["feed"] | currentFeedRate, source, "Feed rate update");
    return true;
  }

  if (action == "home") {
    sendToOctopus("M215 H", source);
    return true;
  }

  if (action == "pos1") {
    sendToOctopus("M215 P1", source);
    return true;
  }

  if (action == "pos2") {
    sendToOctopus("M215 P2", source);
    return true;
  }

  if (action == "random_position") {
    runRandomPosition(source);
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
    if (gcode.length()) sendToOctopus(gcode, source);
    return true;
  }

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

  const String source = doc["source"] | "HTTP";
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

  const String source = doc["source"] | "Browser";
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

  if (type != WStype_TEXT) return;

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    logMessage(kBoardName, "Invalid WebSocket JSON payload.");
    return;
  }

  const String typeName = doc["type"] | "";
  if (typeName == "cmd") {
    const String gcode = doc["gcode"] | "";
    if (gcode.length()) sendToOctopus(gcode, "Web UI");
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
  server.on("/api/state", HTTP_GET, handleApiState);
  server.on("/api/command", HTTP_POST, handleApiCommand);
  server.on("/api/log", HTTP_POST, handleApiLog);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setupWebSocket() {
  webSocket.begin();
  webSocket.onEvent(handleWsEvent);
}

} // namespace

void setup() {
  Serial.begin(115200);
  initStringStorage();
  randomSeed(micros());

  pinMode(kOctopusResetPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(kOctopusResetPin, kOctopusResetActiveLow ? HIGH : LOW);

  octopusSerial.begin(kOctopusBaud, SERIAL_8N1, kOctopusRxPin, kOctopusTxPin);

  WiFi.setHostname(kBoardName);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kApIp, kApGateway, kApSubnet);
  WiFi.softAP(kApSsid, kApPassword);
  dnsServer.start(kDnsPort, "*", WiFi.softAPIP());

  setupHttp();
  setupWebSocket();

  logMessage(kBoardName, String("Access point ready at http://") + WiFi.softAPIP().toString());
  logMessage(kBoardName, "Lamp output delegated to Octopus M355 on the configured bed/heater output.");
  logMessage(kBoardName, String("Octopus reset line ready on GPIO ") + kOctopusResetPin);
  logMessage(kBoardName, String("NRF24 pins CE=") + kNrfCePin + ", CSN=" + kNrfCsnPin + ", SCK=" + kNrfSckPin + ", MISO=" + kNrfMisoPin + ", MOSI=" + kNrfMosiPin);

  initializeRadio();

  delay(300);
  sendToOctopus("M115");
  queryLampState(kBoardName);
  sendToOctopus("M114");
  queryRandomCodes(kBoardName);
  broadcastState();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  webSocket.loop();

  handleRadio();
  handleDimmerLoop();
  readOctopusSerial();
  pollOctopusState();
  pollRandomCodes();
  recoverOctopusLink();
  healthCheck();

  const uint32_t now = millis();
  if (octopusOnline && now - lastOctopusRxMs > kOctopusOfflineMs) {
    octopusOnline = false;
    logMessage(kBoardName, "Octopus serial RX heartbeat timed out.");
    broadcastState();
  }

  if (remoteOnline && now - lastRemoteActivityMs > kRemoteOfflineMs) {
    remoteOnline = false;
    dimmerActive = false;
    logMessage(kBoardName, "NRF remote heartbeat timed out.");
    broadcastState();
  }

  if (!radioReady && now - lastNrfInitMs >= kNrfReinitMs)
    initializeRadio();
}

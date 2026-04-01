#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

#include "web_page.h"

namespace {

constexpr char kBoardName[] = "ESP_Octopus";
constexpr char kRemoteBoardName[] = "ESP_Remote";

constexpr char kApSsid[] = "ESP_Octopus";
constexpr char kApPassword[] = "octopus123";
constexpr byte kDnsPort = 53;
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);
const IPAddress kRemoteIp(192, 168, 4, 2);

constexpr int kOctopusTxPin = 17;
constexpr int kOctopusRxPin = 16;
constexpr uint32_t kOctopusBaud = 115200;

// Drive the six lamps through a transistor / MOSFET. Do not power them directly from the ESP32 pin.
constexpr int kLampPwmPin = 18;
constexpr int kLampPwmChannel = 0;
constexpr uint32_t kLampPwmFrequency = 5000;
constexpr uint8_t kLampPwmResolution = 8;
constexpr bool kLampPwmActiveHigh = true;

constexpr uint32_t kStatePollMs = 500;
constexpr uint32_t kRandomCodePollMs = 15000;
constexpr uint32_t kRemoteOfflineMs = 5000;
constexpr uint32_t kOctopusOfflineMs = 5000;
constexpr size_t kSerialLineMax = 256;
constexpr size_t kMaxRandomCodes = 16;
constexpr size_t kLogHistorySize = 120;

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

float axisPositions[kAxisCount] = { 0, 0, 0, 0, 0, 0 };
uint16_t randomCodes[kMaxRandomCodes] = {};
size_t randomCodeCount = 0;
bool waitingForRandomCodeList = false;

LightState lightState;
bool octopusOnline = false;
bool remoteOnline = false;
int currentFeedRate = 200;

uint32_t lastStatePollMs = 0;
uint32_t lastRandomCodeQueryMs = 0;
uint32_t lastOctopusActivityMs = 0;
uint32_t lastRemoteActivityMs = 0;

String serialLine;
String remoteIpString = kRemoteIp.toString();
String logHistory[kLogHistorySize];
size_t logHistoryStart = 0;
size_t logHistoryCount = 0;

int axisIndexForLabel(const char axis) {
  for (size_t i = 0; i < kAxisCount; ++i) {
    if (kAxes[i] == axis) return static_cast<int>(i);
  }
  return -1;
}

uint8_t clampBrightness(const int value) {
  return static_cast<uint8_t>(constrain(value, 0, 255));
}

bool isKnownRemote(const IPAddress& ip) {
  return ip == kRemoteIp;
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

void applyLampOutput() {
  const uint8_t requestedDuty = lightState.on ? lightState.brightness : 0;
  const uint8_t duty = kLampPwmActiveHigh ? requestedDuty : static_cast<uint8_t>(255 - requestedDuty);
  ledcWrite(kLampPwmChannel, duty);
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
  lights["pwmPin"] = kLampPwmPin;

  JsonArray codes = doc["randomCodes"].to<JsonArray>();
  for (size_t i = 0; i < randomCodeCount; ++i)
    codes.add(randomCodes[i]);
}

void broadcastState() {
  JsonDocument doc;
  fillStateDocument(doc);
  broadcastJson(doc);
}

void markRemoteSeen(const IPAddress& remoteIp, const char* reason) {
  remoteIpString = remoteIp.toString();
  lastRemoteActivityMs = millis();

  if (remoteOnline) return;

  remoteOnline = true;
  logMessage(kBoardName, String("Remote reachable at ") + remoteIpString + (reason ? String(" (") + reason + ")" : ""));
  broadcastState();
}

void sendLogHistoryToClient(const uint8_t clientNum) {
  for (size_t i = 0; i < logHistoryCount; ++i) {
    const size_t index = (logHistoryStart + i) % kLogHistorySize;
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

void sendToOctopus(const String& line, const String& source = kBoardName, const bool logTx = true) {
  if (line.isEmpty()) return;

  if (logTx)
    logMessage(source, String("TX -> Octopus: ") + line);
  octopusSerial.print(line);
  octopusSerial.print('\n');
  lastOctopusActivityMs = millis();
}

void setLightState(const bool on, const uint8_t brightness, const String& source, const String& reason) {
  const bool nextOn = on && brightness > 0;
  const bool changed = nextOn != lightState.on || brightness != lightState.brightness;

  lightState.on = nextOn;
  lightState.brightness = brightness;
  if (brightness > 0)
    lightState.lastNonZeroBrightness = brightness;

  applyLampOutput();

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
    lastOctopusActivityMs = millis();
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

  logMessage("Octopus", line);
  parsePositionLine(line);
  parseRandomCodeLine(line);

  if (line.startsWith("FIRMWARE_NAME:"))
    octopusOnline = true;

  if (line.indexOf("start") >= 0 || line.indexOf("ok") >= 0)
    lastOctopusActivityMs = millis();
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
  const IPAddress remoteIp = server.client().remoteIP();
  if (isKnownRemote(remoteIp))
    markRemoteSeen(remoteIp, "state poll");

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

  const IPAddress remoteIp = server.client().remoteIP();
  String source = doc["source"] | "HTTP";
  if (isKnownRemote(remoteIp)) {
    source = kRemoteBoardName;
    markRemoteSeen(remoteIp, "command");
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

  const IPAddress remoteIp = server.client().remoteIP();
  String source = doc["source"] | "Remote";
  if (isKnownRemote(remoteIp)) {
    source = kRemoteBoardName;
    markRemoteSeen(remoteIp, "log");
  }

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
  serialLine.reserve(kSerialLineMax);
  randomSeed(micros());

  ledcSetup(kLampPwmChannel, kLampPwmFrequency, kLampPwmResolution);
  ledcAttachPin(kLampPwmPin, kLampPwmChannel);
  applyLampOutput();

  octopusSerial.begin(kOctopusBaud, SERIAL_8N1, kOctopusRxPin, kOctopusTxPin);

  WiFi.setHostname(kBoardName);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kApIp, kApGateway, kApSubnet);
  WiFi.softAP(kApSsid, kApPassword);
  dnsServer.start(kDnsPort, "*", WiFi.softAPIP());

  setupHttp();
  setupWebSocket();

  logMessage(kBoardName, String("Access point ready at http://") + WiFi.softAPIP().toString());
  logMessage(kBoardName, String("PWM output ready on GPIO ") + kLampPwmPin);

  delay(300);
  sendToOctopus("M115");
  sendToOctopus("M114");
  queryRandomCodes(kBoardName);
  broadcastState();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  webSocket.loop();

  readOctopusSerial();
  pollOctopusState();
  pollRandomCodes();

  const uint32_t now = millis();
  if (octopusOnline && now - lastOctopusActivityMs > kOctopusOfflineMs) {
    octopusOnline = false;
    logMessage(kBoardName, "Octopus serial heartbeat timed out.");
    broadcastState();
  }

  if (remoteOnline && now - lastRemoteActivityMs > kRemoteOfflineMs) {
    remoteOnline = false;
    logMessage(kBoardName, "Remote heartbeat timed out.");
    broadcastState();
  }
}

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

#include "web_page.h"

namespace {

constexpr char kApSsid[] = "ESP_Octopus";
constexpr char kApPassword[] = "octopus123";
constexpr byte kDnsPort = 53;

constexpr int kOctopusTxPin = 17;
constexpr int kOctopusRxPin = 16;
constexpr uint32_t kOctopusBaud = 115200;

constexpr uint32_t kStatePollMs = 500;
constexpr size_t kSerialLineMax = 256;

constexpr char kAxes[] = { 'X', 'Y', 'Z', 'A', 'B', 'C' };
constexpr size_t kAxisCount = sizeof(kAxes) / sizeof(kAxes[0]);

DNSServer dnsServer;
WebServer server(80);
WebSocketsServer webSocket(81);
HardwareSerial octopusSerial(2);

float axisPositions[kAxisCount] = { 0, 0, 0, 0, 0, 0 };
bool octopusOnline = false;
uint32_t lastStatePollMs = 0;
uint32_t lastOctopusActivityMs = 0;
String serialLine;

int axisIndexForLabel(const char axis) {
  for (size_t i = 0; i < kAxisCount; ++i) {
    if (kAxes[i] == axis) return static_cast<int>(i);
  }
  return -1;
}

void broadcastJson(const JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);
  webSocket.broadcastTXT(payload);
}

void broadcastLog(const String& line) {
  StaticJsonDocument<320> doc;
  doc["type"] = "log";
  doc["line"] = line;
  broadcastJson(doc);
}

void broadcastStatus(const String& message) {
  StaticJsonDocument<256> doc;
  doc["type"] = "status";
  doc["message"] = message;
  broadcastJson(doc);
}

void broadcastState() {
  StaticJsonDocument<256> doc;
  doc["type"] = "state";
  doc["octopusOnline"] = octopusOnline;
  doc["feed"] = 200;
  JsonObject positions = doc["positions"].to<JsonObject>();
  for (size_t i = 0; i < kAxisCount; ++i)
    positions[String(kAxes[i])] = axisPositions[i];
  broadcastJson(doc);
}

void sendToOctopus(const String& line) {
  if (line.isEmpty()) return;
  octopusSerial.print(line);
  octopusSerial.print('\n');
  lastOctopusActivityMs = millis();
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

void handleOctopusLine(const String& rawLine) {
  String line = rawLine;
  line.trim();
  if (!line.length()) return;

  broadcastLog(line);
  parsePositionLine(line);

  if (line.startsWith("FIRMWARE_NAME:"))
    octopusOnline = true;

  if (line.indexOf("start") >= 0 || line.indexOf("ok") >= 0)
    lastOctopusActivityMs = millis();
}

void pollOctopusState() {
  const uint32_t now = millis();
  if (now - lastStatePollMs < kStatePollMs) return;
  lastStatePollMs = now;
  sendToOctopus("M114");
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

void handleWsEvent(const uint8_t clientNum, const WStype_t type, uint8_t* payload, const size_t length) {
  if (type == WStype_CONNECTED) {
    broadcastStatus("Client connected.");
    broadcastState();
    return;
  }

  if (type != WStype_TEXT) return;

  StaticJsonDocument<512> doc;
  const DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    broadcastLog("Invalid WebSocket JSON payload.");
    return;
  }

  const String typeName = doc["type"] | "";
  if (typeName == "cmd") {
    const String gcode = doc["gcode"] | "";
    if (gcode.length()) sendToOctopus(gcode);
    return;
  }

  if (typeName == "move") {
    JsonObject axes = doc["axes"].as<JsonObject>();
    const int feed = doc["feed"] | 200;

    String gcode = "G90\nG1";
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
    }

    gcode += " F";
    gcode += String(feed);
    sendToOctopus(gcode);
    broadcastState();
  }
}

void setupHttp() {
  server.on("/", HTTP_GET, handleRoot);
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

  octopusSerial.begin(kOctopusBaud, SERIAL_8N1, kOctopusRxPin, kOctopusTxPin);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(kApSsid, kApPassword);
  dnsServer.start(kDnsPort, "*", WiFi.softAPIP());

  setupHttp();
  setupWebSocket();

  broadcastStatus("ESP Octopus AP ready.");

  delay(300);
  sendToOctopus("M115");
  sendToOctopus("M114");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  webSocket.loop();

  readOctopusSerial();
  pollOctopusState();

  const uint32_t now = millis();
  if (octopusOnline && now - lastOctopusActivityMs > 5000) {
    octopusOnline = false;
    broadcastState();
  }
}

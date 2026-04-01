#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>

namespace {

constexpr char kBoardName[] = "ESP_Remote";
constexpr char kOctopusBoardName[] = "ESP_Octopus";

constexpr char kWifiSsid[] = "ESP_Octopus";
constexpr char kWifiPassword[] = "octopus123";
const IPAddress kLocalIp(192, 168, 4, 2);
const IPAddress kGatewayIp(192, 168, 4, 1);
const IPAddress kSubnetMask(255, 255, 255, 0);
const IPAddress kDnsIp(192, 168, 4, 1);
const IPAddress kOctopusIp(192, 168, 4, 1);

// All buttons use INPUT_PULLUP and are active-low.
constexpr uint8_t kLightOnPin = 13;
constexpr uint8_t kLightOffPin = 14;
constexpr uint8_t kPos1Pin = 25;
constexpr uint8_t kPos2Pin = 26;
constexpr uint8_t kRandomPin = 27;
constexpr uint8_t kDimmerPin = 32;
constexpr uint8_t kSparePin = 33;

constexpr uint32_t kDebounceMs = 35;
constexpr uint32_t kWifiRetryMs = 5000;
constexpr uint32_t kButtonTriggeredWifiRetryMs = 1000;
constexpr uint32_t kFastStatePollMs = 1500;
constexpr uint32_t kIdleStatePollMs = 12000;
constexpr uint32_t kFastPollingWindowMs = 6000;
constexpr uint32_t kSleepAfterIdleMs = 45000;
constexpr uint32_t kDimmerStepMs = 180;
constexpr uint8_t kDimmerStep = 16;
constexpr uint32_t kHttpTimeoutMs = 1500;
constexpr uint32_t kHttpFailureLogMs = 4000;
constexpr uint32_t kLoopDelayMs = 8;

enum ButtonIndex : size_t {
  kButtonLightOn = 0,
  kButtonLightOff,
  kButtonPos1,
  kButtonPos2,
  kButtonRandom,
  kButtonDimmer,
  kButtonSpare,
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

ButtonState buttons[kButtonCount] = {
  { "LIGHT_ON", kLightOnPin },
  { "LIGHT_OFF", kLightOffPin },
  { "POS1", kPos1Pin },
  { "POS2", kPos2Pin },
  { "RANDOM", kRandomPin },
  { "DIMMER", kDimmerPin },
  { "SPARE", kSparePin },
};

bool startupLogSent = false;
bool dimmerPressed = false;
int8_t dimmerDirection = 1;
bool octopusOnline = false;
bool lightsOn = false;
uint8_t brightness = 160;
uint8_t lastNonZeroBrightness = 160;

uint32_t lastWifiRetryMs = 0;
uint32_t lastStatePollMs = 0;
uint32_t lastDimmerStepMs = 0;
uint32_t lastHttpFailureLogMs = 0;
uint32_t lastUserActivityMs = 0;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;
bool pendingButtonPress[kButtonCount] = { false, false, false, false, false, false, false };

String baseUrl() {
  return String("http://") + kOctopusIp.toString();
}

void logLocal(const String& message) {
  Serial.println("[" + String(kBoardName) + "] " + message);
}

void noteUserActivity() {
  const uint32_t now = millis();
  lastUserActivityMs = now;
}

uint32_t currentStatePollInterval() {
  return millis() - lastUserActivityMs < kFastPollingWindowMs ? kFastStatePollMs : kIdleStatePollMs;
}

void applyStateFromJson(JsonVariantConst payload) {
  octopusOnline = payload["octopusOnline"] | octopusOnline;
  lightsOn = payload["lights"]["on"] | lightsOn;
  brightness = payload["lights"]["brightness"] | brightness;
  lastNonZeroBrightness = payload["lights"]["lastNonZeroBrightness"] | lastNonZeroBrightness;
}

void logHttpFailure(const String& message) {
  const uint32_t now = millis();
  if (now - lastHttpFailureLogMs < kHttpFailureLogMs) return;
  lastHttpFailureLogMs = now;
  logLocal(message);
}

bool postJson(const char* path, const JsonDocument& request, JsonDocument* response) {
  if (WiFi.status() != WL_CONNECTED) {
    logHttpFailure("HTTP request skipped because Wi-Fi is not connected.");
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  const String url = baseUrl() + path;

  if (!http.begin(client, url)) {
    logHttpFailure("HTTP begin failed for " + url);
    return false;
  }

  http.setTimeout(kHttpTimeoutMs);
  http.addHeader("Content-Type", "application/json");

  String body;
  serializeJson(request, body);
  const int httpCode = http.POST(body);
  if (httpCode <= 0) {
    logHttpFailure("POST " + url + " failed: " + http.errorToString(httpCode));
    http.end();
    return false;
  }

  const String responseBody = http.getString();
  http.end();

  if (httpCode < 200 || httpCode >= 300) {
    logHttpFailure("POST " + url + " returned HTTP " + String(httpCode));
    return false;
  }

  if (response && responseBody.length()) {
    const DeserializationError error = deserializeJson(*response, responseBody);
    if (error) {
      logHttpFailure("Failed to parse JSON response from " + url);
      return false;
    }
  }

  return true;
}

bool getJson(const char* path, JsonDocument& response) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;
  const String url = baseUrl() + path;

  if (!http.begin(client, url)) {
    logHttpFailure("HTTP begin failed for " + url);
    return false;
  }

  http.setTimeout(kHttpTimeoutMs);
  const int httpCode = http.GET();
  if (httpCode <= 0) {
    logHttpFailure("GET " + url + " failed: " + http.errorToString(httpCode));
    http.end();
    return false;
  }

  const String responseBody = http.getString();
  http.end();

  if (httpCode < 200 || httpCode >= 300) {
    logHttpFailure("GET " + url + " returned HTTP " + String(httpCode));
    return false;
  }

  const DeserializationError error = deserializeJson(response, responseBody);
  if (error) {
    logHttpFailure("Failed to parse JSON response from " + url);
    return false;
  }

  return true;
}

void sendRemoteLog(const String& message) {
  if (WiFi.status() != WL_CONNECTED) return;

  JsonDocument doc;
  doc["source"] = kBoardName;
  doc["message"] = message;
  postJson("/api/log", doc, nullptr);
}

bool fetchState() {
  JsonDocument doc;
  if (!getJson("/api/state", doc)) return false;

  applyStateFromJson(doc.as<JsonVariantConst>());
  return true;
}

bool sendAction(const String& action, const int brightnessOverride = -1) {
  JsonDocument request;
  request["source"] = kBoardName;
  request["action"] = action;
  if (brightnessOverride >= 0)
    request["brightness"] = brightnessOverride;

  JsonDocument response;
  if (!postJson("/api/command", request, &response)) return false;

  applyStateFromJson(response.as<JsonVariantConst>());
  noteUserActivity();
  return true;
}

void connectWifi() {
  WiFi.disconnect(false, true);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(kBoardName);
  WiFi.config(kLocalIp, kGatewayIp, kSubnetMask, kDnsIp);
  WiFi.begin(kWifiSsid, kWifiPassword);
  lastWifiRetryMs = millis();
}

bool isButtonCurrentlyPressed(const ButtonIndex index) {
  return digitalRead(buttons[index].pin) == LOW;
}

bool isAnyButtonPressed() {
  for (size_t i = 0; i < kButtonCount; ++i) {
    if (isButtonCurrentlyPressed(static_cast<ButtonIndex>(i)))
      return true;
  }
  return false;
}

bool dispatchDiscreteCommand(const ButtonIndex index) {
  bool success = false;

  switch (index) {
    case kButtonLightOn:
      logLocal("LIGHT_ON pressed.");
      success = sendAction("light_on");
      break;
    case kButtonLightOff:
      logLocal("LIGHT_OFF pressed.");
      success = sendAction("light_off");
      break;
    case kButtonPos1:
      logLocal("POS1 pressed.");
      success = sendAction("pos1");
      break;
    case kButtonPos2:
      logLocal("POS2 pressed.");
      success = sendAction("pos2");
      break;
    case kButtonRandom:
      logLocal("RANDOM pressed.");
      success = sendAction("random_position");
      break;
    case kButtonSpare:
      logLocal("SPARE button pressed. No action assigned.");
      sendRemoteLog("Spare button pressed. No action assigned.");
      return true;
    default:
      return false;
  }

  if (!success)
    logHttpFailure("Command failed for button " + String(buttons[index].name));
  return success;
}

void logPinMap() {
  logLocal("Button map:");
  logLocal("  LIGHT_ON  -> GPIO " + String(kLightOnPin));
  logLocal("  LIGHT_OFF -> GPIO " + String(kLightOffPin));
  logLocal("  POS1      -> GPIO " + String(kPos1Pin));
  logLocal("  POS2      -> GPIO " + String(kPos2Pin));
  logLocal("  RANDOM    -> GPIO " + String(kRandomPin));
  logLocal("  DIMMER    -> GPIO " + String(kDimmerPin));
  logLocal("  SPARE     -> GPIO " + String(kSparePin) + " (reserved)");
}

void handleWifiState() {
  const wl_status_t status = WiFi.status();
  if (status != lastWifiStatus) {
    lastWifiStatus = status;

    if (status == WL_CONNECTED) {
      logLocal("Connected to " + String(kOctopusBoardName) + " AP. Local IP=" + WiFi.localIP().toString());
      startupLogSent = false;
      if (fetchState())
        logLocal("Fetched initial state from ESP_Octopus.");
    } else {
      logLocal("Wi-Fi disconnected. Status=" + String(static_cast<int>(status)));
    }
  }

  if (status == WL_CONNECTED) {
    if (!startupLogSent) {
      sendRemoteLog("Remote online. Fixed IP=" + WiFi.localIP().toString());
      startupLogSent = true;
    }
    return;
  }

  const uint32_t now = millis();
  if (now - lastWifiRetryMs < kWifiRetryMs) return;
  logLocal("Retrying Wi-Fi connection to ESP_Octopus...");
  connectWifi();
}

void pollState() {
  if (WiFi.status() != WL_CONNECTED) return;

  const uint32_t now = millis();
  if (now - lastStatePollMs < currentStatePollInterval()) return;
  lastStatePollMs = now;

  if (fetchState()) return;
  logHttpFailure("State poll from ESP_Octopus failed.");
}

bool ensureWifiReadyForButton(const char* buttonName) {
  if (WiFi.status() == WL_CONNECTED) return true;

  const uint32_t now = millis();
  if (now - lastWifiRetryMs >= kButtonTriggeredWifiRetryMs) {
    logLocal(String(buttonName) + " pressed while Wi-Fi is down. Retrying connection now.");
    connectWifi();
  }
  return false;
}

void handleDiscreteCommand(const ButtonIndex index) {
  noteUserActivity();

  if (!ensureWifiReadyForButton(buttons[index].name)) {
    pendingButtonPress[index] = true;
    return;
  }

  pendingButtonPress[index] = false;
  dispatchDiscreteCommand(index);
}

void handleDimmerStep() {
  const uint8_t baseBrightness = lightsOn
    ? brightness
    : static_cast<uint8_t>(lastNonZeroBrightness > 0 ? lastNonZeroBrightness : 160);

  const int nextBrightness = constrain(static_cast<int>(baseBrightness) + dimmerDirection * static_cast<int>(kDimmerStep), 0, 255);
  if (!sendAction("set_brightness", nextBrightness)) {
    logHttpFailure("Dimmer step failed.");
    return;
  }

  brightness = static_cast<uint8_t>(nextBrightness);
  lightsOn = brightness > 0;
  if (brightness > 0)
    lastNonZeroBrightness = brightness;
}

void handleButtonPressed(const ButtonIndex index) {
  if (index == kButtonDimmer) {
    noteUserActivity();

    if (!ensureWifiReadyForButton(buttons[index].name)) {
      pendingButtonPress[index] = true;
      return;
    }

    pendingButtonPress[index] = false;
    dimmerPressed = true;
    lastDimmerStepMs = 0;
    const String direction = dimmerDirection > 0 ? "up" : "down";
    logLocal("DIMMER pressed. Holding will move brightness " + direction + ".");
    sendRemoteLog("Dimmer hold started. Direction=" + direction);
    return;
  }

  handleDiscreteCommand(index);
}

void handleButtonReleased(const ButtonIndex index) {
  noteUserActivity();

  if (index != kButtonDimmer) return;

  pendingButtonPress[index] = false;
  if (!dimmerPressed) return;

  dimmerPressed = false;
  dimmerDirection = -dimmerDirection;
  const String nextDirection = dimmerDirection > 0 ? "up" : "down";
  logLocal("DIMMER released. Next hold will move brightness " + nextDirection + ".");
  sendRemoteLog("Dimmer hold stopped. Next direction=" + nextDirection);
}

void pollButtons() {
  const uint32_t now = millis();

  for (size_t i = 0; i < kButtonCount; ++i) {
    ButtonState& button = buttons[i];
    const bool rawLevel = digitalRead(button.pin);

    if (rawLevel != button.lastRead) {
      button.lastRead = rawLevel;
      button.lastChangeMs = now;
    }

    if (now - button.lastChangeMs < kDebounceMs) continue;
    if (rawLevel == button.stableLevel) continue;

    button.stableLevel = rawLevel;
    if (button.stableLevel == LOW)
      handleButtonPressed(static_cast<ButtonIndex>(i));
    else
      handleButtonReleased(static_cast<ButtonIndex>(i));
  }
}

void handleDimmerHold() {
  if (!dimmerPressed) return;
  if (WiFi.status() != WL_CONNECTED) return;

  const uint32_t now = millis();
  if (lastDimmerStepMs != 0 && now - lastDimmerStepMs < kDimmerStepMs) return;
  lastDimmerStepMs = now;
  handleDimmerStep();
}

void processPendingCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  for (size_t i = 0; i < kButtonCount; ++i) {
    if (!pendingButtonPress[i]) continue;

    const ButtonIndex index = static_cast<ButtonIndex>(i);
    if (index == kButtonDimmer) {
      if (!isButtonCurrentlyPressed(index)) {
        pendingButtonPress[i] = false;
        continue;
      }

      pendingButtonPress[i] = false;
      dimmerPressed = true;
      lastDimmerStepMs = 0;
      const String direction = dimmerDirection > 0 ? "up" : "down";
      logLocal("DIMMER resumed after reconnect. Direction=" + direction);
      sendRemoteLog("Dimmer resumed after reconnect. Direction=" + direction);
      noteUserActivity();
      continue;
    }

    pendingButtonPress[i] = false;
    logLocal(String("Sending queued button action for ") + buttons[i].name + ".");
    dispatchDiscreteCommand(index);
  }
}

void enterLightSleepIfIdle() {
  if (dimmerPressed) return;
  if (isAnyButtonPressed()) return;
  if (millis() - lastUserActivityMs < kSleepAfterIdleMs) return;

  logLocal("Idle timeout reached. Turning Wi-Fi off and entering light sleep.");
  WiFi.disconnect(false, true);
  WiFi.mode(WIFI_OFF);
  lastWifiStatus = WL_DISCONNECTED;

  for (size_t i = 0; i < kButtonCount; ++i)
    gpio_wakeup_enable(static_cast<gpio_num_t>(buttons[i].pin), GPIO_INTR_LOW_LEVEL);

  esp_sleep_enable_gpio_wakeup();
  delay(20);
  esp_light_sleep_start();

  for (size_t i = 0; i < kButtonCount; ++i)
    gpio_wakeup_disable(static_cast<gpio_num_t>(buttons[i].pin));

  logLocal("Woke from light sleep. Reconnecting Wi-Fi.");
  noteUserActivity();
  lastStatePollMs = 0;
  connectWifi();
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  for (const ButtonState& button : buttons)
    pinMode(button.pin, INPUT_PULLUP);

  logPinMap();
  noteUserActivity();
  connectWifi();
}

void loop() {
  handleWifiState();
  pollState();
  pollButtons();
  processPendingCommands();
  handleDimmerHold();
  enterLightSleepIfIdle();
  delay(kLoopDelayMs);
}

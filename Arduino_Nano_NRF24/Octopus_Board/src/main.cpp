#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>

#include <avr/wdt.h>

#include "NrfOctopusProtocol.h"

extern unsigned int __heap_start;
extern void* __brkval;

namespace {

using namespace NrfOctopus;

constexpr uint8_t kRadioCePin = 9;
constexpr uint8_t kRadioCsnPin = 10;
constexpr uint8_t kOctopusResetPin = 4;
constexpr uint8_t kStatusLedPin = 5;

constexpr uint32_t kOctopusBaud = 115200;
constexpr uint32_t kStatePollMs = 1000;
constexpr uint32_t kRandomCodePollMs = 15000;
constexpr uint32_t kOctopusOfflineMs = 5000;
constexpr uint32_t kOctopusRecoveryRetryMs = 3000;
constexpr uint32_t kOctopusSerialReinitMs = 30000;
constexpr uint32_t kOctopusHardwareResetTimeoutMs = 120000;
constexpr uint32_t kOctopusResetCooldownMs = 180000;
constexpr uint32_t kDimmerKeepAliveTimeoutMs = 900;
constexpr uint32_t kDimmerStepMs = 180;
constexpr uint32_t kHealthCheckMs = 60000;
constexpr uint16_t kLowRamThresholdBytes = 180;
constexpr uint8_t kLowRamStrikeLimit = 3;
constexpr uint8_t kDimmerStep = 16;
constexpr uint8_t kMaxRandomCodes = 16;
constexpr uint8_t kSerialLineMax = 95;
constexpr uint32_t kRadioReinitMs = 2000;

RF24 radio(kRadioCePin, kRadioCsnPin);

bool radioReady = false;
bool octopusOnline = false;
bool waitingForRandomCodeList = false;
bool dimmerActive = false;
bool lightsOn = false;
int8_t dimmerDirection = 1;
uint8_t brightness = 160;
uint8_t lastNonZeroBrightness = 160;
uint8_t randomCodeCount = 0;
uint8_t activeRandomCode = 0;
uint16_t randomCodes[kMaxRandomCodes] = {};

uint8_t lastSessionId = 0;
uint16_t lastSequence = 0;
uint8_t lowRamStrikeCount = 0;

uint32_t lastStatePollMs = 0;
uint32_t lastRandomCodeQueryMs = 0;
uint32_t lastOctopusRxMs = 0;
uint32_t lastOctopusRecoveryMs = 0;
uint32_t lastOctopusSerialReinitMs = 0;
uint32_t lastOctopusHardwareResetMs = 0;
uint32_t lastDimmerKeepAliveMs = 0;
uint32_t lastDimmerStepMs = 0;
uint32_t lastHealthCheckMs = 0;
uint32_t lastRadioInitMs = 0;

char serialLine[kSerialLineMax + 1] = {};
uint8_t serialLineLength = 0;

void sendToOctopus(const __FlashStringHelper* line);
void sendToOctopus(const char* line);

inline void feedWatchdog() {
  wdt_reset();
}

int freeRam() {
  int v;
  return reinterpret_cast<int>(&v) - (reinterpret_cast<int>(__brkval == nullptr ? &__heap_start : __brkval));
}

void forceReset() {
  cli();
  wdt_enable(WDTO_15MS);
  for (;;) {}
}

void pulseStatusLed(const uint8_t count, const uint16_t onMs, const uint16_t offMs) {
  for (uint8_t i = 0; i < count; ++i) {
    digitalWrite(kStatusLedPin, HIGH);
    delay(onMs);
    digitalWrite(kStatusLedPin, LOW);
    if (i + 1 < count)
      delay(offMs);
  }
}

bool initializeRadio() {
  const uint32_t now = millis();
  if (now - lastRadioInitMs < kRadioReinitMs)
    return radioReady;

  lastRadioInitMs = now;
  radioReady = radio.begin();
  if (!radioReady)
    return false;

  radio.setChannel(108);
  radio.setDataRate(RF24_250KBPS);
  radio.setPALevel(RF24_PA_LOW);
  radio.setRetries(5, 15);
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(true);
  radio.setPayloadSize(sizeof(CommandPacket));
  radio.openReadingPipe(1, kRadioAddress);
  radio.startListening();
  return true;
}

bool ensureRadioReady() {
  if (radioReady && radio.isChipConnected())
    return true;

  radioReady = false;
  return initializeRadio();
}

void releaseOctopusResetLine() {
  digitalWrite(kOctopusResetPin, LOW);
  pinMode(kOctopusResetPin, INPUT);
}

void assertOctopusResetLine() {
  pinMode(kOctopusResetPin, OUTPUT);
  digitalWrite(kOctopusResetPin, LOW);
}

void pulseOctopusResetLine() {
  assertOctopusResetLine();
  delay(250);
  releaseOctopusResetLine();
}

void sendLampStateToOctopus() {
  char command[20];
  if (!lightsOn || brightness == 0) {
    strcpy(command, "M355 S0");
  }
  else {
    snprintf(command, sizeof(command), "M355 P%u S1", brightness);
  }
  sendToOctopus(command);
}

void queryLampState() {
  sendToOctopus(F("M355"));
}

void setLightState(const bool on, const uint8_t nextBrightness, const bool syncToOctopus = true) {
  brightness = nextBrightness;
  lightsOn = on && nextBrightness > 0;
  if (nextBrightness > 0)
    lastNonZeroBrightness = nextBrightness;
  if (syncToOctopus)
    sendLampStateToOctopus();
}

void turnLightsOn() {
  const uint8_t restored = brightness > 0 ? brightness : (lastNonZeroBrightness > 0 ? lastNonZeroBrightness : 160);
  setLightState(true, restored);
}

void turnLightsOff() {
  setLightState(false, brightness);
}

void setBrightness(const uint8_t nextBrightness) {
  setLightState(nextBrightness > 0, nextBrightness);
}

void clearRandomCodes() {
  randomCodeCount = 0;
  activeRandomCode = 0;
}

void addRandomCode(const uint16_t code) {
  if (randomCodeCount >= kMaxRandomCodes)
    return;

  for (uint8_t i = 0; i < randomCodeCount; ++i) {
    if (randomCodes[i] == code)
      return;
  }

  randomCodes[randomCodeCount++] = code;
}

void sendToOctopus(const __FlashStringHelper* line) {
  Serial.println(line);
}

void sendToOctopus(const char* line) {
  Serial.println(line);
}

void queryRandomCodes() {
  lastRandomCodeQueryMs = millis();
  waitingForRandomCodeList = true;
  clearRandomCodes();
  sendToOctopus(F("M215"));
}

void runRandomPosition() {
  if (randomCodeCount == 0) {
    queryRandomCodes();
    return;
  }

  const uint8_t index = static_cast<uint8_t>(random(randomCodeCount));
  activeRandomCode = static_cast<uint8_t>(randomCodes[index]);
  char command[16];
  snprintf(command, sizeof(command), "M215 S%u", randomCodes[index]);
  sendToOctopus(command);
}

void parseRandomCodeLine(const char* line) {
  if (strcmp(line, "Spider SD file codes:") == 0) {
    waitingForRandomCodeList = true;
    clearRandomCodes();
    return;
  }

  const char* marker = strstr(line, "M215 S");
  if (marker != nullptr) {
    const uint16_t code = static_cast<uint16_t>(atoi(marker + 6));
    if (code > 0)
      addRandomCode(code);
    return;
  }

  if (waitingForRandomCodeList && strcmp(line, "ok") == 0)
    waitingForRandomCodeList = false;
}

void parseLampStateLine(const char* line) {
  if (strncmp(line, "Case light:", 11) != 0)
    return;

  const char* value = line + 11;
  while (*value == ' ') ++value;

  if (strcmp(value, "off") == 0 || strcmp(value, "OFF") == 0) {
    setLightState(false, brightness, false);
    return;
  }

  if (strcmp(value, "on") == 0 || strcmp(value, "ON") == 0) {
    const uint8_t restored = lastNonZeroBrightness > 0 ? lastNonZeroBrightness : 160;
    setLightState(true, restored, false);
    return;
  }

  const int reportedBrightness = atoi(value);
  if (reportedBrightness < 0 || reportedBrightness > 255)
    return;

  setLightState(true, static_cast<uint8_t>(reportedBrightness), false);
}

void handleOctopusLine(const char* line) {
  if (!line[0])
    return;

  lastOctopusRxMs = millis();
  octopusOnline = true;

  parseLampStateLine(line);

  if (strncmp(line, "FIRMWARE_NAME:", 14) == 0) {
    octopusOnline = true;
    queryLampState();
  }

  parseRandomCodeLine(line);
}

void readOctopusSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r')
      continue;

    if (c == '\n') {
      serialLine[serialLineLength] = '\0';
      handleOctopusLine(serialLine);
      serialLineLength = 0;
      continue;
    }

    if (serialLineLength < kSerialLineMax)
      serialLine[serialLineLength++] = c;
  }
}

void pollOctopusState() {
  const uint32_t now = millis();
  if (now - lastStatePollMs < kStatePollMs)
    return;

  lastStatePollMs = now;
  sendToOctopus(F("M114"));
}

void pollRandomCodes() {
  const uint32_t now = millis();
  if (randomCodeCount != 0 || now - lastRandomCodeQueryMs < kRandomCodePollMs)
    return;

  queryRandomCodes();
}

void stagedOctopusRecovery() {
  const uint32_t now = millis();

  if (!octopusOnline && now - lastOctopusRecoveryMs >= kOctopusRecoveryRetryMs) {
    lastOctopusRecoveryMs = now;
    sendToOctopus(F("M115"));
  }

  if (now - lastOctopusRxMs >= kOctopusSerialReinitMs && now - lastOctopusSerialReinitMs >= kOctopusSerialReinitMs) {
    lastOctopusSerialReinitMs = now;
    Serial.end();
    delay(20);
    Serial.begin(kOctopusBaud);
    sendToOctopus(F("M115"));
  }

  if (now - lastOctopusRxMs >= kOctopusHardwareResetTimeoutMs && now - lastOctopusHardwareResetMs >= kOctopusResetCooldownMs) {
    lastOctopusHardwareResetMs = now;
    pulseOctopusResetLine();
    Serial.end();
    delay(50);
    Serial.begin(kOctopusBaud);
    sendToOctopus(F("M115"));
  }
}

void handleDimmerLoop() {
  if (!dimmerActive)
    return;

  const uint32_t now = millis();
  if (now - lastDimmerKeepAliveMs > kDimmerKeepAliveTimeoutMs) {
    dimmerActive = false;
    dimmerDirection = -dimmerDirection;
    return;
  }

  if (lastDimmerStepMs != 0 && now - lastDimmerStepMs < kDimmerStepMs)
    return;

  lastDimmerStepMs = now;
  const int next = constrain(static_cast<int>(lightsOn ? brightness : (lastNonZeroBrightness > 0 ? lastNonZeroBrightness : 160)) + dimmerDirection * static_cast<int>(kDimmerStep), 0, 255);
  setBrightness(static_cast<uint8_t>(next));
}

void handleCommand(const CommandPacket& packet) {
  if (!isProtocolPacketValid(packet))
    return;

  if (packet.sessionId == lastSessionId && packet.sequence == lastSequence)
    return;

  lastSessionId = packet.sessionId;
  lastSequence = packet.sequence;

  switch (static_cast<CommandType>(packet.command)) {
    case CMD_LIGHT_ON:
      turnLightsOn();
      break;
    case CMD_LIGHT_OFF:
      turnLightsOff();
      break;
    case CMD_POS1:
      sendToOctopus(F("M215 P1"));
      break;
    case CMD_POS2:
      sendToOctopus(F("M215 P2"));
      break;
    case CMD_RANDOM:
      runRandomPosition();
      break;
    case CMD_DIMMER_START:
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
      }
      break;
    case CMD_STATE_REQUEST:
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
    handleCommand(packet);
    pulseStatusLed(1, 6, 0);
  }
}

void healthCheck() {
  const uint32_t now = millis();
  if (now - lastHealthCheckMs < kHealthCheckMs)
    return;

  lastHealthCheckMs = now;
  const int ram = freeRam();
  if (ram >= static_cast<int>(kLowRamThresholdBytes)) {
    lowRamStrikeCount = 0;
    return;
  }

  ++lowRamStrikeCount;
  if (lowRamStrikeCount >= kLowRamStrikeLimit)
    forceReset();
}

void updateOnlineState() {
  if (octopusOnline && millis() - lastOctopusRxMs > kOctopusOfflineMs)
    octopusOnline = false;
}

void initPins() {
  pinMode(kStatusLedPin, OUTPUT);
  digitalWrite(kStatusLedPin, LOW);
  releaseOctopusResetLine();
}

} // namespace

void setup() {
  MCUSR = 0;
  wdt_disable();
  initPins();

  Serial.begin(kOctopusBaud);
  initializeRadio();

  randomSeed(
#ifdef A6
    analogRead(A6) ^
#endif
    micros()
  );

  lastOctopusRxMs = millis();
  queryRandomCodes();
  sendToOctopus(F("M115"));
  queryLampState();

  wdt_enable(WDTO_8S);
}

void loop() {
  feedWatchdog();
  handleRadio();
  readOctopusSerial();
  pollOctopusState();
  pollRandomCodes();
  handleDimmerLoop();
  updateOnlineState();
  stagedOctopusRecovery();
  healthCheck();
}

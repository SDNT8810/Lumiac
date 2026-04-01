#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>

#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#include "NrfOctopusProtocol.h"

extern unsigned int __heap_start;
extern void* __brkval;

namespace {

using namespace NrfOctopus;

constexpr uint8_t kRadioCePin = 9;
constexpr uint8_t kRadioCsnPin = 10;
constexpr uint8_t kStatusLedPin = 8;
constexpr bool kUseStatusLed = false;

constexpr uint8_t kLightOnPin = A0;
constexpr uint8_t kLightOffPin = A1;
constexpr uint8_t kPos1Pin = A2;
constexpr uint8_t kPos2Pin = A3;
constexpr uint8_t kRandomPin = A4;
constexpr uint8_t kDimmerPin = A5;
constexpr uint8_t kSparePin = 2;

constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kStayAwakeAfterActivityMs = 5000;
constexpr uint32_t kDimmerKeepAliveMs = 250;
constexpr uint32_t kTxRetryMs = 300;
constexpr uint32_t kTxTimeoutMs = 5000;
constexpr uint32_t kRadioReinitMs = 2000;
constexpr uint32_t kHealthCheckMs = 60000;
constexpr uint16_t kLowRamThresholdBytes = 180;
constexpr uint8_t kLowRamStrikeLimit = 3;

enum ButtonIndex : uint8_t {
  BUTTON_LIGHT_ON = 0,
  BUTTON_LIGHT_OFF,
  BUTTON_POS1,
  BUTTON_POS2,
  BUTTON_RANDOM,
  BUTTON_DIMMER,
  BUTTON_SPARE,
  BUTTON_COUNT
};

struct ButtonState {
  uint8_t pin;
  bool stablePressed;
  bool lastReadPressed;
  bool wakePending;
  uint32_t lastChangeMs;
};

struct PendingTx {
  bool active;
  uint8_t attempts;
  uint32_t firstAttemptMs;
  uint32_t lastAttemptMs;
  CommandPacket packet;
};

RF24 radio(kRadioCePin, kRadioCsnPin);

ButtonState buttons[BUTTON_COUNT] = {
  { kLightOnPin, false, false, false, 0 },
  { kLightOffPin, false, false, false, 0 },
  { kPos1Pin, false, false, false, 0 },
  { kPos2Pin, false, false, false, 0 },
  { kRandomPin, false, false, false, 0 },
  { kDimmerPin, false, false, false, 0 },
  { kSparePin, false, false, false, 0 },
};

volatile bool wakeInterruptObserved = false;
volatile uint8_t wakeSnapshotPortC = 0xFF;
volatile uint8_t wakeSnapshotPortD = 0xFF;

bool radioReady = false;
bool dimmerActive = false;
uint8_t sessionId = 0;
uint16_t nextSequence = 1;
uint8_t lowRamStrikeCount = 0;

uint32_t lastUserActivityMs = 0;
uint32_t lastDimmerKeepAliveMs = 0;
uint32_t lastRadioInitMs = 0;
uint32_t lastHealthCheckMs = 0;

PendingTx pendingTx = {};

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

bool isButtonPressedRaw(const ButtonIndex index) {
  return digitalRead(buttons[index].pin) == LOW;
}

void noteUserActivity() {
  lastUserActivityMs = millis();
}

void pulseStatusLed(const uint8_t count, const uint16_t onMs, const uint16_t offMs) {
  if (!kUseStatusLed)
    return;

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
  radio.openWritingPipe(kRadioAddress);
  radio.stopListening();
  radio.powerUp();
  delay(5);
  return true;
}

void prepareForDeepSleep() {
  radio.powerDown();
  radioReady = false;

  ADCSRA &= ~_BV(ADEN);
  power_adc_disable();
  power_spi_disable();
  power_timer0_disable();
  power_timer1_disable();
  power_timer2_disable();
  power_twi_disable();
  power_usart0_disable();
}

void restoreAfterDeepSleep() {
  power_all_enable();
  ADCSRA |= _BV(ADEN);
}

bool ensureRadioReady() {
  if (radioReady && radio.isChipConnected())
    return true;

  radioReady = false;
  return initializeRadio();
}

void queuePacket(const CommandType command, const uint8_t value = 0) {
  fillCommandPacket(pendingTx.packet, sessionId, nextSequence++, command, value);
  pendingTx.active = true;
  pendingTx.attempts = 0;
  pendingTx.firstAttemptMs = 0;
  pendingTx.lastAttemptMs = 0;
}

bool transmitNow(const CommandPacket& packet) {
  if (!ensureRadioReady())
    return false;

  const bool sent = radio.write(&packet, sizeof(packet));
  if (!sent)
    radioReady = false;
  return sent;
}

bool sendQueuedPacket() {
  if (!pendingTx.active)
    return true;

  const uint32_t now = millis();
  if (pendingTx.lastAttemptMs != 0 && now - pendingTx.lastAttemptMs < kTxRetryMs)
    return false;

  pendingTx.lastAttemptMs = now;
  if (pendingTx.firstAttemptMs == 0)
    pendingTx.firstAttemptMs = now;
  ++pendingTx.attempts;

  if (transmitNow(pendingTx.packet)) {
    pendingTx.active = false;
    pulseStatusLed(1, 8, 0);
    return true;
  }

  if (now - pendingTx.firstAttemptMs > kTxTimeoutMs) {
    pendingTx.active = false;
    pulseStatusLed(3, 30, 40);
  }

  return false;
}

void sendImmediateCommand(const CommandType command, const uint8_t value = 0) {
  noteUserActivity();
  queuePacket(command, value);
  sendQueuedPacket();
}

void startDimmer() {
  if (dimmerActive)
    return;

  noteUserActivity();
  dimmerActive = true;
  lastDimmerKeepAliveMs = 0;
  sendImmediateCommand(CMD_DIMMER_START);
}

void stopDimmer() {
  if (!dimmerActive)
    return;

  noteUserActivity();
  dimmerActive = false;
  sendImmediateCommand(CMD_DIMMER_STOP);
}

void handleDiscreteButton(const ButtonIndex index) {
  switch (index) {
    case BUTTON_LIGHT_ON: sendImmediateCommand(CMD_LIGHT_ON); break;
    case BUTTON_LIGHT_OFF: sendImmediateCommand(CMD_LIGHT_OFF); break;
    case BUTTON_POS1: sendImmediateCommand(CMD_POS1); break;
    case BUTTON_POS2: sendImmediateCommand(CMD_POS2); break;
    case BUTTON_RANDOM: sendImmediateCommand(CMD_RANDOM); break;
    case BUTTON_SPARE: break;
    default: break;
  }
}

void handleButtonPressed(const ButtonIndex index) {
  if (index == BUTTON_DIMMER) {
    startDimmer();
    return;
  }

  handleDiscreteButton(index);
}

void handleButtonReleased(const ButtonIndex index) {
  noteUserActivity();
  if (index == BUTTON_DIMMER)
    stopDimmer();
}

bool buttonPressedInWakeSnapshot(const ButtonIndex index, const uint8_t snapshotC, const uint8_t snapshotD) {
  switch (buttons[index].pin) {
    case A0: return !(snapshotC & _BV(PC0));
    case A1: return !(snapshotC & _BV(PC1));
    case A2: return !(snapshotC & _BV(PC2));
    case A3: return !(snapshotC & _BV(PC3));
    case A4: return !(snapshotC & _BV(PC4));
    case A5: return !(snapshotC & _BV(PC5));
    case 2:  return !(snapshotD & _BV(PD2));
    default: return false;
  }
}

void processWakeSnapshot() {
  uint8_t snapshotC = 0xFF;
  uint8_t snapshotD = 0xFF;
  bool observed = false;

  noInterrupts();
  observed = wakeInterruptObserved;
  if (observed) {
    snapshotC = wakeSnapshotPortC;
    snapshotD = wakeSnapshotPortD;
    wakeInterruptObserved = false;
  }
  interrupts();

  if (!observed)
    return;

  noteUserActivity();
  for (uint8_t i = 0; i < BUTTON_COUNT; ++i)
    buttons[i].wakePending = buttonPressedInWakeSnapshot(static_cast<ButtonIndex>(i), snapshotC, snapshotD);
}

void processWakePendingButtons() {
  for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
    if (!buttons[i].wakePending)
      continue;

    buttons[i].wakePending = false;
    handleButtonPressed(static_cast<ButtonIndex>(i));
  }
}

void pollButtons() {
  const uint32_t now = millis();

  for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
    const bool pressed = isButtonPressedRaw(static_cast<ButtonIndex>(i));
    ButtonState& button = buttons[i];

    if (pressed != button.lastReadPressed) {
      button.lastReadPressed = pressed;
      button.lastChangeMs = now;
    }

    if (now - button.lastChangeMs < kDebounceMs)
      continue;
    if (pressed == button.stablePressed)
      continue;

    button.stablePressed = pressed;
    if (pressed)
      handleButtonPressed(static_cast<ButtonIndex>(i));
    else
      handleButtonReleased(static_cast<ButtonIndex>(i));
  }
}

void handleDimmerKeepAlive() {
  if (!dimmerActive)
    return;

  if (!buttons[BUTTON_DIMMER].stablePressed && !isButtonPressedRaw(BUTTON_DIMMER)) {
    stopDimmer();
    return;
  }

  const uint32_t now = millis();
  if (lastDimmerKeepAliveMs != 0 && now - lastDimmerKeepAliveMs < kDimmerKeepAliveMs)
    return;

  lastDimmerKeepAliveMs = now;

  CommandPacket packet;
  fillCommandPacket(packet, sessionId, nextSequence++, CMD_DIMMER_KEEPALIVE, 0);
  transmitNow(packet);
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

void enableWakeInterrupts() {
  PCICR |= _BV(PCIE1) | _BV(PCIE2);
  PCMSK1 |= _BV(PCINT8) | _BV(PCINT9) | _BV(PCINT10) | _BV(PCINT11) | _BV(PCINT12) | _BV(PCINT13);
  PCMSK2 |= _BV(PCINT18);
}

void enterDeepSleepIfIdle() {
  if (pendingTx.active || dimmerActive)
    return;
  if (millis() - lastUserActivityMs < kStayAwakeAfterActivityMs)
    return;

  for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
    if (isButtonPressedRaw(static_cast<ButtonIndex>(i)))
      return;
  }

  prepareForDeepSleep();
  wdt_disable();

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();

  noInterrupts();
  PCIFR |= _BV(PCIF1) | _BV(PCIF2);
  sleep_bod_disable();
  interrupts();
  sleep_cpu();

  sleep_disable();
  restoreAfterDeepSleep();
  wdt_enable(WDTO_8S);
  noteUserActivity();
  initializeRadio();
}

void initButtonState() {
  for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    const bool pressed = isButtonPressedRaw(static_cast<ButtonIndex>(i));
    buttons[i].stablePressed = pressed;
    buttons[i].lastReadPressed = pressed;
    buttons[i].wakePending = false;
    buttons[i].lastChangeMs = 0;
  }
}

void initStatusLed() {
  pinMode(kStatusLedPin, OUTPUT);
  digitalWrite(kStatusLedPin, LOW);
}

} // namespace

ISR(PCINT1_vect) {
  wakeSnapshotPortC = PINC;
  wakeSnapshotPortD = PIND;
  wakeInterruptObserved = true;
}

ISR(PCINT2_vect) {
  wakeSnapshotPortC = PINC;
  wakeSnapshotPortD = PIND;
  wakeInterruptObserved = true;
}

void setup() {
  MCUSR = 0;
  wdt_disable();

  initStatusLed();
  initButtonState();
  enableWakeInterrupts();

  sessionId = static_cast<uint8_t>(micros());
  if (sessionId == 0)
    sessionId = 1;

  initializeRadio();
  noteUserActivity();
  wdt_enable(WDTO_8S);
}

void loop() {
  feedWatchdog();
  processWakeSnapshot();
  processWakePendingButtons();
  pollButtons();
  sendQueuedPacket();
  handleDimmerKeepAlive();
  healthCheck();
  enterDeepSleepIfIdle();
}

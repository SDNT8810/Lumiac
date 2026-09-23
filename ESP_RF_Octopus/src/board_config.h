#pragma once

#include <Arduino.h>

// GPIO numbers, not connector positions. See Docs/esp-controllers.md for wiring.
namespace board {

constexpr uint8_t kUnusedPin = 0xFF;

#if defined(ESP8266)
constexpr char kName[] = "ESP12S_Octopus";
constexpr int kOctopusTxPin = 1;
constexpr int kOctopusRxPin = 3;
constexpr int kOctopusResetPin = 2; // Boot strap: must remain HIGH during ESP boot.
constexpr uint8_t kLightOnPin = 14;
constexpr uint8_t kLightOffPin = 5;
constexpr uint8_t kPos1Pin = 4;
constexpr uint8_t kPos2Pin = 12;
constexpr uint8_t kRandomPin = 13;
constexpr uint8_t kDimmerPin = 16; // Polling required: GPIO16 has no GPIO interrupt.
constexpr uint8_t kPos3Pin = 15;   // Boot strap: RF output must be LOW during boot.
constexpr uint8_t kReservePin = kUnusedPin;
constexpr uint8_t kApMaxConnections = 4;
constexpr size_t kLogHistorySize = 32;
constexpr size_t kLogReplayLimit = 12;
constexpr uint32_t kLowHeapThresholdBytes = 8000;
#elif defined(ESP32)
constexpr char kName[] = "ESP_RF_Octopus";
constexpr int kOctopusTxPin = 17;
constexpr int kOctopusRxPin = 16;
constexpr int kOctopusResetPin = 23;
constexpr uint8_t kLightOnPin = 14;
constexpr uint8_t kLightOffPin = 32;
constexpr uint8_t kPos1Pin = 25;
constexpr uint8_t kPos2Pin = 33;
constexpr uint8_t kRandomPin = 26;
constexpr uint8_t kDimmerPin = 13;
constexpr uint8_t kPos3Pin = 27;
constexpr uint8_t kReservePin = 21;
constexpr uint8_t kApMaxConnections = 8;
constexpr size_t kLogHistorySize = 120;
constexpr size_t kLogReplayLimit = 30;
constexpr uint32_t kLowHeapThresholdBytes = 30000;
#else
  #error "Lumiac controller requires an ESP32 or ESP8266 target."
#endif

} // namespace board

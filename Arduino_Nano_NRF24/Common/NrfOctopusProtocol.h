#pragma once

#include <Arduino.h>

namespace NrfOctopus {

constexpr uint8_t kProtocolVersion = 1;
constexpr uint64_t kRadioAddress = 0xE8E8F0F0E1ULL;

enum CommandType : uint8_t {
  CMD_NONE = 0,
  CMD_LIGHT_ON = 1,
  CMD_LIGHT_OFF = 2,
  CMD_POS1 = 3,
  CMD_POS2 = 4,
  CMD_RANDOM = 5,
  CMD_DIMMER_START = 6,
  CMD_DIMMER_KEEPALIVE = 7,
  CMD_DIMMER_STOP = 8,
  CMD_STATE_REQUEST = 9
};

enum StateFlags : uint8_t {
  STATE_OCTOPUS_ONLINE = 0x01,
  STATE_LIGHTS_ON = 0x02,
  STATE_DIMMER_ACTIVE = 0x04,
  STATE_RANDOM_CODES_READY = 0x08
};

struct __attribute__((packed)) CommandPacket {
  uint8_t version;
  uint8_t sessionId;
  uint16_t sequence;
  uint8_t command;
  uint8_t value;
  uint8_t reserved0;
  uint8_t reserved1;
};

struct __attribute__((packed)) StatePacket {
  uint8_t version;
  uint8_t flags;
  uint8_t brightness;
  uint8_t lastNonZeroBrightness;
  uint8_t randomCodeCount;
  uint8_t activeRandomCode;
  uint8_t lastSessionId;
  uint16_t lastSequence;
};

static_assert(sizeof(CommandPacket) == 8, "CommandPacket size mismatch");
static_assert(sizeof(StatePacket) == 9, "StatePacket size mismatch");

inline bool isProtocolPacketValid(const CommandPacket& packet) {
  return packet.version == kProtocolVersion;
}

inline void fillCommandPacket(
  CommandPacket& packet,
  const uint8_t sessionId,
  const uint16_t sequence,
  const CommandType command,
  const uint8_t value = 0
) {
  packet.version = kProtocolVersion;
  packet.sessionId = sessionId;
  packet.sequence = sequence;
  packet.command = static_cast<uint8_t>(command);
  packet.value = value;
  packet.reserved0 = 0;
  packet.reserved1 = 0;
}

} // namespace NrfOctopus

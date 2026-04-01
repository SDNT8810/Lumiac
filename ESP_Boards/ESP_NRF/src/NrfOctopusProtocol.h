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

struct __attribute__((packed)) CommandPacket {
  uint8_t version;
  uint8_t sessionId;
  uint16_t sequence;
  uint8_t command;
  uint8_t value;
  uint8_t reserved0;
  uint8_t reserved1;
};

static_assert(sizeof(CommandPacket) == 8, "CommandPacket size mismatch");

inline bool isProtocolPacketValid(const CommandPacket& packet) {
  return packet.version == kProtocolVersion;
}

} // namespace NrfOctopus

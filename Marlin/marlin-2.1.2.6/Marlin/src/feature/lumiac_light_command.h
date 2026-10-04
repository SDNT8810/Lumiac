#pragma once
#include <stdint.h>

// State is carried by each UART's EmergencyParser, so interleaved ports cannot
// combine digits. ;L000..;L255 are absolute PWM commands, with ;L000 meaning off.
// The comment prefix prevents these from filling / replaying the G-code queue.
namespace lumiac {
constexpr uint16_t light_start = 0x100;
inline int consume_light(uint16_t& state, uint8_t c) {
  const unsigned stage = state >> 8, value = state & 0xff;
  if (c == '\r' || c == '\n') {
    state = 0;
    return stage == 4 ? int(value) : -1;
  }
  if (stage >= 1 && stage <= 3 && c >= '0' && c <= '9') {
    const unsigned next = value * 10 + c - '0';
    state = next <= 255 ? uint16_t(((stage + 1) << 8) | next) : 0x500;
  } else state = 0x500; // Invalid input: ignore the rest of this line.
  return -1;
}
}

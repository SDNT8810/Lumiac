#pragma once
#include <stdint.h>

namespace lumiac {
// ;F000 = standby, ;F001 = normal, ;F002 = random. Actual motion is local.
enum FanMode : uint8_t { FanStandby = 0, FanNormal = 1, FanRandom = 2 };
constexpr uint16_t fan_command_tag = 0x8000;
constexpr uint8_t motion_fan_pwm(uint8_t mode, bool moving, bool paused) {
  return mode == FanStandby ? 0 : !moving || paused ? 77 : mode == FanRandom ? 255 : 204;
}
} // namespace lumiac

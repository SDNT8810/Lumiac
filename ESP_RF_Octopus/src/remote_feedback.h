#pragma once
#include <stdint.h>

namespace remote_control {
// Visual feedback only; command repeat and standby timings stay in Gestures.
class KeyFeedback {
  uint8_t down_ = 0, held_ = 0;
  uint32_t started_[8] = {};
public:
  uint8_t down() const { return down_; }
  uint8_t held() const { return held_; }

  template<class Emit> void update(uint8_t down, uint32_t now, Emit emit) {
    uint8_t held = 0;
    for (unsigned key = 0; key < 8; ++key) {
      const uint8_t bit = 1u << key;
      if (!(down & bit)) continue;
      if (!(down_ & bit)) started_[key] = now;
      if (now - started_[key] >= 400) held |= bit;
    }
    if (down == down_ && held == held_) return;
    down_ = down;
    held_ = held;
    emit(down_, held_);
  }
};
} // namespace remote_control

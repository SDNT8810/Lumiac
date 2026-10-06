#pragma once
#include <stdint.h>

namespace remote_control {
enum class Event { Brighter, Dimmer, Next, Previous, PlayDown, PlayTap, PlayDoubleTap, Standby };
constexpr uint32_t kBrightnessHoldMs = 400;
constexpr uint32_t kBrightnessRepeatHz = 15; // 2 percentage points per step = 30%/s.
constexpr uint32_t kStandbyHoldMs = 2500;
constexpr uint32_t kPlayDoubleClickMs = 600;

// Hardware-independent timing. Repeated HID down reports never become extra taps.
class Gestures {
  uint8_t down_ = 0, blocked_ = 0;
  uint32_t started_[5] = {}, repeated_[2] = {};
  uint32_t playReleasedAt_ = 0;
  bool standbySent_ = false, pendingPlayTap_ = false;
public:
  void cancel() { down_ = blocked_ = 0; standbySent_ = pendingPlayTap_ = false; }
  uint8_t down() const { return down_ & ~blocked_; }

  template<class Emit> void tick(uint32_t now, Emit emit) {
    // Pause stays on button-down. Defer a single release so double-clicking a
    // paused POS cannot briefly resume its old path before starting random.
    if (pendingPlayTap_ && !(down_ & 16) && now - playReleasedAt_ > kPlayDoubleClickMs) {
      pendingPlayTap_ = false;
      emit(Event::PlayTap);
    }
    for (unsigned key = 0; key < 5; ++key) {
      const uint8_t bit = 1u << key;
      if (!(down_ & bit) || (blocked_ & bit)) continue;
      const uint32_t elapsed = now - started_[key];
      if (elapsed >= 15000) {
        blocked_ |= bit;
        if (key == 4) pendingPlayTap_ = false;
        continue;
      }
      if (key < 2 && elapsed >= kBrightnessHoldMs) {
        const uint32_t steps = (elapsed - kBrightnessHoldMs) * kBrightnessRepeatHz / 1000 + 1;
        // At most one second of catch-up after a stalled loop; no delayed runaway.
        const uint32_t count = steps - repeated_[key];
        repeated_[key] = steps;
        for (uint32_t i = 0; i < count && i < kBrightnessRepeatHz; ++i)
          emit(key == 0 ? Event::Brighter : Event::Dimmer);
      }
      if (key == 4 && elapsed >= kStandbyHoldMs && !standbySent_) {
        standbySent_ = true;
        pendingPlayTap_ = false;
        emit(Event::Standby);
      }
    }
  }

  template<class Emit> void update(uint8_t down, uint32_t now, Emit emit) {
    // A different transport command supersedes a pending single Play click.
    if (down & ~down_ & 0x0c) pendingPlayTap_ = false;
    tick(now, emit);
    for (unsigned key = 0; key < 5; ++key) {
      const uint8_t bit = 1u << key;
      if ((down & bit) && !(down_ & bit)) {
        started_[key] = now;
        blocked_ &= ~bit;
        if (key < 2) repeated_[key] = 0;
        if (key == 4) standbySent_ = false;
        const Event events[] = { Event::Brighter, Event::Dimmer, Event::Next, Event::Previous, Event::PlayDown };
        emit(events[key]);
      } else if (!(down & bit) && (down_ & bit)) {
        if (key == 4 && !standbySent_ && !(blocked_ & bit)) {
          if (pendingPlayTap_ && now - playReleasedAt_ <= kPlayDoubleClickMs) {
            pendingPlayTap_ = false;
            emit(Event::PlayDoubleTap);
          } else {
            pendingPlayTap_ = true;
            playReleasedAt_ = now;
          }
        }
        blocked_ &= ~bit;
      }
    }
    down_ = down & 0x1f;
  }
};

inline int stepBrightness(int pwm, bool on, int direction) {
  int percent = on ? (pwm * 100 + 127) / 255 : 0;
  percent += direction * 2;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  return (percent * 255 + 50) / 100;
}
inline unsigned cyclePreset(unsigned current, bool next) {
  if (current < 1 || current > 3) return next ? 1 : 3;
  return next ? current % 3 + 1 : (current + 1) % 3 + 1;
}
} // namespace remote_control

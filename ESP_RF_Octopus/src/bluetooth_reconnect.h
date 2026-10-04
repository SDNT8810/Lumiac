#pragma once
#include <stdint.h>

namespace bluetooth_remote {
class Reconnect {
  bool enabled_ = true, delayed_ = false;
  uint32_t since_ = 0, delay_ = 0;
public:
  bool enabled() const { return enabled_; }
  void resume() { enabled_ = true; delayed_ = false; }
  void suspend() { enabled_ = false; }
  // A failed attempt or a scan delays retries; it never disables them.
  void wait(uint32_t now, uint32_t delay = 2000) {
    since_ = now; delay_ = delay; delayed_ = true;
  }
  bool due(uint32_t now, bool ready, bool saved, bool connected, bool connecting, bool scanning) const {
    return enabled_ && ready && saved && !connected && !connecting && !scanning
      && (!delayed_ || now - since_ >= delay_);
  }
};
} // namespace bluetooth_remote

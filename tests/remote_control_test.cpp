#include "../ESP_RF_Octopus/src/remote_gestures.h"
#include "../ESP_RF_Octopus/src/remote_feedback.h"
#include "../ESP_RF_Octopus/src/bluetooth_reconnect.h"
#include "../Marlin/marlin-2.1.2.6/Marlin/src/feature/lumiac_light_command.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <string>

using remote_control::Event;
using remote_control::Gestures;

int main() {
  bluetooth_remote::Reconnect reconnect;
  auto retryDue = [&](uint32_t now) { return reconnect.due(now, true, true, false, false, false); };
  assert(retryDue(0)); // Saved remote: no browser request or initial ten-second delay.
  assert(!reconnect.due(0, false, true, false, false, false)); // HID not ready.
  assert(!reconnect.due(0, true, false, false, false, false)); // Nothing paired yet.
  assert(!reconnect.due(0, true, true, true, false, false)); // Already connected.
  assert(!reconnect.due(0, true, true, false, true, false)); // Attempt in flight.
  // A sleeping remote may reject many attempts; retries must remain enabled.
  for (uint32_t time = 0; time < 40000; time += 2000) {
    assert(retryDue(time));
    reconnect.wait(time);
    assert(reconnect.enabled() && !retryDue(time + 1999));
    assert(retryDue(time + 2000));
  }
  assert(!reconnect.due(40000, true, true, false, false, true)); // Scan temporarily defers retry.
  reconnect.wait(40000, 10000);
  assert(reconnect.enabled() && !retryDue(49999) && retryDue(50000));
  reconnect.suspend(); // Explicit Disconnect must stay disconnected.
  reconnect.wait(50000); // Its late close callback cannot re-enable it.
  assert(!reconnect.enabled() && !retryDue(100000));
  reconnect.resume(); // Manual reconnect to the saved remote also restores retries.
  assert(retryDue(100000));
  reconnect.wait(100000);
  assert(retryDue(102000));
  reconnect.wait(UINT32_MAX - 999);
  assert(!retryDue(999) && retryDue(1000));
  reconnect.resume();
  assert(!reconnect.due(1000, true, false, false, false, false)); // Forgotten address is never retried.

  std::vector<Event> events;
  auto emit = [&](Event e) { events.push_back(e); };
  auto count = [&](Event e) { return std::count(events.begin(), events.end(), e); };
  Gestures keys;
  keys.update(1, 0, emit);
  keys.update(1, 50, emit); // Remote repeats its held report.
  keys.update(0, 100, emit);
  assert(count(Event::Brighter) == 1);

  events.clear(); keys.cancel();
  keys.update(2, 100, emit);
  keys.tick(499, emit); assert(count(Event::Dimmer) == 1);
  keys.tick(500, emit); assert(count(Event::Dimmer) == 2);
  keys.tick(1500, emit); assert(count(Event::Dimmer) == 17); // 15 x 2% = 30% in the next second.
  keys.update(0, 1500, emit);
  keys.tick(5000, emit); assert(count(Event::Dimmer) == 17);

  for (uint8_t mask : {1, 2}) {
    events.clear(); keys.cancel();
    const Event direction = mask == 1 ? Event::Brighter : Event::Dimmer;
    keys.update(mask, 0, emit);
    keys.tick(400, emit);
    const auto before = count(direction);
    // Irregular polling must still deliver exactly 30 percentage points per second.
    for (uint32_t now = 413; now < 1400; now += 13) keys.tick(now, emit);
    keys.tick(1400, emit);
    assert((count(direction) - before) * 2 == 30);
    keys.update(0, 1401, emit);
  }

  events.clear(); keys.cancel();
  keys.update(16, 0, emit); assert(events.back() == Event::PlayDown);
  keys.tick(1500, emit); assert(count(Event::Standby) == 0);
  keys.update(0, 2499, emit); assert(count(Event::PlayTap) == 0);
  keys.tick(3099, emit); assert(count(Event::PlayTap) == 0);
  keys.tick(3100, emit); assert(events.back() == Event::PlayTap);
  assert(count(Event::Standby) == 0);
  events.clear(); keys.cancel();
  keys.update(16, 3000, emit);
  keys.tick(5499, emit); assert(count(Event::Standby) == 0);
  keys.tick(5500, emit); assert(count(Event::Standby) == 1);
  keys.tick(5600, emit); keys.update(0, 6000, emit);
  assert(count(Event::Standby) == 1 && count(Event::PlayTap) == 0);
  events.clear();
  keys.update(16, 7000, emit); keys.update(0, 9600, emit); // Hold release without timer tick
  assert(count(Event::Standby) == 1 && count(Event::PlayTap) == 0);

  events.clear(); keys.cancel();
  keys.update(17, 1000, emit); // Brightness and transport are independent.
  keys.tick(3500, emit);
  assert(count(Event::Brighter) > 1 && count(Event::Standby) == 1);
  keys.cancel();
  const auto canceledCount = events.size();
  keys.tick(4000, emit); keys.update(0, 4000, emit);
  assert(events.size() == canceledCount); // Disconnect never produces a Play tap.

  events.clear(); keys.cancel();
  keys.update(1, 0, emit); keys.tick(15000, emit);
  assert(keys.down() == 0); // Timed-out keys also stop glowing on the dashboard.
  keys.update(1, 15100, emit); keys.tick(15200, emit);
  assert(count(Event::Brighter) == 1); // Stale input blocked until release.
  keys.update(0, 15300, emit); keys.update(1, 15400, emit);
  assert(count(Event::Brighter) == 2);

  events.clear(); keys.cancel();
  keys.update(16, UINT32_MAX - 1000, emit);
  keys.tick(1498, emit); assert(count(Event::Standby) == 0);
  keys.tick(1499, emit); assert(count(Event::Standby) == 1); // millis wrap
  keys.update(0, 1600, emit); assert(count(Event::PlayTap) == 0);

  // Two completed clicks within 600 ms produce one random command, no single tap.
  for (uint32_t gap : {200u, 600u}) {
    events.clear(); keys.cancel();
    keys.update(16, 100, emit); keys.update(0, 150, emit);
    keys.update(16, 150 + gap - 50, emit);
    keys.update(16, 150 + gap - 10, emit); // Duplicate down report.
    keys.update(0, 150 + gap, emit);
    keys.tick(3000, emit);
    assert(count(Event::PlayDoubleTap) == 1 && count(Event::PlayTap) == 0);
    assert(count(Event::PlayDown) == 2 && count(Event::Standby) == 0);
  }
  events.clear(); keys.cancel();
  keys.update(16, 100, emit); keys.update(0, 150, emit);
  keys.tick(751, emit); assert(count(Event::PlayTap) == 1);
  keys.update(16, 800, emit); keys.update(0, 850, emit);
  keys.tick(1451, emit);
  assert(count(Event::PlayTap) == 2 && count(Event::PlayDoubleTap) == 0);

  // A second press held for standby must never briefly start/resume random.
  events.clear(); keys.cancel();
  keys.update(16, 100, emit); keys.update(0, 150, emit);
  keys.update(16, 300, emit); keys.tick(2799, emit);
  assert(count(Event::PlayTap) == 0 && count(Event::PlayDoubleTap) == 0);
  keys.tick(2800, emit); keys.update(0, 2900, emit); keys.tick(4000, emit);
  assert(count(Event::Standby) == 1 && count(Event::PlayTap) == 0 && count(Event::PlayDoubleTap) == 0);

  events.clear(); keys.cancel();
  keys.update(16, 100, emit); keys.update(0, 150, emit);
  keys.cancel(); keys.tick(1000, emit); // Disconnect cancels a pending single tap too.
  assert(count(Event::PlayTap) == 0 && count(Event::PlayDoubleTap) == 0);
  events.clear(); keys.cancel();
  keys.update(16, 100, emit); keys.update(0, 150, emit);
  keys.update(4, 300, emit); keys.update(0, 350, emit); keys.tick(1000, emit);
  assert(count(Event::Next) == 1 && count(Event::PlayTap) == 0);

  events.clear(); keys.cancel();
  keys.update(16, UINT32_MAX - 199, emit); keys.update(0, UINT32_MAX - 99, emit);
  keys.update(16, 100, emit); keys.update(0, 200, emit); keys.tick(1000, emit);
  assert(count(Event::PlayDoubleTap) == 1 && count(Event::PlayTap) == 0);

  events.clear(); keys.cancel();
  keys.update(4, 0, emit); keys.update(4, 1000, emit);
  assert(count(Event::Next) == 1);
  keys.update(0, 1100, emit); keys.update(8, 1200, emit);
  assert(count(Event::Previous) == 1);
  assert(remote_control::cyclePreset(0, true) == 1);
  assert(remote_control::cyclePreset(0, false) == 3);
  assert(remote_control::cyclePreset(3, true) == 1);
  assert(remote_control::cyclePreset(1, false) == 3);
  assert(remote_control::cyclePreset(2, false) == 1);
  assert(remote_control::stepBrightness(179, true, 1) == 184);
  assert(remote_control::stepBrightness(179, true, -1) == 173);
  assert(remote_control::stepBrightness(255, true, 1) == 255);
  assert(remote_control::stepBrightness(0, false, -1) == 0);
  assert(remote_control::stepBrightness(179, false, 1) == 5); // + from off starts at 2%

  remote_control::KeyFeedback feedback;
  std::vector<std::pair<uint8_t, uint8_t>> frames;
  auto show = [&](uint8_t down, uint8_t held) { frames.push_back({down, held}); };
  feedback.update(1, 100, show);
  feedback.update(0, 110, show);
  assert(frames.size() == 2 && frames[0].first == 1 && frames[1].first == 0);
  frames.clear();
  feedback.update(1, 200, show);
  feedback.update(17, 300, show); // Concurrent brightness and play have separate timers.
  feedback.update(17, 599, show);
  assert(frames.size() == 2 && feedback.held() == 0);
  feedback.update(17, 600, show); assert(feedback.held() == 1);
  feedback.update(17, 700, show); assert(feedback.held() == 17);
  feedback.update(17, 800, show); assert(frames.size() == 4); // No duplicate frames.
  feedback.update(0, 801, show); // Disconnect / cancellation clears both colors.
  assert(feedback.down() == 0 && feedback.held() == 0);
  feedback.update(64, UINT32_MAX - 199, show); // RF POS3 and timer wrap.
  feedback.update(64, 199, show); assert(feedback.held() == 0);
  feedback.update(64, 200, show); assert(feedback.held() == 64);
  feedback.update(0, 201, show);

  auto light = [](const std::string& text) {
    uint16_t state = lumiac::light_start;
    int result = -1;
    for (char c : text) { int value = lumiac::consume_light(state, c); if (value >= 0) result = value; }
    return result;
  };
  for (unsigned i = 0; i < 256; ++i) {
    char text[6]; snprintf(text, sizeof(text), "%03u\n", i);
    assert(light(text) == int(i));
  }
  for (auto bad : {"256\n", "999\n", "-01\n", "25\n", "0255\n", "1a2\n", "025 X1\n", "\n"})
    assert(light(bad) == -1);
  uint16_t uart1 = lumiac::light_start, uart2 = lumiac::light_start;
  for (char c : std::string("123")) { lumiac::consume_light(uart1, c); lumiac::consume_light(uart2, '0'); }
  assert(lumiac::consume_light(uart1, '\n') == 123);
  assert(lumiac::consume_light(uart2, '\n') == 0);
  puts("PASS automatic Bluetooth retry lifecycle, remote timing, live key feedback, tap/hold, disconnect, stale keys, wraparound, preset cycling, PWM and independent UART light decoding");
}

// The runner supplies the real emergency parser with only its hardware config stubbed.
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "feature/e_parser.h"

bool wait_for_user = true, wait_for_heatup = true;
bool held = false;
unsigned pauses = 0, resumes = 0;
void quickpause_stepper() { held = true; ++pauses; }
void quickresume_stepper() { held = false; ++resumes; }
void report_current_position_moving() {}
bool EmergencyParser::enabled = true;
bool EmergencyParser::killed_by_M112 = false;
bool EmergencyParser::quickstop_by_M410 = false;
volatile int16_t EmergencyParser::light_pending = -1;
volatile uint8_t EmergencyParser::fan_mode = lumiac::FanNormal;
EmergencyParser emergency_parser;

void feed(EmergencyParser::State& port, const char* text) {
  for (; *text; ++text) EmergencyParser::update(port, *text);
}
int main() {
  EmergencyParser::State uart = EmergencyParser::EP_RESET, usb = EmergencyParser::EP_RESET;
  auto fan = [&](bool moving) { return lumiac::motion_fan_pwm(EmergencyParser::fan_mode, moving, held); };
  assert(fan(false) == 77); // Boot with no commands: 30%.
  assert(fan(true) == 204); // POS/manual/homing movement: 80%.
  feed(uart, ";P000\n"); assert(held && pauses == 1);
  assert(fan(true) == 77); // Queued moves remain, but pause must lower cooling.
  for (unsigned i = 0; i <= 255; ++i) {
    char text[10]; snprintf(text, sizeof(text), ";L%03u\n", i);
    feed(uart, text); assert(EmergencyParser::light_pending == int(i));
    assert(held && pauses == 1 && resumes == 0); // Dimming never resumes motion.
    assert(fan(true) == 77);
  }
  feed(uart, ";R000\n"); assert(!held && resumes == 1);
  assert(fan(true) == 204);
  feed(uart, "P000\n"); assert(held && pauses == 2);
  feed(uart, "M410\n"); assert(EmergencyParser::quickstop_by_M410);
  feed(uart, "R000\n"); assert(!held && resumes == 2);
  feed(uart, ";F002\n"); assert(fan(true) == 255); // Random.
  assert(fan(false) == 77); // Finished movement still returns to idle.
  feed(uart, ";P000\n"); assert(fan(true) == 77);
  feed(uart, ";R000\n"); assert(fan(true) == 255);
  feed(uart, ";F001\r\n"); assert(fan(true) == 204);
  for (const char* bad : {";F003\n", ";F255\n", ";F256\n", ";F00\n", ";F0000\n", ";F00x\n", ";F-01\n", "M118 ;F000\n"}) {
    feed(uart, bad); assert(EmergencyParser::fan_mode == lumiac::FanNormal);
  }
  feed(uart, ";L179\r\n"); assert(EmergencyParser::light_pending == 179);
  for (const char* bad : {";L999\n", ";L256\n", ";L25\n", ";L1x2\n", ";L0012\n", ";Layer 100\n", "M118 ;L000\n", ";hello ;L000\n"}) {
    feed(uart, bad); assert(EmergencyParser::light_pending == 179);
  }
  feed(uart, ";L1"); feed(usb, ";L0");
  feed(uart, "23\n"); assert(EmergencyParser::light_pending == 123);
  feed(usb, "05\n"); assert(EmergencyParser::light_pending == 5);
  feed(uart, ";F00"); feed(usb, ";L1");
  feed(uart, "2\n"); feed(usb, "79\n");
  assert(EmergencyParser::fan_mode == lumiac::FanRandom && EmergencyParser::light_pending == 179);
  feed(uart, ";F000\n;L000\n"); // Standby turns off fan and lamps without a queued G-code.
  assert(fan(false) == 0 && fan(true) == 0 && EmergencyParser::light_pending == 0);
  feed(uart, ";L005\n"); // Brightness alone in standby must not wake the fan.
  assert(fan(true) == 0 && EmergencyParser::light_pending == 5);
  feed(uart, ";L000\n");
  EmergencyParser::disable();
  feed(uart, ";L111\n;F002\n;P000\n");
  assert(EmergencyParser::light_pending == 0 && fan(true) == 0 && !held);
  EmergencyParser::enable();
  feed(uart, ";F001\n;L179\nM108\n"); // Wake restores normal cooling and 70% light.
  assert(fan(false) == 77 && fan(true) == 204);
  assert(EmergencyParser::light_pending == 179 && !wait_for_user && !wait_for_heatup);
  puts("PASS real UART parser: fan idle/motion/random/pause/standby/wake, independent lamp control, malformed input, interleaved ports and disabled parser");
}

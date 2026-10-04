#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "remote_gestures.h"

namespace bluetooth_remote {
using Handler = void (*)(remote_control::Event);
using KeyHandler = void (*)(uint8_t down, uint8_t held);
void begin();
void poll(Handler handler, KeyHandler keys);
void keyState(JsonObject out);
void state(JsonObject out);
bool command(const String& action, const String& address, String& message);
}

// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#include "hal_function.h"
#include <avr/interrupt.h>

void HAL_setPWMLight(uint8_t value) {
    OCR2B = value;
}

static void (*_endstopCallback)(uint8_t) = nullptr;

void HAL_registerEndstopCallback(void (*callback)(uint8_t rawMask)) {
    _endstopCallback = callback;
}

ISR(PCINT2_vect) {
    uint8_t raw = ~PINK;
    if (_endstopCallback) _endstopCallback(raw);
}

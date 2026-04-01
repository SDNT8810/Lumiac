// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#include "hal_motion.h"
#include <avr/interrupt.h>

void HAL_setDirPin(uint8_t motorIndex, bool dir) {
    if (dir)
        PORTC &= ~(1 << motorIndex);
    else
        PORTC |=  (1 << motorIndex);
}

void HAL_stepPulse(uint8_t motorIndex) {
    PORTA |= (1 << motorIndex);
    __asm__ __volatile__("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n");
    PORTA &= ~(1 << motorIndex);
}

uint8_t HAL_readEndstopBit(uint8_t motorIndex) {
    return !(PINK & (1 << motorIndex));
}

void HAL_stopStepperTimer() {
    TIMSK1 &= ~(1 << OCIE1A);
}

static void (*_stepperCallback)() = nullptr;

void HAL_registerStepperTimerCallback(void (*callback)()) {
    _stepperCallback = callback;
}

ISR(TIMER1_COMPA_vect) {
    if (_stepperCallback) _stepperCallback();
}

// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#include "hal_init.h"
#include <avr/interrupt.h>

void HAL_initPWMLight(uint8_t initialValue) {
    TCCR2A = 0;
    TCCR2B = 0;
    TCCR2A |= (1 << WGM20) | (1 << WGM21);
    TCCR2A |= (1 << COM2B1);
    TCCR2B |= (1 << CS20);
    OCR2B   = initialValue;
}

void HAL_initStepperTimer(uint32_t timerFreqHz) {
    cli();
    TCCR1A = 0;
    TCCR1B = 0;
    TCCR1B |= (1 << WGM12);
    TCCR1B |= (1 << CS10);
    OCR1A   = (16000000UL / timerFreqHz) - 1;
    TIMSK1 |= (1 << OCIE1A);
    sei();
}

void HAL_initEndstopInterrupt() {
    PCICR  |= (1 << PCIE2);
    PCMSK2 |= 0b11111111;
}

void HAL_deinitEndstopInterrupt() {
    PCICR  &= ~(1 << PCIE2);
    PCMSK2  = 0x00;
}

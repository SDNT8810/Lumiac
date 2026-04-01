// LumiacTx v0.1.3
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.

#include "hal_tx.h"
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <Arduino.h>
#include <RF24.h>

extern RF24 radio;

static void (*_cb_portD)(uint8_t) = nullptr;
static void (*_cb_portC)(uint8_t) = nullptr;

void HAL_TX_registerWakeCallbacks(void (*cb_portD)(uint8_t), void (*cb_portC)(uint8_t)) {
    _cb_portD = cb_portD;
    _cb_portC = cb_portC;
}

ISR(PCINT2_vect) {
    if (_cb_portD) _cb_portD(PIND);
}

ISR(PCINT1_vect) {
    if (_cb_portC) _cb_portC(PINC);
}

void HAL_TX_enableWakeInterrupts() {
    PCICR |= (1 << PCIE2);
    PCMSK2 |=
        (1 << PCINT18) |   // D2 - btnOff
        (1 << PCINT19) |   // D3 - btnOn
        (1 << PCINT20) |   // D4 - btnP1
        (1 << PCINT21) |   // D5 - btnP2
        (1 << PCINT23) |   // D7 - btnP3
        (1 << PCINT16);    // D8 - btnFun (D8 = PCINT0 in realtà su Nano, verificare)

    PCICR |= (1 << PCIE1);
    PCMSK1 |=
        (1 << PCINT8) |    // A0 - pinCLK encoder
        (1 << PCINT9);     // A1 - pinDT  encoder
}

void HAL_TX_sleep() {
    radio.powerDown();
    ADCSRA &= ~(1 << ADEN);
    power_adc_disable();

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();

    noInterrupts();
    // BOD disable durante sleep
    MCUCR |= (1 << BODS) | (1 << BODSE);
    MCUCR = (MCUCR & ~(1 << BODSE)) | (1 << BODS);
    interrupts();

    sleep_cpu();
    sleep_disable();

    power_adc_enable();
    ADCSRA |= (1 << ADEN);
    radio.powerUp();
    delay(5);
}

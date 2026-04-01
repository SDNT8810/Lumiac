// LumiacTx v0.1.3
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.

#ifndef HAL_TX_H
#define HAL_TX_H

#include <stdint.h>

// Registra i callback chiamati dalle ISR di wake-up.
// cb_portD riceve lo snapshot di PIND (Port D = pin digitali D0-D7)
// cb_portC riceve lo snapshot di PINC (Port C = pin analogici A0-A5)
void HAL_TX_registerWakeCallbacks(void (*cb_portD)(uint8_t), void (*cb_portC)(uint8_t));

// Abilita gli interrupt di pin-change per i pulsanti e l'encoder.
// Su AVR: configura PCICR, PCMSK1, PCMSK2.
void HAL_TX_enableWakeInterrupts();

// Porta il microcontrollore in sleep profonda (PWR_DOWN).
// Spegne ADC e radio prima di dormire, li riaccende al risveglio.
// Su AVR: SLEEP_MODE_PWR_DOWN + BOD disable + power_adc.
void HAL_TX_sleep();

#endif

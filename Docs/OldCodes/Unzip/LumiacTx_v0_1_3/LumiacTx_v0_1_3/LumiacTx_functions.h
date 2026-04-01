// LumiacTx v0.1.3
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.

#ifndef LUMIAC_TX_FUNCTIONS_H
#define LUMIAC_TX_FUNCTIONS_H

#include <stdint.h>
#include <stdbool.h>

void wakeCallbackPortD(uint8_t portSnapshot);
void wakeCallbackPortC(uint8_t portSnapshot);
void goToSleep();
bool sendWithAck(const char* payload);
void processWakeEvent(uint8_t dState, uint8_t cState);

#endif

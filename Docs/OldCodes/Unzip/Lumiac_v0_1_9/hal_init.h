// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#ifndef HAL_INIT_H
#define HAL_INIT_H

#include <stdint.h>

void HAL_initPWMLight(uint8_t initialValue);
void HAL_initStepperTimer(uint32_t timerFreqHz);
void HAL_initEndstopInterrupt();
void HAL_deinitEndstopInterrupt();

#endif

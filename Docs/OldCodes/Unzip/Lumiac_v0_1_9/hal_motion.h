// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#ifndef HAL_MOTION_H
#define HAL_MOTION_H

#include <stdint.h>
#include <stdbool.h>

void HAL_setDirPin(uint8_t motorIndex, bool dir);
void HAL_stepPulse(uint8_t motorIndex);
uint8_t HAL_readEndstopBit(uint8_t motorIndex);
void HAL_stopStepperTimer();
void HAL_registerStepperTimerCallback(void (*callback)());

#endif

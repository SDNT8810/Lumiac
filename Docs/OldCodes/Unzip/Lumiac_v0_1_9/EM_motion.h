// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#ifndef EM_MOTION_H
#define EM_MOTION_H

#include <stdint.h>
#include <stdbool.h>

bool endstopValido(uint8_t pin);
void emergencyStop();
void ctrlEndStop();
void calcMovement();
void fineMovimento();
void stepperTimerISRCallback();

#endif

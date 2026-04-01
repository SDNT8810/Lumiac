// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#ifndef HAL_FUNCTION_H
#define HAL_FUNCTION_H

#include <stdint.h>

void HAL_setPWMLight(uint8_t value);
void HAL_registerEndstopCallback(void (*callback)(uint8_t rawMask));

#endif

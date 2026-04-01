// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#include "hal_common.h"
#include <avr/wdt.h>
#include <avr/interrupt.h>

void HAL_wdtDisable()        { wdt_disable(); }
void HAL_softReset()         { wdt_enable(WDTO_15MS); while (1) {} }
void HAL_disableInterrupts() { cli(); }
void HAL_enableInterrupts()  { sei(); }

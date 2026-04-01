// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#ifndef EM_COMM_H
#define EM_COMM_H

#include <stdint.h>

void spiSelDev(uint8_t devId);
void resetRF();
uint32_t tmc5160_read(uint8_t address);
void resetSG();

struct TMC_CheckResult {
    bool spi_ok;
    bool chopconf_ok;
    bool current_ok;
    bool microstep_ok;
    bool sg_ok;
};

TMC_CheckResult checkTMC(uint8_t i);
void testMemDrv();

#endif

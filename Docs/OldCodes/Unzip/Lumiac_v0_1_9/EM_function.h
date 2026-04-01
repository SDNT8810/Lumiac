// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#ifndef EM_FUNCTION_H
#define EM_FUNCTION_H

#include <stdint.h>

void softReset();
void resetRxCod();
void generaRndPos();
void emPos(int armID, uint32_t posF);
void emOFF();
void handleLight();
void ledStatus(int emStatus);
void rxCmdUART();
void rxCmdRF();
void interpretaComando();
void allineaArrayPos();
void handleSG();
void endstopISRCallback(uint8_t rawMask);

#endif

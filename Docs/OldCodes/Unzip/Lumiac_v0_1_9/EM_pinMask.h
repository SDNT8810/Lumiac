// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#ifndef EM_PINMASK_H
#define EM_PINMASK_H

#include "EM_conf2.h"
#include "EM_pins.h"

#if nMot == 1
    static const uint8_t STEP_PINS[nMot]    = {STEP_PINSM1};
    static const uint8_t DIR_PINS[nMot]     = {DIR_PINSM1};
    static const uint8_t ENDSTOP_PINS[nMot] = {EndstopPinM1};
    static const uint8_t CSN_PINS[nMot]     = {CsnPinDrvM1};
    static const uint8_t DIAG_PINS[nMot]    = {DiagPinM1};
#elif nMot == 2
    static const uint8_t STEP_PINS[nMot]    = {STEP_PINSM1, STEP_PINSM2};
    static const uint8_t DIR_PINS[nMot]     = {DIR_PINSM1,  DIR_PINSM2};
    static const uint8_t ENDSTOP_PINS[nMot] = {EndstopPinM1, EndstopPinM2};
    static const uint8_t CSN_PINS[nMot]     = {CsnPinDrvM1, CsnPinDrvM2};
    static const uint8_t DIAG_PINS[nMot]    = {DiagPinM1, DiagPinM2};
#elif nMot == 3
    static const uint8_t STEP_PINS[nMot]    = {STEP_PINSM1, STEP_PINSM2, STEP_PINSM3};
    static const uint8_t DIR_PINS[nMot]     = {DIR_PINSM1,  DIR_PINSM2,  DIR_PINSM3};
    static const uint8_t ENDSTOP_PINS[nMot] = {EndstopPinM1, EndstopPinM2, EndstopPinM3};
    static const uint8_t CSN_PINS[nMot]     = {CsnPinDrvM1, CsnPinDrvM2, CsnPinDrvM3};
    static const uint8_t DIAG_PINS[nMot]    = {DiagPinM1, DiagPinM2, DiagPinM3};
#elif nMot == 4
    static const uint8_t STEP_PINS[nMot]    = {STEP_PINSM1, STEP_PINSM2, STEP_PINSM3, STEP_PINSM4};
    static const uint8_t DIR_PINS[nMot]     = {DIR_PINSM1,  DIR_PINSM2,  DIR_PINSM3,  DIR_PINSM4};
    static const uint8_t ENDSTOP_PINS[nMot] = {EndstopPinM1, EndstopPinM2, EndstopPinM3, EndstopPinM4};
    static const uint8_t CSN_PINS[nMot]     = {CsnPinDrvM1, CsnPinDrvM2, CsnPinDrvM3, CsnPinDrvM4};
    static const uint8_t DIAG_PINS[nMot]    = {DiagPinM1, DiagPinM2, DiagPinM3, DiagPinM4};
#elif nMot == 5
    static const uint8_t STEP_PINS[nMot]    = {STEP_PINSM1, STEP_PINSM2, STEP_PINSM3, STEP_PINSM4, STEP_PINSM5};
    static const uint8_t DIR_PINS[nMot]     = {DIR_PINSM1,  DIR_PINSM2,  DIR_PINSM3,  DIR_PINSM4,  DIR_PINSM5};
    static const uint8_t ENDSTOP_PINS[nMot] = {EndstopPinM1, EndstopPinM2, EndstopPinM3, EndstopPinM4, EndstopPinM5};
    static const uint8_t CSN_PINS[nMot]     = {CsnPinDrvM1, CsnPinDrvM2, CsnPinDrvM3, CsnPinDrvM4, CsnPinDrvM5};
    static const uint8_t DIAG_PINS[nMot]    = {DiagPinM1, DiagPinM2, DiagPinM3, DiagPinM4, DiagPinM5};
#elif nMot == 6
    static const uint8_t STEP_PINS[nMot]    = {STEP_PINSM1, STEP_PINSM2, STEP_PINSM3, STEP_PINSM4, STEP_PINSM5, STEP_PINSM6};
    static const uint8_t DIR_PINS[nMot]     = {DIR_PINSM1,  DIR_PINSM2,  DIR_PINSM3,  DIR_PINSM4,  DIR_PINSM5,  DIR_PINSM6};
    static const uint8_t ENDSTOP_PINS[nMot] = {EndstopPinM1, EndstopPinM2, EndstopPinM3, EndstopPinM4, EndstopPinM5, EndstopPinM6};
    static const uint8_t CSN_PINS[nMot]     = {CsnPinDrvM1, CsnPinDrvM2, CsnPinDrvM3, CsnPinDrvM4, CsnPinDrvM5, CsnPinDrvM6};
    static const uint8_t DIAG_PINS[nMot]    = {DiagPinM1, DiagPinM2, DiagPinM3, DiagPinM4, DiagPinM5, DiagPinM6};
#else
    #error "Valore di nMot non valido"
#endif

#endif

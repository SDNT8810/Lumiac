// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#ifndef EM_CONF2_H
#define EM_CONF2_H

#define debug           1

#define nMot            6

#define GEAR_RATIO      ratio
#define LEAD_MM         pitch
#define MOVE_MM         corsaMax
#define DIR_INVERT_MASK 0b11111111
#define ENDSTOP_MASK    0xFF

#define stepMotor       200
#define microstp        16
#define MOTOR_STEPS     stepMotor
#define MICROSTEPS      microstp
#define RMS_CURRENT     1800
#define R_SENSE         0.075f
#define sensSG          5
#define sogliaTG        0xFFFFF

#define TIMER_FREQ      40000UL

#define ACCEL_STEP_S2   ((uint32_t)(ACCEL_MM_S2 * STEPS_PER_MM))
#define MM_PER_REV      (LEAD_MM * GEAR_RATIO)
#define STEPS_PER_MM    ((uint32_t)(((float)(MOTOR_STEPS * MICROSTEPS) / MM_PER_REV) + 0.5f))
#define MAX_STEP_HZ     ((uint32_t)(velMax * STEPS_PER_MM))
#define MIN_STEP_HZ     ((uint32_t)(velMin * STEPS_PER_MM))
#define HOM_STEP_HZ     ((uint32_t)(velHom * STEPS_PER_MM))
#define TOTAL_STEPS     ((uint32_t)(MOVE_MM * STEPS_PER_MM + 0.5f))

#define pos1      ((uint32_t)(posizione1  * STEPS_PER_MM + 0.5f))
#define pos2      ((uint32_t)(posizione2  * STEPS_PER_MM + 0.5f))
#define pos3      ((uint32_t)(posizione3  * STEPS_PER_MM + 0.5f))
#define posON     ((uint32_t)(posizioneON * STEPS_PER_MM + 0.5f))
#define posOFF    ((int32_t) (posizioneH  * STEPS_PER_MM - 1000.0f))
#define posH      ((uint32_t)(posizioneH  * STEPS_PER_MM))
#define posT      ((uint32_t)(posizioneT  * STEPS_PER_MM + 0.5f))
#define posMax    ((uint32_t)(corsaMax    * STEPS_PER_MM))
#define minGap    ((uint32_t)(saltoRnd    * STEPS_PER_MM + 0.5f))
#define posToll   ((uint32_t)(tollRnd     * STEPS_PER_MM + 0.5f))

#define devId_Rf24   0
#define devId_DrvM1  1
#define devId_DrvM2  2
#define devId_DrvM3  3
#define devId_DrvM4  4
#define devId_DrvM5  5
#define devId_DrvM6  6
#define devId_DrvM7  7
#define devId_DrvM8  8
#define nDevSpi      (nMot + 1)

extern uint8_t csnPins[nDevSpi];

#endif

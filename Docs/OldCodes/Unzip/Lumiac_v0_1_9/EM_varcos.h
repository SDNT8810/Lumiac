// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#ifndef EM_VARCOS_H
#define EM_VARCOS_H

#include <stdint.h>
#include <Arduino.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <TMCStepper.h>
#include "EM_config.h"
#include "EM_codeTRx.h"
#include "EM_pins.h"
#include "EM_pinMask.h"

extern const uint8_t  armN;
extern       uint8_t  armIdx;
extern const uint8_t  xEncValue;

enum PowerState  { vsON, ON, vsOFF, OFF   };
enum runState    { IDLE, MOVING, FUN, STOP };
enum alarmState  { ttOk, EndS, Stall, Emrcy };
enum speedState  { stop, slow, normal, fast  };

extern PowerState  pState;
extern runState    rState;
extern alarmState  aState;
extern speedState  sState;

extern int32_t   finalPos[nMot];
extern int32_t   lastPos[nMot];
extern uint32_t  deltaPos[nMot];
extern uint32_t  errorAcc[nMot];

extern volatile bool     motorStop[nMot];
extern          bool     stallGuard[nMot];
extern volatile uint8_t  alarmGuard[nMot];
extern volatile uint8_t  endstopRawMask;
extern          bool     allStopped;

#define cw      1
#define ccw     0
#define premuto 0
#define nonprem 1

#define aTtOk  0
#define aEndS  1
#define aStall 2
#define aEmrcy 3

extern volatile uint32_t maxSteps;
extern volatile uint32_t counter;
extern volatile uint32_t step_acc;
extern volatile uint32_t step_rate;
extern volatile uint32_t max_rate;
extern volatile uint32_t min_rate;
extern volatile uint32_t accel_inc;
extern volatile uint32_t accel_steps;
extern volatile bool     motionActive;
extern volatile bool     movementDoneFlag;

extern const int32_t  baudrate;
extern RF24           radio;
extern const byte     address[6];
extern TMC5160Stepper* driver[nMot];

extern int16_t       rxCodP;
extern int16_t       rxLivL;
extern const int16_t livLON;
extern const int16_t funDelay;
extern int16_t       lastLiv;
extern int16_t       lastCod;
extern String        comando;

extern volatile bool    endstopEnabled;
extern volatile uint8_t endstopEventMask;

#endif

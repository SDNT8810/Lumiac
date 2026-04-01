// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#include "EM_varcos.h"

const uint8_t  armN      = nMot;
      uint8_t  armIdx    = 0;
const uint8_t  xEncValue = 10;

PowerState  pState = OFF;
runState    rState = IDLE;
alarmState  aState = ttOk;
speedState  sState = normal;

int32_t  finalPos[nMot] = {};
int32_t  lastPos[nMot]  = {};
uint32_t deltaPos[nMot] = {};
uint32_t errorAcc[nMot] = {};

volatile bool    motorStop[nMot]   = {};
         bool    stallGuard[nMot]  = {};
volatile uint8_t alarmGuard[nMot]  = {};
volatile uint8_t endstopRawMask    = 0;
         bool    allStopped        = true;

volatile uint32_t maxSteps        = 0;
volatile uint32_t counter         = 0;
volatile uint32_t step_acc        = 0;
volatile uint32_t step_rate       = 0;
volatile uint32_t max_rate        = 0;
volatile uint32_t min_rate        = 0;
volatile uint32_t accel_inc       = 0;
volatile uint32_t accel_steps     = 0;
volatile bool     motionActive    = false;
volatile bool     movementDoneFlag = false;

const int32_t baudrate = 115200;

RF24        radio(RF24_CE_PIN, RF24_CSN_PIN);
const byte  address[6] = indirizzo;

TMC5160Stepper* driver[nMot];

int16_t       rxCodP   = codOFF;
int16_t       rxLivL   = codEnc;
const int16_t livLON   = dimmerON;
const int16_t funDelay = (int16_t)(pausaFunMov * 1000);
int16_t       lastLiv  = codEnc;
int16_t       lastCod  = codOFF;
String        comando  = "";

volatile bool    endstopEnabled   = true;
volatile uint8_t endstopEventMask = 0;

// csnPins: mappa devId → pin CSN fisico
// devId_Rf24=0, devId_DrvM1=1 ... devId_DrvM6=6
uint8_t csnPins[nDevSpi] = {
    CsnPinDrvM1,
    CsnPinDrvM2,
    CsnPinDrvM3,
    CsnPinDrvM4,
    CsnPinDrvM5,
    CsnPinDrvM6,
    RF24_CSN_PIN
};

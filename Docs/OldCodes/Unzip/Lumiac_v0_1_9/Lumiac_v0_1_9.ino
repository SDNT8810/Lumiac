// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

// HAL layer: hal_common, hal_motion, hal_function, hal_init

#define verFW 0.19

#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <TMCStepper.h>

#include "hal_common.h"
#include "hal_motion.h"
#include "hal_function.h"
#include "hal_init.h"

#include "EM_config.h"
#include "EM_pins.h"
#include "EM_pinMask.h"
#include "EM_codeTRx.h"
#include "EM_varcos.h"
#include "EM_comm.h"
#include "EM_motion.h"
#include "EM_function.h"
#include "EM_init.h"
#include "EM_test.h"
#include "EM_radioTx.h"

void setup() {
    HAL_wdtDisable();
    delay(2000);
    Serial.begin(baudrate);
    initPins();
    initTimerStep();
    initPWM();
    initVar();
    initIntES();

    SPI.begin();
    delay(250);
    initTMC();
    delay(250);
    initRF();
    delay(250);

    primoAvvio();
    installStart();
    ctrlEndStop();

    if (debug == 1) {
        Serial.print("LUMIAC pronta. - Versione FW: "); Serial.print(verFW);
        Serial.print(" - N. bracci= "); Serial.println(armN);
    }
}

void loop() {
    ctrlEndStop();
    rxCmdUART();
    rxCmdRF();

    if (rxCodP == codON && pState != ON) {
        pState = vsON;
    } else if (rxCodP == codON) {
        rxCodP = codIdle;
    }

    if (pState != OFF) {

        if (debug == 1 && (rState == MOVING || rState == FUN)) {
            Serial.print("Asse 1 - finalPos: "); Serial.print(finalPos[0]);
            Serial.print(" - lastPos: ");         Serial.print(lastPos[0]);
            Serial.print("  |   Asse 2 - finalPos: "); Serial.print(finalPos[1]);
            Serial.print(" - lastPos: ");              Serial.println(lastPos[1]);
        }

        if (rxCodP != lastCod) {
            interpretaComando();
        }

        if (movementDoneFlag) {
            movementDoneFlag = false;
            allineaArrayPos();
            if (rState == FUN) {
                emPos(armN, posMax);
                if (debug == 1) { Serial.print("Modalità FUN da loop. Valore rState= "); Serial.println(rState); }
            } else {
                if (debug == 1) {
                    Serial.print("Stato alarmGuard: "); Serial.print(alarmGuard[0]); Serial.print(" - "); Serial.println(alarmGuard[1]);
                    Serial.print("Stati macchina - pState: "); Serial.print(pState); Serial.print(" - rState: "); Serial.print(rState); Serial.print(" - aState: "); Serial.println(aState);
                }
                fineMovimento();
            }
        }
        handleLight();
        handleSG();
    }
}

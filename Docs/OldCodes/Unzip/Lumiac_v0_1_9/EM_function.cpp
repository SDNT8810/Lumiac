// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#include "EM_function.h"
#include "EM_motion.h"
#include "EM_varcos.h"
#include "hal_common.h"
#include "hal_function.h"
#include <nRF24L01.h>
#include <RF24.h>
#include <TMCStepper.h>
#include <Arduino.h>

void softReset() {
    HAL_softReset();
}

void resetRxCod() {
    lastCod = rxCodP;
    rxCodP  = codIdle;
    if (debug == 1) {
        Serial.print("Valore rxCod: "); Serial.print(rxCodP);
        Serial.print(" - Valore lastCod: "); Serial.println(lastCod);
    }
}

void generaRndPos() {
    for (armIdx = 0; armIdx < armN; armIdx++) {
        int32_t posStart = posH + posToll;
        int32_t posEnd   = posMax - posToll;
        do {
            finalPos[armIdx] = random(posStart, posEnd + 1);
            if (finalPos[armIdx] < (int32_t)posH || finalPos[armIdx] > (int32_t)posMax) {
                finalPos[armIdx] = random(posStart, posEnd + 1);
                if (debug == 1) Serial.println("Superati i limiti, ricalcolo.");
            }
        } while (abs(finalPos[armIdx] - lastPos[armIdx]) < (int32_t)minGap);

        if (debug == 1) {
            Serial.print("generaRndPos: Motore n.: "); Serial.print(armIdx);
            Serial.print(" - Posizione attuale= ");    Serial.print(lastPos[armIdx]);
            Serial.print(" - Posizione finale (rnd)= "); Serial.println(finalPos[armIdx]);
        }
    }
    if (funDelay > 0) delay(funDelay);
}

void emPos(int armID, uint32_t posF) {
    resetRxCod();

    for (uint8_t i = 0; i < armN; i++) motorStop[i] = false;

    if (rState == FUN) {
        generaRndPos();
        if (debug == 1) Serial.println("Ho generato rndPos.");
    } else {
        for (armIdx = 0; armIdx < armID; armIdx++) {
            if (debug == 1) {
                Serial.print("Motore n.= "); Serial.print(armIdx);
                Serial.print(" - Posizione attuale= "); Serial.print(lastPos[armIdx]);
            }
            finalPos[armIdx] = posF;
            if (debug == 1) { Serial.print(" - Posizione finale= "); Serial.println(finalPos[armIdx]); }
        }
    }

    if (debug == 1) Serial.println("Lancio calcMovement.");
    calcMovement();
    if (debug == 1) Serial.println("Funzione emPos completata.");
}

void emOFF() {
    Serial.println("Spengo");
    pState  = vsOFF;
    rState  = MOVING;
    sState  = normal;
    lastLiv = rxLivL;
    emPos(armN, posOFF);
}

void handleLight() {
    uint32_t counterFade = (counter == 0) ? counter : counter - 1;

    switch (pState) {
        case vsOFF:
            rxLivL = map(counterFade, 0, maxSteps, lastLiv, 0);
            Serial.print("handleLight: fade2OFF maxSteps= "); Serial.print(maxSteps);
            Serial.print(" | counter= ");     Serial.print(counter);
            Serial.print(" - counterFade= "); Serial.print(counterFade);
            Serial.print(" - rxLivL= ");      Serial.print(rxLivL);
            Serial.print(" - lastLiv= ");     Serial.println(lastLiv);
            break;
        case vsON:
            rxLivL = map(counterFade, 0, maxSteps, 0, livLON);
            break;
        default:
            if (rxLivL <= 0)   rxLivL = 0;
            if (rxLivL >= 255) rxLivL = 255;
            break;
    }
    HAL_setPWMLight((uint8_t)rxLivL);
}

void ledStatus(int emStatus) {
    if (debug == 1) {
        Serial.println("Funzione LedStatus completata.");
        Serial.print("Stato: "); Serial.print(emStatus);
    }
}

void rxCmdUART() {
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n') {
            if      (comando == "off" || comando == "p99") { rxCodP = codOFF;  }
            else if (comando == "on"  || comando == "p77") { rxCodP = codON;   }
            else if (comando == "p1"  || comando == "p11") { rxCodP = codPos1; }
            else if (comando == "p2"  || comando == "p22") { rxCodP = codPos2; }
            else if (comando == "p3"  || comando == "p33") { rxCodP = codPos3; }
            else if (comando == "fun" || comando == "p55") { rxCodP = codFun;  }
            else if (comando == "h") {
                HAL_disableInterrupts();
                motionActive = false;
                rState = STOP;
                aState = Emrcy;
                pState = vsOFF;
                comando = "";
                return;
            }
            else if (comando == "r") {
                HAL_disableInterrupts();
                motionActive = false;
                rState = STOP;
                aState = Emrcy;
                pState = ON;
                HAL_softReset();
                return;
            }
            else if (comando.toInt() >= 0 && comando.toInt() <= 255) {
                lastLiv = rxLivL;
                rxLivL  = (int16_t)comando.toInt();
                if (debug == 1) {
                    Serial.print("Liv. Encoder: "); Serial.print(comando.toInt());
                    Serial.print(" - "); Serial.println(rxLivL);
                }
            }
            else { comando = ""; return; }
            comando = "";
        } else {
            comando += c;
            if (debug == 1) { Serial.print("rxCmdUART+: Comando (buffer)= "); Serial.println(comando); }
        }
    }
}

void rxCmdRF() {
    if (radio.available()) {
        char msg[32] = {0};
        radio.read(msg, sizeof(msg));
        radio.writeAckPayload(1, "OK", 3);

        String s         = String(msg);
        int    separator = s.indexOf(':');
        if (separator < 0) return;

        int    incomingID = s.substring(0, separator).toInt();
        if (incomingID != trxID) return;

        String payload = s.substring(separator + 1);
        String codRx   = payload.substring(1);

        if (payload.startsWith("P")) {
            rxCodP = (int16_t)codRx.toInt();
            if (debug == 1) { Serial.print("Ricevuto P: "); Serial.println(rxCodP); }
        }

        if (payload.startsWith("L")) {
            if (debug == 1) { Serial.print("Ricevuto L: "); Serial.println(codRx.toInt()); }
            if (pState != vsON && pState != vsOFF) {
                if (codRx.toInt() == 10) rxLivL += xEncValue;
                if (codRx.toInt() == 5)  rxLivL -= xEncValue;
            }
            if (debug == 1) { Serial.print("Dimmer="); Serial.println(rxLivL); }
        }
    }
}

void interpretaComando() {
    switch (rxCodP) {
        case codPos1: rState = MOVING; if (debug > 1) Serial.println("Vado in posizione 1"); emPos(armN, pos1); break;
        case codPos2: rState = MOVING; if (debug > 1) Serial.println("Vado in posizione 2"); emPos(armN, pos2); break;
        case codPos3: rState = MOVING; if (debug > 1) Serial.println("Vado in posizione 3"); emPos(armN, pos3); break;
        case codFun:  rState = FUN;    if (debug > 1) Serial.println("Attivo modalità giostra"); emPos(armN, posMax); break;
        case codOFF:
            pState = vsOFF; rState = MOVING; lastLiv = rxLivL;
            if (debug > 1) Serial.println("Avvio lo spegnimento");
            emOFF();
            break;
        case codON:
            pState = vsON; rState = MOVING;
            if (debug > 1) Serial.println("Accensione in corso");
            emPos(armN, posON);
            break;
        case codIdle: break;
        default: return;
    }
}

void allineaArrayPos() {
    if (debug == 1) Serial.println("Allineo Array lastPos=finalPos.");
    HAL_disableInterrupts();
    for (uint8_t i = 0; i < armN; i++) lastPos[i] = finalPos[i];
    HAL_enableInterrupts();
}

void handleSG() {
    for (armIdx = 0; armIdx < 1; armIdx++) {
        stallGuard[armIdx] = !digitalRead(DIAG_PINS[armIdx]);
        digitalWrite(StatusLedPin, stallGuard[0] ? HIGH : LOW);
    }
}

void endstopISRCallback(uint8_t rawMask) {
    endstopRawMask = rawMask & ENDSTOP_MASK;
}

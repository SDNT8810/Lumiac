// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#include "EM_init.h"
#include "EM_motion.h"
#include "EM_function.h"
#include "EM_comm.h"
#include "EM_varcos.h"
#include "hal_common.h"
#include "hal_motion.h"
#include "hal_function.h"
#include "hal_init.h"
#include <Arduino.h>
#include <SPI.h>
#include <TMCStepper.h>

bool isProtectedPin(uint8_t p) {
    return (
        p == 0  || p == 1  ||
        p == 10 || p == 11 || p == 12 || p == 13 ||
        p == 50 || p == 51 || p == 52 || //p == 53 ||
        p == EndstopPinM1 || p == EndstopPinM2 || p == EndstopPinM3 || p == EndstopPinM4 ||
        p == EndstopPinM5 || p == EndstopPinM6
        p == EnablePin
    );
}

void initPins() {
    for (uint8_t p = 2; p <= 53; p++) {
        if (isProtectedPin(p)) continue;
        pinMode(p, OUTPUT);
        digitalWrite(p, LOW);
    }
    delay(500);

    pinMode(EnablePin,    OUTPUT); digitalWrite(EnablePin,    HIGH);
    pinMode(LAMP_PWM_PIN, OUTPUT); analogWrite(LAMP_PWM_PIN, codEnc);
    pinMode(RF24_CE_PIN,  OUTPUT);
    pinMode(RF24_CSN_PIN, OUTPUT); digitalWrite(RF24_CSN_PIN, HIGH);

    for (int i = 0; i < armN; i++) {
        pinMode(STEP_PINS[i],    OUTPUT); digitalWrite(STEP_PINS[i],    LOW);
        pinMode(DIR_PINS[i],     OUTPUT); digitalWrite(DIR_PINS[i],     LOW);
        pinMode(ENDSTOP_PINS[i], INPUT);
        pinMode(DIAG_PINS[i],    INPUT_PULLUP);
        pinMode(CSN_PINS[i],     OUTPUT); digitalWrite(CSN_PINS[i], HIGH);
    }

    pinMode(StatusLedPin, OUTPUT); digitalWrite(StatusLedPin, LOW);

    if (debug == 1) Serial.println(". Inizializzazione PIN completata.");
}

void initIntES() {
    HAL_registerEndstopCallback(endstopISRCallback);
    HAL_initEndstopInterrupt();
}

void deinitIntES() {
    HAL_deinitEndstopInterrupt();
}

void initRF() {
    radio.begin();
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.enableAckPayload();
    radio.setAutoAck(true);
    radio.openReadingPipe(1, address);
    radio.clearStatusFlags();
    radio.flush_rx();
    radio.flush_tx();

    if (radio.isChipConnected()) {
        radio.startListening();
        if (debug == 1) {
            Serial.println("Ricevitore Online");
            Serial.println(". Inizializzazione RTx completata.");
        }
    } else {
        Serial.println("Ricevitore Offline");
        resetRF();
    }
}

void initPWM() {
    HAL_initPWMLight(codEnc);
}

void initTimerStep() {
    HAL_registerStepperTimerCallback(stepperTimerISRCallback);
    HAL_initStepperTimer(TIMER_FREQ);
    motionActive = false;
}

void initStatusHW() {
    motionActive = false;
    pState       = OFF;
    rState       = IDLE;
}

void initTMC() {
    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE3));

    for (uint8_t i = 0; i < nMot; i++) {
        driver[i] = new TMC5160Stepper(CSN_PINS[i], R_SENSE);
        driver[i]->begin();
        driver[i]->GSTAT();
        driver[i]->toff(5);
        driver[i]->blank_time(54);
        driver[i]->en_pwm_mode(false);
        driver[i]->shaft(false);
        driver[i]->multistep_filt(false);
        driver[i]->direct_mode(false);
        driver[i]->rms_current((uint16_t)RMS_CURRENT);
        driver[i]->ihold(0);
        driver[i]->iholddelay(15);
        driver[i]->hstrt(5);
        driver[i]->hend(3);
        driver[i]->microsteps(microstp);
        driver[i]->intpol(false);
        driver[i]->TCOOLTHRS(sogliaTG);
        driver[i]->sgt(sensSG);
        driver[i]->diag0_stall(true);
        driver[i]->diag0_error(false);
    }

    SPI.endTransaction();
    testMemDrv();
}

void initVar() {
    rxCodP = codOFF;
    rxLivL = codEnc;
    lastLiv = rxLivL;
    randomSeed(analogRead(A0));

    for (int i = 0; i < armN; i++) {
        lastPos[i]  = posH;
        finalPos[i] = -1;
    }
    for (int i = 0; i < 1; i++) {
        stallGuard[i] = false;
    }

    if (debug == 1) Serial.println(". Inizializzazione variabili completata.");
}

void primoAvvio() {
    int speedInit = 60 * (int)ratio;

    digitalWrite(EnablePin, LOW);

    for (int i = 0; i < armN; i++) {
        if (debug == 1) { Serial.print("Faccio girare il mot n: "); Serial.println(i); }

        digitalWrite(DIR_PINS[i], cw);
        for (int t = 0; t < (int)posT; t++) {
            digitalWrite(STEP_PINS[i], HIGH); delayMicroseconds(speedInit);
            digitalWrite(STEP_PINS[i], LOW);  delayMicroseconds(speedInit);
        }

        digitalWrite(DIR_PINS[i], ccw);
        if (digitalRead(ENDSTOP_PINS[i]) == premuto) {
            if (debug == 1) { Serial.print("Motore "); Serial.print(i); Serial.println(" in home."); }
            lastPos[i]  = posH;
            finalPos[i] = posH;
            deltaPos[i] = 0;
            errorAcc[i] = 0;
            continue;
        } else {
            while (true) {
                digitalWrite(STEP_PINS[i], HIGH); delayMicroseconds(speedInit);
                digitalWrite(STEP_PINS[i], LOW);  delayMicroseconds(speedInit);
                if (endstopValido(ENDSTOP_PINS[i])) break;
            }
        }
    }

    for (int i = 0; i < armN; i++) {
        lastPos[i]  = posH;
        finalPos[i] = posH;
        deltaPos[i] = 0;
        errorAcc[i] = 0;
        motorStop[i] = false;
    }

    digitalWrite(EnablePin, HIGH);

    pState  = OFF;
    rState  = IDLE;
    rxCodP  = codIdle;
    rxLivL  = codEnc;
    lastLiv = rxLivL;
    HAL_setPWMLight((uint8_t)rxLivL);

    if (debug == 1) {
        Serial.println("Homing completato!");
        Serial.println("Funzione primoAvvio completata.");
    }
}

void installStart() {
    digitalWrite(StatusLedPin, HIGH); delay(100);
    digitalWrite(StatusLedPin, LOW);  delay(300);
    digitalWrite(StatusLedPin, HIGH); delay(100);
    digitalWrite(StatusLedPin, LOW);  delay(300);
    digitalWrite(StatusLedPin, HIGH); delay(100);
    digitalWrite(StatusLedPin, LOW);  delay(300);

    endstopEnabled = false;
    delay(50);
    endstopEnabled = true;
}

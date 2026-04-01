// Lumiac v0.1.9
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.
// =================================================================================

#include "EM_test.h"
#include "EM_motion.h"
#include "EM_varcos.h"
#include <Arduino.h>
#include <TMCStepper.h>

void selfTestTMC_Move() {
    const int speedH        = 50;
    const int TEST_STEPS_CW = 3000;
    const int TEST_SPEED    = speedH * 3;

    if (debug == 1) Serial.println(". selfTestTMC_Move --------------------------------");

    digitalWrite(EnablePin, LOW);
    delay(10);

    for (uint8_t i = 0; i < nMot; i++) {

        digitalWrite(DIR_PINS[i], cw);
        delayMicroseconds(50);

        if (debug == 1) Serial.println(". selfTestTMC_Move - Step cw - microStep std");
        for (int t = 0; t < TEST_STEPS_CW; t++) {
            digitalWrite(STEP_PINS[i], HIGH); delayMicroseconds(TEST_SPEED);
            digitalWrite(STEP_PINS[i], LOW);  delayMicroseconds(TEST_SPEED);
        }
        delay(100);

        driver[i]->microsteps(16);
        delay(5);
        if (debug == 1) Serial.println(". selfTestTMC_Move - Step cw - microStep 16");
        for (int t = 0; t < TEST_STEPS_CW; t++) {
            digitalWrite(STEP_PINS[i], HIGH); delayMicroseconds(TEST_SPEED);
            digitalWrite(STEP_PINS[i], LOW);  delayMicroseconds(TEST_SPEED);
        }
        delay(100);

        driver[i]->microsteps(4);
        delay(5);
        if (debug == 1) Serial.println(". selfTestTMC_Move - Step cw - microStep 4");
        for (int t = 0; t < TEST_STEPS_CW; t++) {
            digitalWrite(STEP_PINS[i], HIGH); delayMicroseconds(TEST_SPEED);
            digitalWrite(STEP_PINS[i], LOW);  delayMicroseconds(TEST_SPEED);
        }

        driver[i]->microsteps(microstp);
        delay(100);

        if (debug == 1) Serial.println(". selfTestTMC_Move - Step cw - Corrente 0.3A");
        driver[i]->rms_current(300);
        delay(10);
        for (int t = 0; t < TEST_STEPS_CW; t++) {
            digitalWrite(STEP_PINS[i], HIGH); delayMicroseconds(TEST_SPEED);
            digitalWrite(STEP_PINS[i], LOW);  delayMicroseconds(TEST_SPEED);
        }
        delay(100);

        driver[i]->rms_current((uint16_t)RMS_CURRENT);
        delay(10);

        driver[i]->sgt(-5);
        delay(5);
        driver[i]->sgt(sensSG);

        digitalWrite(DIR_PINS[i], ccw);
        delayMicroseconds(50);
        while (true) {
            digitalWrite(STEP_PINS[i], HIGH); delayMicroseconds(speedH);
            digitalWrite(STEP_PINS[i], LOW);  delayMicroseconds(speedH);
            if (endstopValido(ENDSTOP_PINS[i])) break;
        }
    }

    digitalWrite(EnablePin, HIGH);
    delay(10);
}

void selfTestTMC_DIAG() {
    const int speedH        = 50;
    const int TEST_STEPS_CW = 10000;
    const int TEST_STEPS_CCW = 300;
    const int TEST_SPEED    = speedH * 3;

    if (debug == 1) Serial.println(". selfTestTMC_Diag -------------------------------------");

    digitalWrite(EnablePin, LOW);
    delay(10);

    for (uint8_t i = 0; i < nMot; i++) {

        driver[i]->TCOOLTHRS(0xFFFFF);
        driver[i]->sgt(5);
        driver[i]->diag0_stall(true);
        driver[i]->diag0_error(false);
        delay(10);

        digitalWrite(DIR_PINS[i], cw);
        delayMicroseconds(50);

        bool stallDetected = false;
        for (int t = 0; t < TEST_STEPS_CW; t++) {
            digitalWrite(STEP_PINS[i], HIGH); delayMicroseconds(TEST_SPEED);
            digitalWrite(STEP_PINS[i], LOW);  delayMicroseconds(TEST_SPEED);
            if (t > 50 && digitalRead(DIAG_PINS[i]) == LOW) {
                stallDetected = true;
                break;
            }
        }

        if (stallDetected)
            { Serial.print("DRV "); Serial.print(i); Serial.println(" STALL OK"); }
        else
            { Serial.print("DRV "); Serial.print(i); Serial.println(" STALL **FAIL**"); }

        delay(2000);

        digitalWrite(DIR_PINS[i], ccw);
        delayMicroseconds(50);
        for (int t = 0; t < TEST_STEPS_CCW; t++) {
            digitalWrite(STEP_PINS[i], HIGH); delayMicroseconds(TEST_SPEED);
            digitalWrite(STEP_PINS[i], LOW);  delayMicroseconds(TEST_SPEED);
        }
        delay(50);

        while (true) {
            digitalWrite(STEP_PINS[i], HIGH); delayMicroseconds(speedH);
            digitalWrite(STEP_PINS[i], LOW);  delayMicroseconds(speedH);
            if (endstopValido(ENDSTOP_PINS[i])) break;
        }
    }

    digitalWrite(EnablePin, HIGH);
    delay(10);
}

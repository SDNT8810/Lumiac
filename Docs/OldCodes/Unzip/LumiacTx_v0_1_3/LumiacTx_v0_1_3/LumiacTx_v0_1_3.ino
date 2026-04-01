// LumiacTx v0.1.3
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.

#include <SPI.h>
#include <RF24.h>
#include "hal_tx.h"
#include "LumiacTx_config.h"
#include "LumiacTx_functions.h"
#include "EM_codeTRx.h"

RF24        radio(RF24_CE_PIN, RF24_CSN_PIN);
const byte  address[6] = indirizzo;

unsigned long lastActivity = 0;

volatile bool    wakeEvent       = false;
volatile uint8_t portDSnapshot   = 0;
volatile uint8_t portCSnapshot   = 0;

int txLivL = codEnc;
int txCodP = 0;

// Callback chiamate dalla HAL nelle ISR — salvano lo snapshot e segnalano il wake
void onWakePortD(uint8_t snapshot) { wakeEvent = true; portDSnapshot = snapshot; }
void onWakePortC(uint8_t snapshot) { wakeEvent = true; portCSnapshot = snapshot; }

void setup() {
    Serial.begin(115200);

    pinMode(statusLed, OUTPUT);

    radio.begin();
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.setRetries(5, 15);
    radio.enableAckPayload();
    radio.setAutoAck(true);
    radio.openWritingPipe(address);
    radio.openReadingPipe(1, address);
    radio.stopListening();

    pinMode(btnOff, INPUT_PULLUP);
    pinMode(btnOn,  INPUT_PULLUP);
    pinMode(btnP1,  INPUT_PULLUP);
    pinMode(btnP2,  INPUT_PULLUP);
    pinMode(btnP3,  INPUT_PULLUP);
    pinMode(btnFun, INPUT_PULLUP);
    pinMode(pinCLK, INPUT_PULLUP);
    pinMode(pinDT,  INPUT_PULLUP);

#if USE_ENCODER
    if (debug == 1) Serial.println("Modalità: ENCODER");
#else
    if (debug == 1) Serial.println("Modalità: DUE PULSANTI");
#endif

    HAL_TX_registerWakeCallbacks(onWakePortD, onWakePortC);
    HAL_TX_enableWakeInterrupts();

    lastActivity = millis();
}

void loop() {

    if (wakeEvent) {
        noInterrupts();
        uint8_t d = portDSnapshot;
        uint8_t c = portCSnapshot;
        wakeEvent = false;
        interrupts();

        processWakeEvent(d, c);

        if (txCodP != 0) {
            char bufferBtn[10];
            sprintf(bufferBtn, "P%04d", txCodP);
            sendWithAck(bufferBtn);
            txCodP = 0;
        }

        if (txLivL != 0) {
            char bufferEnc[10];
            sprintf(bufferEnc, "L%04d", txLivL);
            sendWithAck(bufferEnc);
            txLivL = 0;
        }
    }

    if (millis() - lastActivity > SLEEP_TIMEOUT) {
        goToSleep();
    }
}

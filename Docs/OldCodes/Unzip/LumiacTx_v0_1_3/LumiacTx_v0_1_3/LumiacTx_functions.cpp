// LumiacTx v0.1.3
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.

#include "LumiacTx_functions.h"
#include "LumiacTx_config.h"
#include "hal_tx.h"
#include "EM_codeTRx.h"
#include <Arduino.h>
#include <RF24.h>

extern RF24          radio;
extern unsigned long lastActivity;
extern volatile bool wakeEvent;
extern int           txLivL;
extern int           txCodP;

// wakeCallbackPortD / wakeCallbackPortC
// Chiamate dalla HAL quando scatta l'ISR di wake-up.
// Sostituiscono le due ISR(PCINT*_vect) che leggevano PIND/PINC direttamente.

void wakeCallbackPortD(uint8_t portSnapshot) {
    wakeEvent    = true;
    // il valore viene salvato dalla HAL e passato a processWakeEvent() nel loop
    (void)portSnapshot; // usato tramite il meccanismo di snapshot nel .ino
}

void wakeCallbackPortC(uint8_t portSnapshot) {
    wakeEvent    = true;
    (void)portSnapshot;
}


void goToSleep() {
#if debug
    Serial.println("Nano: Dormo...");
#endif

    digitalWrite(statusLed, HIGH);
    delay(100);
    digitalWrite(statusLed, LOW);

    HAL_TX_sleep(); // ← era: ADCSRA, power_adc, set_sleep_mode, sleep_cpu, MCUCR, ecc.

#if debug
    Serial.println("Nano: Mi sveglio");
#endif

    lastActivity = millis();
}


bool sendWithAck(const char* payload) {
    char buffer[32];
    sprintf(buffer, "%04d:%s", trxID, payload);

#if debug
    Serial.print("Nano: Trasmetto -> ");
    Serial.println(buffer);
#endif

    char ackBuffer[10];
    radio.stopListening();

    for (int attempt = 0; attempt < 3; attempt++) {
        bool ok = radio.write(buffer, strlen(buffer) + 1);

        if (ok) {
            radio.startListening();
            unsigned long start = millis();

            while (millis() - start < 20) {
                if (radio.available()) {
                    radio.read(&ackBuffer, sizeof(ackBuffer));
                    radio.stopListening();
                    if (strcmp(ackBuffer, "OK") == 0) {
                        lastActivity = millis();
                        return true;
                    }
                }
            }
            radio.stopListening();
        }
    }

#if debug
    Serial.println("Nano: Nessun ACK!");
#endif

    for (int i = 0; i < 3; i++) {
        digitalWrite(statusLed, HIGH); delay(80);
        digitalWrite(statusLed, LOW);  delay(80);
    }

    return false;
}


// processWakeEvent
// Legge gli snapshot di porta passati come parametro
void processWakeEvent(uint8_t dState, uint8_t cState) {

    if (!(dState & (1 << PD2))) txCodP = codOFF;
    if (!(dState & (1 << PD3))) txCodP = codON;
    if (!(dState & (1 << PD4))) txCodP = codPos1;
    if (!(dState & (1 << PD5))) txCodP = codPos2;
    if (!(dState & (1 << PD7))) txCodP = codPos3;

#if USE_ENCODER
    if (!(cState & (1 << PC0))) txLivL = 10;
    if (!(cState & (1 << PC1))) txLivL = 5;
#else
    if (!(cState & (1 << PC0))) txLivL = 10;
    if (!(cState & (1 << PC1))) txLivL = 5;
#endif
}

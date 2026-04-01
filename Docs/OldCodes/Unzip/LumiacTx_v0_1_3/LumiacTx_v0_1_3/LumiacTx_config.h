// LumiacTx v0.1.3
// Copyright (c) 2026 Mirko Emilio Agusta
// All rights reserved.
// Proprietary and confidential. Provided under NDA. 
// Unauthorized use, copying, modification, or distribution is strictly prohibited.
//
// Use is limited to the agreed project scope. For authorized use only.

#ifndef LUMIAC_TX_CONFIG_H
#define LUMIAC_TX_CONFIG_H

#include <Arduino.h>

#define SLEEP_TIMEOUT 5000UL
#define debug         1
#define USE_ENCODER   1

static const int RF24_CE_PIN  = 9;
static const int RF24_CSN_PIN = 10;

static const int btnOff = 2;
static const int btnOn  = 3;
static const int btnP1  = 4;
static const int btnP2  = 5;
static const int btnP3  = 7;
static const int btnFun = 8;

static const int statusLed = 6;

static const int pinCLK = A0;
static const int pinDT  = A1;

#endif

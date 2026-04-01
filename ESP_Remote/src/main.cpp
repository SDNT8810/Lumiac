#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>



void setup() {
  Serial.begin(115200);
  serialLine.reserve(kSerialLineMax);

  octopusSerial.begin(kOctopusBaud, SERIAL_8N1, kOctopusRxPin, kOctopusTxPin);

}

void loop() {

}

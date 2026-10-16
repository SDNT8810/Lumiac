#pragma once
#include <time.h>
#include <new>
#if defined(ESP8266)
#include <LittleFS.h>
#define LUMIAC_FS LittleFS
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
extern "C" {
#include <cont.h>
}
#else
#include <SPIFFS.h>
#define LUMIAC_FS SPIFFS
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/queue.h>
#include <esp_system.h>
#endif
#include "cloud_config.h"
#include "build_version.h"
#include "../../shared/firmware_transfer.h"
#include "ota_writer.h"
#include "cloud_certificate.h"

#if defined(ESP8266)
constexpr char kControllerRole[] = "esp12";
constexpr char kOtaImageMarker[] = "LUMIAC_OTA_ROLE:esp12 " LUMIAC_FIRMWARE_REVISION;
#else
constexpr char kControllerRole[] = "esp32";
constexpr char kOtaImageMarker[] = "LUMIAC_OTA_ROLE:esp32 " LUMIAC_FIRMWARE_REVISION;
#endif

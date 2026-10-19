#pragma once
const char kLocalOtaMarker[] = "LUMIAC_LOCAL_OTA_V1";
bool otaIdentifier(const String& id) {
  if (id.length() != 36) return false;
  for (unsigned i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (id[i] != '-') return false; }
    else if (lumiac::firmwareHex(id[i]) < 0) return false;
  }
  return true;
}
String localFirmwareAuthorization(JsonVariantConst doc) {
  return otaMac("lumiac-local-v1|" + cloudBoot + "|" + localFirmwareNonce + "|" + doc["id"].as<String>() + "|" +
    doc["target"].as<String>() + "|" + doc["kind"].as<String>() + "|" + String(doc["size"].as<uint32_t>()) + "|" +
    doc["sha256"].as<String>() + "|" + String(doc["crc32"] | 0U) + "|" + doc["revision"].as<String>() + "|" +
    doc["machine"].as<String>() + "|" + String(doc["port"].as<unsigned>()) + "|" + doc["imageId"].as<String>());
}
const char* validateLocalFirmwareRequest(JsonVariantConst doc) {
  if (!doc.is<JsonObjectConst>() || doc["session"] != cloudBoot) return "Controller restarted; read local update info again";
  if (localFirmwareNonce.isEmpty() || doc["nonce"] != localFirmwareNonce ||
      !doc["auth"].is<const char*>() || !strlen(LUMIAC_CLOUD_DEVICE_KEY)) return "Local update authentication failed";
  const String id = doc["id"] | "", image = doc["imageId"] | "", hash = doc["sha256"] | "";
  const String target = doc["target"] | "", kind = doc["kind"] | "", revision = doc["revision"] | "";
  if (!otaIdentifier(id) || !otaIdentifier(image) || !otaHashValid(hash) || revision.isEmpty() || revision.length() > 32 ||
      !doc["port"].is<unsigned>() || doc["port"].as<unsigned>() < 1024 || doc["port"].as<unsigned>() > 65535 ||
      !doc["size"].is<uint32_t>()) return "Invalid local firmware manifest";
  if (doc["machine"] != LUMIAC_FIRMWARE_ID) return "Hardware/gearing identity changed; install a matched USB/SD package";
  const uint32_t size = doc["size"].as<uint32_t>();
  const bool controller = target == kControllerRole && kind == "app", octopus = target == "octopus" && kind == "octopus";
  if ((!controller && !octopus) || size < 1024 || size > (octopus ? lumiac::octopusFirmwareLimit : target == "esp12" ? 0x100000U : 0x1c0000U)) return "Wrong firmware target or size";
  if (!otaEqual(doc["auth"].as<String>(), localFirmwareAuthorization(doc))) return "Local update authentication failed";
  return nullptr;
}
bool localFirmwareClient(const IPAddress& ip) {
  if (ip[0] == 192 && ip[1] == 168 && ip[2] == 4 && ip[3] > 1 && ip[3] < 255) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  const uint32_t mask = WiFi.subnetMask(), station = WiFi.localIP(), remote = ip;
  return mask && remote && (remote & mask) == (station & mask) && remote != station;
}
void localFirmwareReply(unsigned code, const String& error = "") {
  JsonDocument doc;
  if (!error.isEmpty()) doc["error"] = error;
  else {
    writeRemoteState(doc);
    doc["localSupported"] = cloudStorageReady; doc["nonce"] = localFirmwareNonce;
    doc["machine"] = LUMIAC_FIRMWARE_ID; doc["localMarker"] = kLocalOtaMarker;
  }
  String body; serializeJson(doc, body); server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}
void handleLocalFirmwareInfo() { localFirmwareReply(200); }
void handleLocalFirmwareStart() {
  const IPAddress sender = server.client().remoteIP();
  if (!localFirmwareClient(sender)) { localFirmwareReply(403, "Use the Lumiac hotspot or the same router LAN"); return; }
  JsonDocument doc;
  if (server.arg("plain").length() > 1536 || deserializeJson(doc, server.arg("plain"))) { localFirmwareReply(400, "Invalid JSON"); return; }
  if (const char* error = validateLocalFirmwareRequest(doc.as<JsonVariantConst>())) { localFirmwareReply(403, error); return; }
  if (firmwareUpdating || updateRebootAt) { localFirmwareReply(409, "A firmware update is already active"); return; }
  if (startupHomeInProgress || embeddedPlayback.waitingForHome) { localFirmwareReply(409, "Wait for homing to finish before updating"); return; }
  startRemoteUpdate(doc.as<JsonVariantConst>());
  if (!firmwareUpdating || remoteUpdate.id != doc["id"].as<String>()) { localFirmwareReply(409, remoteUpdate.error.isEmpty() ? "Octopus must be connected and support updates" : remoteUpdate.error); return; }
  localFirmwareUrl = "http://" + sender.toString() + ":" + String(doc["port"].as<unsigned>()) + "/firmware/" + doc["imageId"].as<String>();
  localFirmwareNonce = cloudBoot + "-" + String(random(0x7fffffff), HEX);
  persistDiagnostic("Local Wi-Fi update accepted: " + remoteUpdate.target);
  localFirmwareReply(202);
}

#pragma once
// Included after the normal controller handlers, inside their namespace.
struct RemoteUpdate {
  String id, imageId, target, kind, sha256, revision, state = "idle", phase, error;
  uint32_t size = 0, done = 0, crc32 = 0;
} remoteUpdate;
String cloudBoot, cloudSsid, cloudPassword, cloudState = "not configured", cloudError;
String lastCloudActionId, lastCloudActionType, lastCloudActionError;
bool cloudStorageReady = false, pendingUpdateBoot = false;
String localFirmwareNonce, localFirmwareUrl;
String otaDeferredError;
bool otaOpenInFlight = false;
uint32_t lastCloudPoll = 0, lastWifiAttempt = 0, updateSince = 0, updateProgress = 0, updateRebootAt = 0;
String updateIdleMarker;
bool updateIdleConfirmed = false;
OtaWriter cloudWriter;
File octopusUpdateFile;
unsigned sdPhase = 0, sdRetries = 0;
uint32_t sdOffset = 0, sdExpectedOffset = 0, sdSentAt = 0;
String sdLastCommand, sdExpectedReply, sdLastReply;
bool sdWaiting = false;
#if defined(ESP8266)
BearSSL::WiFiClientSecure cloudTls;
BearSSL::Session cloudTlsSession;
BearSSL::X509List cloudRoots(kCloudRunRootCa);
#else
WiFiClientSecure cloudTls;
#endif
HTTPClient cloudHttp;
WiFiClient localFirmwareClientSocket;
#include "cloud_transport.h"
bool cloudTransportReady = false;
uint32_t cloudPollInterval = 5000, cloudFailures = 0, cloudLastElapsed = 0;
String lastControlFault, controllerResetReason;
uint32_t controlFaultCount = 0, controlFaultAt = 0;
bool cloudTransportBusy() { return cloudInFlight; }
#include "persistent_diagnostics.h"
#include "network_connection.h"

bool saveCloudSettings() {
  if (!cloudStorageReady) return false;
  JsonDocument doc;
  doc["ssid"] = cloudSsid; doc["password"] = cloudPassword;
  doc["parked"] = firmwareParked; doc["pending"] = pendingUpdateBoot;
  doc["lastFault"] = lastControlFault; doc["faultCount"] = controlFaultCount;
  if (pendingUpdateBoot) {
    doc["job"] = remoteUpdate.id; doc["target"] = remoteUpdate.target;
    doc["hash"] = remoteUpdate.sha256; doc["revision"] = remoteUpdate.revision;
    doc["size"] = remoteUpdate.size;
  }
  File file = LUMIAC_FS.open("/remote.tmp", "w");
  if (!file) return false;
  const size_t wanted = measureJson(doc), written = serializeJson(doc, file);
  file.flush(); file.close();
  if (wanted != written) return false;
  // SPIFFS cannot replace an existing file by rename. Keep a recoverable copy
  // while switching settings; startup also accepts this backup after power loss.
  if (LUMIAC_FS.exists("/remote.json")) {
    LUMIAC_FS.remove("/remote.backup");
    if (!LUMIAC_FS.rename("/remote.json", "/remote.backup")) return false;
  }
  if (LUMIAC_FS.rename("/remote.tmp", "/remote.json")) return true;
  LUMIAC_FS.rename("/remote.backup", "/remote.json");
  return false;
}
void clearFirmwarePark() {
  if (firmwareUpdating || !firmwareParked) return;
  firmwareParked = false;
  if (!saveCloudSettings()) firmwareParked = true;
}
void recordControlFault(const String& reason) {
  lastControlFault = reason; controlFaultAt = millis(); ++controlFaultCount;
  saveCloudSettings();
  persistDiagnostic("Control fault: " + reason);
}
void writeRemoteState(JsonDocument& doc) {
  doc["session"] = cloudBoot; doc["controller"] = kControllerRole;
  doc["software"] = LUMIAC_FIRMWARE_REVISION; doc["firmware"] = LUMIAC_FIRMWARE_ID;
  doc["sourceBranch"] = LUMIAC_FIRMWARE_BRANCH;
  doc["homed"] = octopusHomed; doc["homing"] = startupHomeInProgress || embeddedPlayback.waitingForHome;
  doc["octopusSoftware"] = octopusCloudRevision;
  JsonObject reliability = doc["reliability"].to<JsonObject>();
  reliability["lastFault"] = lastControlFault; reliability["faultCount"] = controlFaultCount;
  reliability["faultAtMs"] = controlFaultAt; reliability["resetReason"] = controllerResetReason;
  reliability["uptimeMs"] = millis(); reliability["freeHeap"] = ESP.getFreeHeap();
#if defined(ESP8266)
  reliability["largestHeapBlock"] = ESP.getMaxFreeBlockSize();
#else
  reliability["largestHeapBlock"] = ESP.getMaxAllocHeap();
#endif
  reliability["cloudBusy"] = cloudInFlight; reliability["cloudFailures"] = cloudFailures;
  reliability["cloudRequestMs"] = cloudLastElapsed;
  reliability["diagnosticWriteFailures"] = diagnosticWriteFailures;
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = cloudSsid; wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["passwordSaved"] = !cloudPassword.isEmpty(); wifi["pending"] = wifiConnectionPending;
  wifi["statusCode"] = int(WiFi.status()); wifi["attempts"] = wifiAttempts;
  wifi["disconnectReason"] = wifiDisconnectReason.load(std::memory_order_relaxed);
  wifi["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
  wifi["gateway"] = WiFi.gatewayIP().toString(); wifi["dns"] = WiFi.dnsIP().toString();
  wifi["rssi"] = WiFi.RSSI(); wifi["utcEpoch"] = time(nullptr);
  wifi["cloud"] = cloudState; wifi["error"] = WiFi.status() == WL_CONNECTED ? cloudError : networkWifiError();
  JsonObject ota = doc["ota"].to<JsonObject>();
  ota["supported"] = cloudStorageReady; ota["sdSupported"] = octopusSdOta;
  ota["localSupported"] = true; ota["localMarker"] = "LUMIAC_LOCAL_OTA_V1";
  ota["active"] = firmwareUpdating; ota["parked"] = firmwareParked;
  ota["marker"] = kOtaImageMarker; ota["id"] = remoteUpdate.id;
  ota["target"] = remoteUpdate.target; ota["kind"] = remoteUpdate.kind;
  ota["state"] = remoteUpdate.state; ota["phase"] = remoteUpdate.phase;
  ota["error"] = remoteUpdate.error; ota["sha256"] = remoteUpdate.sha256;
  ota["size"] = remoteUpdate.size; ota["done"] = remoteUpdate.done;
  JsonObject action = doc["remoteAction"].to<JsonObject>();
  action["id"] = lastCloudActionId; action["type"] = lastCloudActionType; action["error"] = lastCloudActionError;
}
void closeCloudHttp() { cloudHttp.end(); cloudTls.stop(); localFirmwareClientSocket.stop(); }
bool openCloudHttp(const String& path) {
  closeCloudHttp();
  configureCloudConnection(cloudTls, cloudHttp);
  if (!cloudHttp.begin(cloudTls, String(LUMIAC_CLOUD_URL) + path)) return false;
  cloudHttp.setUserAgent("Lumiac-Controller/1.0");
  cloudHttp.addHeader("X-Lumiac-Device-Key", LUMIAC_CLOUD_DEVICE_KEY);
  cloudHttp.addHeader("Accept-Encoding", "identity");
  return true;
}
void failRemoteUpdate(const String& error) {
  // The serialized worker still owns HTTP/TLS until its completion mailbox.
  // A late UART error must not close that connection from the hardware loop.
  if (otaOpenInFlight) { otaDeferredError = error; firmwareUpdateCancel = true; return; }
  cloudWriter.abort(); octopusUpdateFile.close(); closeCloudHttp();
  if (remoteUpdate.kind == "octopus" && sdPhase < 5) { octopusSerial.println(); octopusSerial.println("M997 P4"); }
  remoteUpdate.state = "failed"; remoteUpdate.phase = ""; remoteUpdate.error = error;
  firmwareUpdating = false; firmwareUpdateCancel = false; pendingUpdateBoot = false;
  saveCloudSettings(); lastCloudPoll = 0;
  saveUpdateCheckpoint(true);
  logMessage(kBoardName, "Firmware update failed: " + error);
}
void completeRemoteUpdate() {
  octopusUpdateFile.close(); LUMIAC_FS.remove("/octopus.update");
  remoteUpdate.state = "complete"; remoteUpdate.phase = "";
  remoteUpdate.done = remoteUpdate.size; firmwareUpdating = false;
  saveUpdateCheckpoint(true);
  lastCloudPoll = 0; logMessage(kBoardName, "Firmware update complete; motion remains parked.");
}
bool handleFirmwareLine(const String& raw) {
  String line = raw; line.trim(); if (line.startsWith("echo:")) { line.remove(0, 5); line.trim(); }
  if (line == "Cap:LUMIAC_SD_OTA:1") octopusSdOta = true;
  if (line.startsWith("LUMIAC_FW_VERSION:")) octopusCloudRevision = line.substring(strlen("LUMIAC_FW_VERSION:"));
  if (line.startsWith("LUMIAC_MACHINE:")) octopusCloudIdentity = line.substring(strlen("LUMIAC_MACHINE:"));
  if (!firmwareUpdating) return false;
  if (line == updateIdleMarker) updateIdleConfirmed = true;
  if (sdPhase && line.length()) sdLastReply = line.substring(0, 100);
  if (line == "start" && sdPhase && sdPhase < 5) {
    octopusHomed = octopusFirmwareReady = octopusOnline = octopusRealtime = octopusMotionFan = false;
    failRemoteUpdate("Octopus restarted during SD transfer at byte " + String(remoteUpdate.done)); return true;
  }
  if (line.startsWith("LUMIAC_FW_ERROR ") || (sdPhase && (line.startsWith("Error:") || line.startsWith("Unknown command:")))) {
    // A leading delimiter can finish a stale partial command. Its error must
    // be fenced by the following fresh abort ACK before opening a new file.
    if (sdPhase == 1 && sdExpectedReply == "LUMIAC_FW_ABORTED") return true;
    failRemoteUpdate(line); return true;
  }
  if (sdWaiting && ((sdExpectedReply == "offset" && line == "LUMIAC_FW_OFFSET " + String(sdExpectedOffset)) || line == sdExpectedReply)) {
    if (sdExpectedReply == "offset") { sdOffset = sdExpectedOffset; remoteUpdate.done = sdOffset; }
    sdWaiting = false;
  }
  return line.startsWith("LUMIAC_FW_");
}
void startRemoteUpdate(JsonVariantConst job) {
  if (firmwareUpdating || job["session"].as<String>() != cloudBoot) return;
  const String target = job["target"] | "", kind = job["kind"] | "", hash = job["sha256"] | "";
  if (!cloudStorageReady || !octopusOnline || !octopusFirmwareReady || !otaHashValid(hash) || job["machine"].as<String>() != LUMIAC_FIRMWARE_ID ||
      (target != kControllerRole && target != "octopus") ||
      (target == "octopus" ? kind != "octopus" || !octopusSdOta : kind != "app")) return;
  remoteUpdate = RemoteUpdate();
  localFirmwareUrl = ""; otaDeferredError = ""; pendingUpdateBoot = false; updateRebootAt = 0;
  remoteUpdate.id = job["id"].as<String>(); remoteUpdate.imageId = job["imageId"].as<String>();
  remoteUpdate.target = target; remoteUpdate.kind = kind; remoteUpdate.sha256 = hash;
  remoteUpdate.revision = job["revision"].as<String>(); remoteUpdate.size = job["size"] | 0U; remoteUpdate.crc32 = job["crc32"] | 0U;
  firmwareParked = true;
  if (!saveCloudSettings()) { failRemoteUpdate("Cannot persist the parked state"); return; }
  handleSimpleAction("stop_motion", "Firmware update");
  firmwareUpdating = true; firmwareUpdateCancel = false; updateIdleConfirmed = false;
  remoteUpdate.state = "stopping"; remoteUpdate.phase = "waiting for Octopus to stop";
  updateSince = millis(); sdPhase = 0; sdWaiting = false; sdLastReply = "";
  saveUpdateCheckpoint(true);
  updateIdleMarker = "LUMIAC_OTA_IDLE_" + remoteUpdate.id;
  octopusSerial.println(";F001"); // Preserve idle cooling while drivers hold.
  octopusSerial.println("M400"); octopusSerial.println("M118 " + updateIdleMarker);
}
void performFirmwareOpen(CloudExchange& exchange) {
  const uint32_t started = millis();
  bool opened;
  if (!exchange.payload.isEmpty()) {
    closeCloudHttp(); cloudHttp.setTimeout(8000); cloudHttp.setReuse(false); cloudHttp.useHTTP10(true);
#if defined(ESP32)
    cloudHttp.setConnectTimeout(8000);
#endif
    // LAN HTTP carries image bytes only; never send the cloud device key to a PC.
    opened = cloudHttp.begin(localFirmwareClientSocket, exchange.payload);
  } else opened = openCloudHttp("/device/v1/devices/" LUMIAC_CLOUD_DEVICE_ID "/firmware/" + remoteUpdate.imageId);
  if (!opened) { exchange.error = "Cannot open the firmware source"; return; }
  const int code = cloudHttp.GET();
  if (code != 200) exchange.error = exchange.payload.isEmpty() ? cloudConnectionError(cloudTls, code) : "Local firmware HTTP " + String(code);
  else if (cloudHttp.getSize() != int(remoteUpdate.size)) exchange.error = "Firmware download length mismatch";
  exchange.elapsed = millis() - started;
}
bool startFirmwareDownload() {
  if (cloudInFlight) return false;
  if (!cloudTransportReady) { failRemoteUpdate("Network worker unavailable"); return false; }
  CloudExchange* exchange = new (std::nothrow) CloudExchange;
  if (!exchange) { failRemoteUpdate("Insufficient memory to start the download"); return false; }
  exchange->operation = performFirmwareOpen; exchange->payload = localFirmwareUrl;
  if (!submitCloudExchange(exchange)) { delete exchange; return false; }
  cloudInFlight = otaOpenInFlight = true;
  remoteUpdate.state = "opening"; remoteUpdate.phase = "connecting to firmware source";
  return true;
}
void sdCommand(const String& line, const String& reply, uint32_t offset = 0) {
  sdLastCommand = line; sdExpectedReply = reply; sdExpectedOffset = offset;
  if (line == "M997 P4") octopusSerial.println(); // Clear any abandoned partial line.
  sdWaiting = true; sdRetries = 0; sdSentAt = millis(); octopusSerial.println(line);
}
void tickOctopusUpdate() {
  if (sdPhase == 5) {
    if (octopusFirmwareReady && octopusSdOta && octopusCloudRevision == remoteUpdate.revision && octopusCloudIdentity == LUMIAC_FIRMWARE_ID)
      completeRemoteUpdate();
    else if (millis() - updateSince > 120000) failRemoteUpdate("Octopus reboot was not confirmed; check its SD card");
    else if (millis() - sdSentAt > 2500) { sdSentAt = millis(); octopusSerial.println("M115"); }
    return;
  }
  if (sdWaiting) {
    const uint32_t acknowledgementMs = sdPhase == 3 ? 30000 : 10000;
    if (millis() - sdSentAt > acknowledgementMs) {
      if (++sdRetries > 4) {
        failRemoteUpdate("Octopus SD ACK timed out at " + String(remoteUpdate.done) + "/" + String(remoteUpdate.size) +
          "; stage " + remoteUpdate.phase + "; last reply: " + (sdLastReply.length() ? sdLastReply : String("none"))); return;
      }
      sdSentAt = millis(); if (sdLastCommand == "M997 P4") octopusSerial.println(); octopusSerial.println(sdLastCommand);
    }
    return;
  }
  if (sdPhase == 0) { remoteUpdate.phase = "resynchronizing Octopus UART"; sdCommand("M997 P4", "LUMIAC_FW_ABORTED"); sdPhase = 1; return; }
  if (sdPhase == 1) {
    remoteUpdate.phase = "writing SD via UART (16-byte blocks)";
    sdCommand("M997 P0 " + String(remoteUpdate.size) + " " + String(remoteUpdate.crc32), "offset", 0); sdPhase = 2; return;
  }
  if (sdPhase == 2 && sdOffset < remoteUpdate.size) {
    uint8_t bytes[lumiac::firmwareUartBlockBytes];
    const unsigned wanted = min(unsigned(sizeof(bytes)), unsigned(remoteUpdate.size - sdOffset));
    if (!octopusUpdateFile.seek(sdOffset) || octopusUpdateFile.read(bytes, wanted) != int(wanted)) { failRemoteUpdate("Staged firmware read failed"); return; }
    const uint32_t crc = lumiac::firmwareCrc(0xFFFFFFFFUL, bytes, wanted) ^ 0xFFFFFFFFUL;
    sdCommand("M997 P1 " + String(sdOffset) + " " + String(crc) + " " + otaHex(bytes, wanted), "offset", sdOffset + wanted);
    return; // Advance progress only when this exact block is acknowledged.
  }
  if (sdPhase == 2) { remoteUpdate.phase = "verifying complete SD image by readback"; sdCommand("M997 P2", "LUMIAC_FW_VERIFIED"); sdPhase = 3; return; }
  if (sdPhase == 3) {
    // Activation is sent once. Never replay a reset when its acknowledgement is lost.
    sdPhase = 5; remoteUpdate.phase = "waiting for Octopus restart";
    octopusFirmwareReady = octopusOnline = octopusSdOta = octopusHomed = false;
    octopusRealtime = octopusMotionFan = false;
    octopusCloudRevision = ""; octopusCloudIdentity = "";
    updateSince = sdSentAt = millis(); octopusSerial.println("M997 P3");
  }
}
void tickRemoteUpdate() {
  if (updateRebootAt && int32_t(millis() - updateRebootAt) >= 0) { ESP.restart(); return; }
  if (!firmwareUpdating) return;
  if (firmwareUpdateCancel) {
    if (sdPhase == 5 || updateRebootAt || otaOpenInFlight) return;
    failRemoteUpdate("Update cancelled before activation"); return;
  }
  if (remoteUpdate.state == "stopping") {
    if (updateIdleConfirmed) startFirmwareDownload();
    else if (millis() - updateSince > 20000) failRemoteUpdate("Could not confirm the Octopus has stopped");
    return;
  }
  if (remoteUpdate.state == "transferring") { tickOctopusUpdate(); return; }
  if (remoteUpdate.state != "downloading") return;
  uint8_t bytes[1024];
  const size_t wanted = min(sizeof(bytes), min(size_t(cloudHttp.getStreamPtr()->available()), size_t(remoteUpdate.size - remoteUpdate.done)));
  if (!wanted) {
    if (!cloudHttp.connected() || millis() - updateProgress > 30000) failRemoteUpdate("Download interrupted; firmware was not activated");
    return;
  }
  const int count = cloudHttp.getStreamPtr()->read(bytes, wanted);
  if (count <= 0) return;
  if (!cloudWriter.write(bytes, count)) { failRemoteUpdate(cloudWriter.error); return; }
  remoteUpdate.done += count; updateProgress = millis();
  if (remoteUpdate.done != remoteUpdate.size) return;
  closeCloudHttp();
  remoteUpdate.state = "verifying"; remoteUpdate.phase = "checking complete image";
  saveUpdateCheckpoint(true);
  if (!cloudWriter.verified()) { failRemoteUpdate(cloudWriter.error); return; }
  if (remoteUpdate.kind == "app") {
    pendingUpdateBoot = true;
    if (!saveUpdateCheckpoint(true) || !saveCloudSettings()) { failRemoteUpdate("Cannot save the reboot receipt"); return; }
  }
  if (!cloudWriter.finish()) { failRemoteUpdate(cloudWriter.error); return; }
  if (remoteUpdate.kind == "app") {
    remoteUpdate.state = "rebooting"; remoteUpdate.phase = "rebooting the ESP controller";
    updateRebootAt = millis() + 1000; return;
  }
  octopusUpdateFile = LUMIAC_FS.open("/octopus.update", "r");
  if (!octopusUpdateFile) { failRemoteUpdate("Cannot read verified Octopus firmware"); return; }
  sdOffset = 0; sdPhase = 0; sdWaiting = false; remoteUpdate.done = 0;
  remoteUpdate.state = "transferring"; remoteUpdate.phase = "writing and verifying the Octopus microSD";
}
void executeCloudAction(JsonVariantConst action) {
  const String id = action["id"] | "", type = action["type"] | "";
  if (id.isEmpty() || id == lastCloudActionId || action["session"].as<String>() != cloudBoot) return;
  lastCloudActionId = id; lastCloudActionType = type; lastCloudActionError = "";
  if (type == "stop") {
    if (firmwareUpdating) firmwareUpdateCancel = true;
    else handleSimpleAction("stop_motion", "Remote dashboard");
    return;
  }
  if (firmwareUpdating) { lastCloudActionError = "Firmware update in progress"; return; }
  if (!octopusOnline || !octopusFirmwareReady) { lastCloudActionError = "Octopus is offline"; return; }
  const bool requiresHome = type == "move" || type == "resume";
  if (requiresHome && !octopusHomed) { lastCloudActionError = "Home the arms first"; return; }
  if (type == "move") {
    JsonDocument move; move.set(action);
    handleMoveCommand(move["axes"].as<JsonObject>(), action["feed"] | currentFeedRate, "Remote dashboard"); return;
  }
  if (type == "random_loop" || type == "pause" || type == "resume") {
    handleGcodeCommand(type == "random_loop" ? "M215 S1" : type == "pause" ? "M215 P" : "M215 R", "Remote dashboard"); return;
  }
  if (!handleAction(type, action, "Remote dashboard")) lastCloudActionError = "Action rejected by the controller";
}
#include "ota_local_api.h"
void tickCloud() {
  const uint32_t now = millis();
  serviceCloudTransport();
  if (CloudExchange* reply = takeCloudExchange()) {
    cloudInFlight = false; cloudLastElapsed = reply->elapsed;
    if (reply->operation) {
      otaOpenInFlight = false;
      if (!otaDeferredError.isEmpty()) failRemoteUpdate(otaDeferredError);
      else if (!reply->error.isEmpty()) failRemoteUpdate(reply->error);
      else if (firmwareUpdateCancel) failRemoteUpdate("Update cancelled before activation");
      else if (!cloudWriter.begin(remoteUpdate.kind, remoteUpdate.size, remoteUpdate.sha256, remoteUpdate.target.c_str(), remoteUpdate.revision.c_str())) failRemoteUpdate(cloudWriter.error);
      else { remoteUpdate.state = "downloading"; remoteUpdate.phase = "downloading and checking SHA-256"; updateProgress = millis(); }
      delete reply; return;
    }
    JsonDocument packet;
    if (reply->error.isEmpty() && deserializeJson(packet, reply->response, DeserializationOption::NestingLimit(8)))
      reply->error = "Invalid gateway response";
    if (reply->error.isEmpty()) {
      if (cloudState != "connected") persistDiagnostic("Cloud connection established");
      cloudState = "connected"; cloudError = ""; cloudPollInterval = 5000;
      if (!packet["action"].isNull()) executeCloudAction(packet["action"]);
      if (!packet["update"].isNull()) startRemoteUpdate(packet["update"]);
    } else {
      if (cloudError != reply->error) persistDiagnostic("Cloud connection failed: " + reply->error);
      cloudState = "gateway unavailable"; cloudError = reply->error; ++cloudFailures;
      cloudPollInterval = min(uint32_t(60000), cloudPollInterval * 2);
    }
    delete reply; lastCloudPoll = millis();
  }
  tickNetworkConnection();
  if (cloudSsid.isEmpty() || cloudInFlight || wifiConnectionPending || WiFi.status() != WL_CONNECTED) return;
  if (time(nullptr) < 1700000000) { cloudState = "waiting for internet time"; return; }
  // OTA owns HTTPS exclusively while downloading, and only runs with motion parked.
  if (remoteUpdate.state == "opening" || remoteUpdate.state == "downloading" || updateRebootAt || (lastCloudPoll && now - lastCloudPoll < cloudPollInterval)) return;
  lastCloudPoll = now;
  if (!cloudTransportReady) { cloudError = "Cloud worker unavailable"; return; }
#if defined(ESP8266)
  if (ESP.getFreeHeap() < 28000 || ESP.getMaxFreeBlockSize() < 18000) {
#else
  if (ESP.getFreeHeap() < 96000 || ESP.getMaxAllocHeap() < 32768) {
#endif
    cloudError = "Cloud sync deferred: low free memory"; return;
  }
  CloudExchange* exchange = new (std::nothrow) CloudExchange;
  if (!exchange) { cloudError = "Cloud sync deferred: low free memory"; return; }
  {
    JsonDocument packet; fillStateDocument(packet);
    writeDiagnosticBatch(packet["diagnostics"].to<JsonArray>());
    if (packet.overflowed() || !exchange->payload.reserve(measureJson(packet) + 11)) {
      delete exchange; cloudError = "Cloud status deferred: low free memory"; return;
    }
    exchange->payload = "{\"status\":"; serializeJson(packet, exchange->payload); exchange->payload += '}';
  } // Release JSON storage before the worker can allocate TLS buffers.
  if (submitCloudExchange(exchange)) cloudInFlight = true;
  else { delete exchange; cloudError = "Cloud worker busy"; }
}

void beginCloud() {
  cloudTransportReady = configureCloudTransport();
  configureNetworkEvents();
#if defined(ESP8266)
  controllerResetReason = ESP.getResetReason();
#else
  controllerResetReason = String(unsigned(esp_reset_reason()));
#endif
  cloudBoot = WiFi.macAddress() + "-" + String(micros(), HEX) + "-" + String(random(0x7fffffff), HEX);
  localFirmwareNonce = cloudBoot + "-" + String(random(0x7fffffff), HEX);
#if defined(ESP8266)
  cloudStorageReady = LUMIAC_FS.begin();
#else
  cloudStorageReady = LUMIAC_FS.begin(true);
#endif
  if (cloudStorageReady) {
    FsDiagnosticStore diagnostics; diagnosticJournal.load(diagnostics);
    persistDiagnostic("Controller boot " + String(kControllerRole) + " " LUMIAC_FIRMWARE_REVISION + "; " + controllerResetReason);
    File file = LUMIAC_FS.open("/remote.json", "r");
    if (!file) file = LUMIAC_FS.open("/remote.backup", "r");
    if (file) {
      JsonDocument saved;
      if (!deserializeJson(saved, file)) {
        cloudSsid = saved["ssid"] | ""; cloudPassword = saved["password"] | "";
        firmwareParked = saved["parked"] | false;
        lastControlFault = saved["lastFault"] | ""; controlFaultCount = saved["faultCount"] | 0U;
        if (saved["pending"] == true) {
          remoteUpdate.id = saved["job"].as<String>(); remoteUpdate.sha256 = saved["hash"].as<String>();
          remoteUpdate.target = saved["target"].as<String>(); remoteUpdate.kind = "app";
          remoteUpdate.size = remoteUpdate.done = saved["size"] | 0U;
          remoteUpdate.state = saved["revision"].as<String>() == LUMIAC_FIRMWARE_REVISION ? "complete" : "failed";
          if (remoteUpdate.state == "failed") remoteUpdate.error = "Controller rebooted with an unexpected revision";
        }
      }
      file.close();
    }
    loadUpdateCheckpoint(); saveCloudSettings();
  }
  if (firmwareParked) cancelStartupAutomation(kBoardName, "firmware-update parked state");
  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
  if (!cloudSsid.isEmpty()) scheduleWifiConnection();
}
void handleNetworkState() { JsonDocument doc; writeRemoteState(doc); String body; serializeJson(doc, body); server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", body); }
void handleNetworkSave() {
  JsonDocument doc;
  if (server.arg("plain").length() > 1024 || deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
  if (firmwareUpdating) { server.send(409, "application/json", "{\"error\":\"Firmware update in progress\"}"); return; }
  const String ssid = doc["ssid"] | "", password = doc["password"] | "";
  if (!doc["ssid"].is<const char*>() || (!doc["password"].isNull() && !doc["password"].is<const char*>()) ||
      (!doc["openNetwork"].isNull() && !doc["openNetwork"].is<bool>())) {
    server.send(400, "application/json", "{\"error\":\"Invalid Wi-Fi settings\"}"); return;
  }
  const bool openNetwork = doc["openNetwork"] | false;
  if (const char* error = networkSettingsError(ssid, password, openNetwork)) {
    JsonDocument failure; failure["error"] = error; String body; serializeJson(failure, body);
    server.send(400, "application/json", body); return;
  }
  const String oldSsid = cloudSsid, oldPassword = cloudPassword;
  cloudSsid = ssid;
  if (openNetwork) cloudPassword = "";
  else if (!password.isEmpty()) cloudPassword = password;
  if (!saveCloudSettings()) { cloudSsid = oldSsid; cloudPassword = oldPassword; server.send(500, "application/json", "{\"error\":\"Cannot save Wi-Fi settings\"}"); return; }
  scheduleWifiConnection();
  handleNetworkState();
}
#include "network_page.h"

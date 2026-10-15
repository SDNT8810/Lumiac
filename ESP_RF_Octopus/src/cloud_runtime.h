#pragma once
// Included after the normal controller handlers, inside their namespace.
struct RemoteUpdate {
  String id, imageId, target, kind, sha256, revision, state = "idle", phase, error;
  uint32_t size = 0, done = 0, crc32 = 0;
} remoteUpdate;
String cloudBoot, cloudSsid, cloudPassword, cloudState = "not configured", cloudError;
String lastCloudActionId, lastCloudActionType, lastCloudActionError;
bool cloudStorageReady = false, pendingUpdateBoot = false;
uint32_t lastCloudPoll = 0, lastWifiAttempt = 0, updateSince = 0, updateProgress = 0, updateRebootAt = 0;
String updateIdleMarker;
bool updateIdleConfirmed = false;
OtaWriter cloudWriter;
File octopusUpdateFile;
unsigned sdPhase = 0, sdRetries = 0;
uint32_t sdOffset = 0, sdExpectedOffset = 0, sdSentAt = 0;
String sdLastCommand, sdExpectedReply;
bool sdWaiting = false;
#if defined(ESP8266)
BearSSL::WiFiClientSecure cloudTls;
BearSSL::Session cloudTlsSession;
BearSSL::X509List cloudRoots(kCloudRunRootCa);
#else
WiFiClientSecure cloudTls;
#endif
HTTPClient cloudHttp;

bool saveCloudSettings() {
  if (!cloudStorageReady) return false;
  JsonDocument doc;
  doc["ssid"] = cloudSsid; doc["password"] = cloudPassword;
  doc["parked"] = firmwareParked; doc["pending"] = pendingUpdateBoot;
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
void writeRemoteState(JsonDocument& doc) {
  doc["session"] = cloudBoot; doc["controller"] = kControllerRole;
  doc["software"] = LUMIAC_FIRMWARE_REVISION; doc["firmware"] = LUMIAC_FIRMWARE_ID;
  doc["homed"] = octopusHomed; doc["homing"] = startupHomeInProgress || embeddedPlayback.waitingForHome;
  doc["octopusSoftware"] = octopusCloudRevision;
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = cloudSsid; wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
  wifi["cloud"] = cloudState; wifi["error"] = cloudError;
  JsonObject ota = doc["ota"].to<JsonObject>();
  ota["supported"] = cloudStorageReady; ota["sdSupported"] = octopusSdOta;
  ota["active"] = firmwareUpdating; ota["parked"] = firmwareParked;
  ota["marker"] = kOtaImageMarker; ota["id"] = remoteUpdate.id;
  ota["target"] = remoteUpdate.target; ota["kind"] = remoteUpdate.kind;
  ota["state"] = remoteUpdate.state; ota["phase"] = remoteUpdate.phase;
  ota["error"] = remoteUpdate.error; ota["sha256"] = remoteUpdate.sha256;
  ota["size"] = remoteUpdate.size; ota["done"] = remoteUpdate.done;
  JsonObject action = doc["remoteAction"].to<JsonObject>();
  action["id"] = lastCloudActionId; action["type"] = lastCloudActionType; action["error"] = lastCloudActionError;
}
void closeCloudHttp() { cloudHttp.end(); cloudTls.stop(); }
bool openCloudHttp(const String& path) {
  closeCloudHttp();
#if defined(ESP8266)
  cloudTls.setTrustAnchors(&cloudRoots); cloudTls.setSession(&cloudTlsSession);
  cloudTls.setTimeout(3000);
#else
  cloudTls.setCACert(kCloudRunRootCa); cloudHttp.setConnectTimeout(2500);
#endif
  cloudHttp.setTimeout(4000); cloudHttp.setReuse(false); cloudHttp.useHTTP10(true);
  if (!cloudHttp.begin(cloudTls, String(LUMIAC_CLOUD_URL) + path)) return false;
  cloudHttp.setUserAgent("Lumiac-Controller/1.0");
  cloudHttp.addHeader("X-Lumiac-Device-Key", LUMIAC_CLOUD_DEVICE_KEY);
  cloudHttp.addHeader("Accept-Encoding", "identity");
  return true;
}
void failRemoteUpdate(const String& error) {
  cloudWriter.abort(); octopusUpdateFile.close(); closeCloudHttp();
  if (remoteUpdate.kind == "octopus" && sdPhase < 5) octopusSerial.println("M997 P4");
  remoteUpdate.state = "failed"; remoteUpdate.phase = ""; remoteUpdate.error = error;
  firmwareUpdating = false; firmwareUpdateCancel = false; pendingUpdateBoot = false;
  saveCloudSettings(); lastCloudPoll = 0;
  logMessage(kBoardName, "Firmware update failed: " + error);
}
void completeRemoteUpdate() {
  octopusUpdateFile.close(); LUMIAC_FS.remove("/octopus.update");
  remoteUpdate.state = "complete"; remoteUpdate.phase = "";
  remoteUpdate.done = remoteUpdate.size; firmwareUpdating = false;
  lastCloudPoll = 0; logMessage(kBoardName, "Firmware update complete; motion remains parked.");
}
bool handleFirmwareLine(const String& raw) {
  String line = raw; line.trim(); if (line.startsWith("echo:")) line.remove(0, 5);
  if (line == "Cap:LUMIAC_SD_OTA:1") octopusSdOta = true;
  if (line.startsWith("LUMIAC_FW_VERSION:")) octopusCloudRevision = line.substring(strlen("LUMIAC_FW_VERSION:"));
  if (line.startsWith("LUMIAC_MACHINE:")) octopusCloudIdentity = line.substring(strlen("LUMIAC_MACHINE:"));
  if (!firmwareUpdating) return false;
  if (line == updateIdleMarker) updateIdleConfirmed = true;
  if (line.startsWith("LUMIAC_FW_ERROR ")) { failRemoteUpdate(line); return true; }
  if (sdWaiting && ((sdExpectedReply == "offset" && line == "LUMIAC_FW_OFFSET " + String(sdExpectedOffset)) || line == sdExpectedReply))
    sdWaiting = false;
  return line.startsWith("LUMIAC_FW_");
}
void startRemoteUpdate(JsonVariantConst job) {
  if (firmwareUpdating || job["session"].as<String>() != cloudBoot) return;
  const String target = job["target"] | "", kind = job["kind"] | "", hash = job["sha256"] | "";
  if (!otaHashValid(hash) || job["machine"].as<String>() != LUMIAC_FIRMWARE_ID ||
      (target != kControllerRole && target != "octopus") ||
      (target == "octopus" ? kind != "octopus" || !octopusSdOta : kind != "app")) return;
  remoteUpdate = RemoteUpdate();
  remoteUpdate.id = job["id"].as<String>(); remoteUpdate.imageId = job["imageId"].as<String>();
  remoteUpdate.target = target; remoteUpdate.kind = kind; remoteUpdate.sha256 = hash;
  remoteUpdate.revision = job["revision"].as<String>(); remoteUpdate.size = job["size"] | 0U; remoteUpdate.crc32 = job["crc32"] | 0U;
  firmwareParked = true;
  if (!saveCloudSettings()) { failRemoteUpdate("Cannot persist the parked state"); return; }
  handleSimpleAction("stop_motion", "Firmware update");
  firmwareUpdating = true; firmwareUpdateCancel = false; updateIdleConfirmed = false;
  remoteUpdate.state = "stopping"; remoteUpdate.phase = "waiting for Octopus to stop";
  updateSince = millis(); sdPhase = 0; sdWaiting = false;
  updateIdleMarker = "LUMIAC_OTA_IDLE_" + remoteUpdate.id;
  octopusSerial.println(";F001"); // Preserve idle cooling while drivers hold.
  octopusSerial.println("M400"); octopusSerial.println("M118 " + updateIdleMarker);
}
bool startFirmwareDownload() {
  if (remoteUpdate.kind == "app") {
    pendingUpdateBoot = true;
    if (!saveCloudSettings()) { failRemoteUpdate("Cannot save the reboot receipt"); return false; }
  }
  if (!openCloudHttp("/device/v1/devices/" LUMIAC_CLOUD_DEVICE_ID "/firmware/" + remoteUpdate.imageId) ||
      cloudHttp.GET() != 200 || cloudHttp.getSize() != int(remoteUpdate.size)) {
    failRemoteUpdate("Firmware download rejected or length mismatch"); return false;
  }
  if (!cloudWriter.begin(remoteUpdate.kind, remoteUpdate.size, remoteUpdate.sha256)) {
    failRemoteUpdate(cloudWriter.error); return false;
  }
  remoteUpdate.state = "downloading"; remoteUpdate.phase = "downloading and checking SHA-256";
  updateProgress = millis(); return true;
}
void sdCommand(const String& line, const String& reply, uint32_t offset = 0) {
  sdLastCommand = line; sdExpectedReply = reply; sdExpectedOffset = offset;
  sdWaiting = true; sdRetries = 0; sdSentAt = millis(); octopusSerial.println(line);
}
void tickOctopusUpdate() {
  if (sdPhase == 5) {
    if (octopusFirmwareReady && octopusSdOta && octopusCloudRevision == remoteUpdate.revision && octopusCloudIdentity == LUMIAC_FIRMWARE_ID)
      completeRemoteUpdate();
    else if (millis() - updateSince > 90000) failRemoteUpdate("Octopus reboot was not confirmed; check its SD card");
    else if (millis() - sdSentAt > 2500) { sdSentAt = millis(); octopusSerial.println("M115"); }
    return;
  }
  if (sdWaiting) {
    if (millis() - sdSentAt > 2000) {
      if (++sdRetries > 4) { failRemoteUpdate("Octopus transfer acknowledgement timed out"); return; }
      sdSentAt = millis(); octopusSerial.println(sdLastCommand);
    }
    return;
  }
  if (sdPhase == 0) { sdCommand("M997 P4", "LUMIAC_FW_ABORTED"); sdPhase = 1; return; }
  if (sdPhase == 1) {
    sdCommand("M997 P0 S" + String(remoteUpdate.size) + " C" + String(remoteUpdate.crc32), "offset", 0); sdPhase = 2; return;
  }
  if (sdPhase == 2 && sdOffset < remoteUpdate.size) {
    uint8_t bytes[lumiac::firmwareBlockBytes];
    const unsigned wanted = min(unsigned(sizeof(bytes)), unsigned(remoteUpdate.size - sdOffset));
    if (!octopusUpdateFile.seek(sdOffset) || octopusUpdateFile.read(bytes, wanted) != int(wanted)) { failRemoteUpdate("Staged firmware read failed"); return; }
    const uint32_t crc = lumiac::firmwareCrc(0xFFFFFFFFUL, bytes, wanted) ^ 0xFFFFFFFFUL;
    sdCommand("M997 P1 S" + String(sdOffset) + " C" + String(crc) + " D" + otaHex(bytes, wanted), "offset", sdOffset + wanted);
    sdOffset += wanted; remoteUpdate.done = sdOffset; return;
  }
  if (sdPhase == 2) { sdCommand("M997 P2", "LUMIAC_FW_VERIFIED"); sdPhase = 3; return; }
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
    if (sdPhase == 5 || updateRebootAt) return;
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
void tickCloud() {
  const uint32_t now = millis();
  if (cloudSsid.isEmpty()) return;
  if (WiFi.status() != WL_CONNECTED) {
    cloudState = "connecting to Wi-Fi";
    if (now - lastWifiAttempt > 20000) { lastWifiAttempt = now; WiFi.begin(cloudSsid.c_str(), cloudPassword.c_str()); }
    return;
  }
  if (time(nullptr) < 1700000000) { cloudState = "waiting for internet time"; return; }
  // ESP8266 uses one TLS connection: finish its download before polling again.
  if (remoteUpdate.state == "downloading" || updateRebootAt || (lastCloudPoll && now - lastCloudPoll < 5000)) return;
  lastCloudPoll = now;
  JsonDocument packet; fillStateDocument(packet);
  String payload; serializeJson(packet, payload); packet.clear();
  payload = "{\"status\":" + payload + "}";
  if (!openCloudHttp("/device/v1/devices/" LUMIAC_CLOUD_DEVICE_ID "/sync")) { cloudError = "Cannot open remote gateway"; return; }
  cloudHttp.addHeader("Content-Type", "application/json");
  const int code = cloudHttp.POST(payload); payload = "";
  if (code != 200) {
    cloudState = "gateway unavailable"; cloudError = "HTTPS status " + String(code); closeCloudHttp(); return;
  }
  const String response = cloudHttp.getString(); closeCloudHttp();
  if (deserializeJson(packet, response)) { cloudState = "invalid gateway response"; return; }
  cloudState = "connected"; cloudError = "";
  if (!packet["action"].isNull()) executeCloudAction(packet["action"]);
  if (!packet["update"].isNull()) startRemoteUpdate(packet["update"]);
}

void beginCloud() {
  cloudBoot = WiFi.macAddress() + "-" + String(micros(), HEX) + "-" + String(random(0x7fffffff), HEX);
#if defined(ESP8266)
  cloudStorageReady = LUMIAC_FS.begin();
#else
  cloudStorageReady = LUMIAC_FS.begin(true);
#endif
  if (cloudStorageReady) {
    File file = LUMIAC_FS.open("/remote.json", "r");
    if (!file) file = LUMIAC_FS.open("/remote.backup", "r");
    if (file) {
      JsonDocument saved;
      if (!deserializeJson(saved, file)) {
        cloudSsid = saved["ssid"] | ""; cloudPassword = saved["password"] | "";
        firmwareParked = saved["parked"] | false;
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
    saveCloudSettings();
  }
  if (firmwareParked) cancelStartupAutomation(kBoardName, "firmware-update parked state");
  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
  if (!cloudSsid.isEmpty()) { WiFi.begin(cloudSsid.c_str(), cloudPassword.c_str()); lastWifiAttempt = millis(); }
}
void handleNetworkState() { JsonDocument doc; writeRemoteState(doc); String body; serializeJson(doc, body); server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", body); }
void handleNetworkSave() {
  JsonDocument doc;
  if (server.arg("plain").length() > 1024 || deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
  if (firmwareUpdating) { server.send(409, "application/json", "{\"error\":\"Firmware update in progress\"}"); return; }
  const String ssid = doc["ssid"] | "", password = doc["password"] | "";
  if (!doc["ssid"].is<const char*>() || ssid.isEmpty() || ssid.length() > 32 || (!password.isEmpty() && (password.length() < 8 || password.length() > 63))) {
    server.send(400, "application/json", "{\"error\":\"Use a Wi-Fi name up to 32 bytes and a password of 8-63 characters\"}"); return;
  }
  const String oldSsid = cloudSsid, oldPassword = cloudPassword;
  cloudSsid = ssid; cloudPassword = password;
  if (!saveCloudSettings()) { cloudSsid = oldSsid; cloudPassword = oldPassword; server.send(500, "application/json", "{\"error\":\"Cannot save Wi-Fi settings\"}"); return; }
  cloudState = "connecting to Wi-Fi"; cloudError = ""; WiFi.begin(cloudSsid.c_str(), cloudPassword.c_str()); lastWifiAttempt = millis();
  handleNetworkState();
}
void handleNetworkPage() {
  static const char page[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lumiac Internet connection</title><style>body{font:17px system-ui;max-width:600px;margin:40px auto;padding:20px;background:#e9e4da;color:#263831}label,input,button{display:block;margin:12px 0}input,button{padding:12px;font:inherit}input{width:90%}a{color:inherit}</style>
<h1>Lumiac remote access</h1><p>Connect this controller to a 2.4 GHz router. The Lumiac local Wi-Fi remains available.</p>
<form id="network"><label>Wi-Fi name<input id="ssid" maxlength="32" required></label><label>Wi-Fi password<input id="password" type="password" maxlength="63" autocomplete="new-password"></label><button>Save and connect</button></form><p id="state" role="status"></p>
<p>Remote panel: <a href="https://lumiac.control.timiac.it">lumiac.control.timiac.it</a></p><a href="/">Back to dashboard</a>
<script>const $=id=>document.getElementById(id);async function refresh(){try{const r=await fetch('/api/network',{cache:'no-store'}),s=await r.json();$('state').textContent=(s.wifi.connected?'Router connected. ':'Router disconnected. ')+(s.wifi.cloud||'')+(s.wifi.error?' - '+s.wifi.error:'');if(!$('ssid').value)$('ssid').value=s.wifi.ssid||'';}catch(e){$('state').textContent=e.message;}}$('network').onsubmit=async e=>{e.preventDefault();try{const r=await fetch('/api/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:$('ssid').value,password:$('password').value})}),s=await r.json();if(!r.ok)throw Error(s.error);$('password').value='';await refresh();}catch(e){$('state').textContent=e.message;}};refresh();setInterval(refresh,3000);</script></html>)HTML";
  server.send_P(200, "text/html; charset=utf-8", page);
}

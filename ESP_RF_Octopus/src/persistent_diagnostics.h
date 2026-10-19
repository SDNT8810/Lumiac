#pragma once

lumiac::DiagnosticJournal diagnosticJournal;
uint32_t diagnosticWriteFailures = 0;
struct FsDiagnosticStore {
  bool read(unsigned slot, lumiac::DiagnosticEntry& entry) {
    File file = LUMIAC_FS.open("/diagnostics.bin", "r");
    return file && file.seek(slot * sizeof(entry)) && file.read(reinterpret_cast<uint8_t*>(&entry), sizeof(entry)) == int(sizeof(entry));
  }
  bool write(unsigned slot, const lumiac::DiagnosticEntry& entry) {
    if (!LUMIAC_FS.exists("/diagnostics.bin")) {
      File file = LUMIAC_FS.open("/diagnostics.bin", "w");
      if (!file) return false;
      lumiac::DiagnosticEntry empty = {};
      for (unsigned i = 0; i < lumiac::DiagnosticJournal::capacity; ++i)
        if (file.write(reinterpret_cast<const uint8_t*>(&empty), sizeof(empty)) != sizeof(empty)) return false;
      file.flush(); file.close();
    }
    File file = LUMIAC_FS.open("/diagnostics.bin", "r+");
    if (!file || !file.seek(slot * sizeof(entry))) return false;
    const bool ok = file.write(reinterpret_cast<const uint8_t*>(&entry), sizeof(entry)) == sizeof(entry);
    file.flush(); return ok;
  }
};
void persistDiagnostic(const String& text) {
  if (!cloudStorageReady) return;
  FsDiagnosticStore store;
  const time_t utc = time(nullptr);
  if (!diagnosticJournal.append(store, cloudBoot.c_str(), millis(), utc > 1700000000 ? uint32_t(utc) : 0, text.c_str())) ++diagnosticWriteFailures;
}
void writeDiagnosticBatch(JsonArray events, bool all = false) {
  static unsigned cursor = 0;
  for (unsigned n = 0; n < (all ? lumiac::DiagnosticJournal::capacity : 2U); ++n) {
    const unsigned slot = (all ? n : cursor++) % lumiac::DiagnosticJournal::capacity;
    lumiac::DiagnosticEntry record;
    if (lumiac::DiagnosticJournal::cached) record = diagnosticJournal.entries[slot];
    else {
      FsDiagnosticStore store;
      if (!store.read(slot, record) || record.magic != 0x54444A31 ||
          record.text[sizeof(record.text)-1] || record.boot[sizeof(record.boot)-1] ||
          record.checksum != lumiac::DiagnosticJournal::checksum(record)) continue;
    }
    if (!record.sequence) continue;
    JsonObject event = events.add<JsonObject>();
    event["id"] = "p" + String(record.sequence); event["boot"] = record.boot;
    event["sequence"] = record.sequence; event["at"] = record.uptime; event["utc"] = record.utc;
    event["text"] = record.text; event["persistent"] = true;
  }
}
bool saveUpdateCheckpoint(bool force = false) {
  static String state, phase, id;
  static uint32_t done = 0, savedAt = 0;
  if (!cloudStorageReady || remoteUpdate.id.isEmpty()) return false;
  if (!force && state == remoteUpdate.state && phase == remoteUpdate.phase && id == remoteUpdate.id &&
      remoteUpdate.done - done < 65536 && (remoteUpdate.done == done || millis() - savedAt < 10000)) return true;
  JsonDocument doc;
  doc["id"] = remoteUpdate.id; doc["target"] = remoteUpdate.target; doc["kind"] = remoteUpdate.kind;
  doc["sha256"] = remoteUpdate.sha256; doc["revision"] = remoteUpdate.revision;
  doc["size"] = remoteUpdate.size; doc["done"] = remoteUpdate.done;
  doc["state"] = remoteUpdate.state; doc["phase"] = remoteUpdate.phase; doc["error"] = remoteUpdate.error;
  doc["boot"] = cloudBoot; doc["at"] = millis(); doc["activationPending"] = pendingUpdateBoot;
  File file = LUMIAC_FS.open("/ota-checkpoint.tmp", "w");
  if (!file) { ++diagnosticWriteFailures; return false; }
  const bool written = serializeJson(doc, file) == measureJson(doc); file.flush(); file.close();
  if (!written) { ++diagnosticWriteFailures; return false; }
  LUMIAC_FS.remove("/ota-checkpoint.backup");
  if (LUMIAC_FS.exists("/ota-checkpoint.json") && !LUMIAC_FS.rename("/ota-checkpoint.json", "/ota-checkpoint.backup")) { ++diagnosticWriteFailures; return false; }
  if (!LUMIAC_FS.rename("/ota-checkpoint.tmp", "/ota-checkpoint.json")) {
    LUMIAC_FS.rename("/ota-checkpoint.backup", "/ota-checkpoint.json"); ++diagnosticWriteFailures; return false;
  }
  if (state != remoteUpdate.state || phase != remoteUpdate.phase || id != remoteUpdate.id)
    persistDiagnostic("OTA " + remoteUpdate.target + " " + remoteUpdate.state + " " + remoteUpdate.phase + " " + remoteUpdate.error);
  state = remoteUpdate.state; phase = remoteUpdate.phase; id = remoteUpdate.id; done = remoteUpdate.done; savedAt = millis();
  return true;
}
bool installedAppMatches(uint32_t size, const String& expected) {
  if (!size || !otaHashValid(expected)) return false;
  OtaSha256 hash; hash.begin(); uint32_t bytes[128];
#if defined(ESP32)
  const esp_partition_t* partition = esp_ota_get_running_partition();
  if (!partition || size > partition->size) return false;
#else
  if (size > 0x100000) return false;
#endif
  for (uint32_t offset = 0; offset < size;) {
    const uint32_t count = min(uint32_t(sizeof(bytes)), size - offset);
#if defined(ESP32)
    if (esp_partition_read(partition, offset, bytes, count) != ESP_OK) return false;
#else
    if (!ESP.flashRead(offset, bytes, (count + 3) & ~3U)) return false;
#endif
    hash.write(reinterpret_cast<const uint8_t*>(bytes), count); offset += count; yield();
  }
  return otaEqual(hash.finish(), expected);
}
void loadUpdateCheckpoint() {
  File file = LUMIAC_FS.open("/ota-checkpoint.json", "r");
  if (!file) file = LUMIAC_FS.open("/ota-checkpoint.backup", "r");
  if (!file) return;
  JsonDocument saved;
  if (deserializeJson(saved, file)) return;
  remoteUpdate.id = saved["id"] | ""; remoteUpdate.target = saved["target"] | "";
  remoteUpdate.kind = saved["kind"] | ""; remoteUpdate.sha256 = saved["sha256"] | "";
  remoteUpdate.revision = saved["revision"] | ""; remoteUpdate.size = saved["size"] | 0U;
  remoteUpdate.done = saved["done"] | 0U; remoteUpdate.state = saved["state"] | "failed";
  remoteUpdate.error = saved["error"] | "";
  if (remoteUpdate.state != "complete" && remoteUpdate.state != "failed") {
    const bool activated = saved["activationPending"] == true && remoteUpdate.target == kControllerRole &&
      remoteUpdate.revision == LUMIAC_FIRMWARE_REVISION && installedAppMatches(remoteUpdate.size, remoteUpdate.sha256);
    remoteUpdate.state = activated ? "complete" : "failed";
    remoteUpdate.error = activated ? "" : "Update interrupted by controller restart; no automatic retry";
    if (activated) remoteUpdate.done = remoteUpdate.size;
    firmwareParked = true;
  }
  pendingUpdateBoot = false; remoteUpdate.phase = ""; saveUpdateCheckpoint(true);
}
void handleDiagnostics() {
  if (firmwareUpdating || cloudInFlight || ESP.getFreeHeap() < 24000) {
    server.send(503, "application/json", "{\"error\":\"Controller busy; retry diagnostics shortly\"}"); return;
  }
  JsonDocument doc; writeRemoteState(doc);
  writeDiagnosticBatch(doc["recentEvents"].to<JsonArray>(), true);
  String body; serializeJsonPretty(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "attachment; filename=lumiac-diagnostics.json");
  server.send(200, "application/json", body);
}

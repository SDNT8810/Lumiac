#pragma once
#include "firmware_transfer.h"
#include <stddef.h>

namespace lumiac {
struct DiagnosticEntry {
  uint32_t magic, sequence, uptime, utc;
  char boot[40], text[180];
  uint32_t checksum;
};
// Checksummed fixed slots. A torn write loses at most one event. Twelve slots
// bound the RAM footprint on ESP-12S; cloud history retains received events.
struct DiagnosticJournal {
  static constexpr unsigned capacity = 12;
#if defined(ARDUINO_ARCH_ESP8266)
  static constexpr bool cached = false;
#else
  static constexpr bool cached = true;
#endif
  // ESP-12S keeps history on flash and reads individual slots on demand, so
  // logging does not take RAM away from BearSSL's handshake buffers.
  DiagnosticEntry entries[cached ? capacity : 1] = {};
  uint32_t sequence = 0;
  static uint32_t checksum(const DiagnosticEntry& entry) {
    return firmwareCrc(0xFFFFFFFFUL, reinterpret_cast<const uint8_t*>(&entry), offsetof(DiagnosticEntry, checksum)) ^ 0xFFFFFFFFUL;
  }
  template<class Store> void load(Store& store) {
    sequence = 0;
    for (unsigned i = 0; i < capacity; ++i) {
      DiagnosticEntry entry = {};
      if (store.read(i, entry) && entry.magic == 0x54444A31 && entry.sequence &&
          entry.boot[sizeof(entry.boot)-1] == 0 && entry.text[sizeof(entry.text)-1] == 0 && entry.checksum == checksum(entry)) {
        if (cached) entries[i] = entry;
        if (entry.sequence > sequence) sequence = entry.sequence;
      } else if (cached) entries[i] = DiagnosticEntry{};
    }
  }
  template<class Store> bool append(Store& store, const char* boot, uint32_t uptime, uint32_t utc, const char* text) {
    DiagnosticEntry entry = {};
    entry.magic = 0x54444A31; entry.sequence = sequence + 1; entry.uptime = uptime; entry.utc = utc;
    strncpy(entry.boot, boot, sizeof(entry.boot)-1); strncpy(entry.text, text, sizeof(entry.text)-1);
    entry.checksum = checksum(entry);
    const unsigned slot = (entry.sequence - 1) % capacity;
    if (!store.write(slot, entry)) return false;
    if (cached) entries[slot] = entry;
    sequence = entry.sequence; return true;
  }
};
}

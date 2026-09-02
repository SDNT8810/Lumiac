/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../../inc/MarlinConfig.h"

#if HAS_MEDIA && ENABLED(SPIDER_SD_FILE_CODES)

#include "../gcode.h"
#include "../parser.h"
#include "../queue.h"
#include "../../MarlinCore.h"
#include "../../module/endstops.h"
#include "../../sd/cardreader.h"
#include "../../module/motion.h"
#include "../../module/printcounter.h"

struct spider_sd_file_code_t {
  uint16_t code;
  const char *path;
};

#define SPIDER_SD_FILE_CODE(code, path) { code, path },
static constexpr spider_sd_file_code_t spider_sd_file_codes[] = {
  SPIDER_SD_FILE_CODES_LIST
};
#undef SPIDER_SD_FILE_CODE

static bool spider_sd_compat_path(const char * const path, char * const out, const size_t out_size) {
  if (!path || !out || out_size < 8) return false;

  if (strncmp(path, "/gcodes/", 8) == 0) {
    snprintf(out, out_size, "/gcode/%s", path + 8);
    return true;
  }

  if (strncmp(path, "/gcode/", 7) == 0) {
    snprintf(out, out_size, "/gcodes/%s", path + 7);
    return true;
  }

  return false;
}

static void list_spider_sd_file_codes() {
  SERIAL_ECHOLNPGM("Spider SD file codes:");
  SERIAL_ECHOPGM("  M215 H -> ");
  SERIAL_ECHOLNPGM("standard G28 X Y Z A B C");
  SERIAL_ECHOPGM("  M215 P1 -> ");
  SERIAL_ECHOLNPGM(SPIDER_SD_POS1_FILE);
  SERIAL_ECHOPGM("  M215 P2 -> ");
  SERIAL_ECHOLNPGM(SPIDER_SD_POS2_FILE);
  SERIAL_ECHOPGM("  M215 P3 -> ");
  SERIAL_ECHOLNPGM(SPIDER_SD_POS3_FILE);
  for (const auto &shortcut : spider_sd_file_codes) {
    SERIAL_ECHOPGM("  M215 S");
    SERIAL_ECHO(shortcut.code);
    SERIAL_ECHOPGM(" -> ");
    SERIAL_ECHOLN(shortcut.path);
  }
  SERIAL_ECHOLNPGM("  M215 P -> pause active spider SD file");
  SERIAL_ECHOLNPGM("  M215 R -> resume paused spider SD file");
  SERIAL_ECHOLNPGM("  M215 X -> abort active spider SD file");
}

static bool spider_sd_job_active() {
  return card.isPrinting() || card.isPaused();
}

static void spider_sd_pause() {
  if (!card.isPrinting()) {
    SERIAL_ECHOLNPGM("No active spider SD file to pause.");
    return;
  }

  card.pauseSDPrint();
  print_job_timer.pause();
  SERIAL_ECHOLNPGM("Paused spider SD file.");
}

static void spider_sd_resume() {
  if (!card.isPaused()) {
    SERIAL_ECHOLNPGM("No paused spider SD file to resume.");
    return;
  }

  card.startOrResumeFilePrinting();
  startOrResumeJob();
  SERIAL_ECHOLNPGM("Resumed spider SD file.");
}

static void spider_sd_abort() {
  if (!spider_sd_job_active()) {
    SERIAL_ECHOLNPGM("No active spider SD file to abort.");
    return;
  }

  card.abortFilePrintNow(TERN_(SD_RESORT, true));
  card.setInteractiveJob(false);
  queue.clear();
  quickstop_stepper();
  print_job_timer.abort();
  SERIAL_ECHOLNPGM("Aborted spider SD file.");
}

static bool start_spider_sd_path(const char * const path, const char * const label, const bool loop=false) {
  const char *resolved_path = path;
  char compat_path[64] = { 0 };

  if (!card.isMounted()) {
    SERIAL_ECHOLNPGM("No SD card mounted.");
    return false;
  }

  if (spider_sd_job_active()) {
    SERIAL_ECHOLNPGM("Spider SD file already active. Use M215 P, M215 R, or M215 X first.");
    return false;
  }

  card.openFileRead(resolved_path);

  if (!card.isFileOpen() && spider_sd_compat_path(path, compat_path, sizeof(compat_path))) {
    resolved_path = compat_path;
    card.openFileRead(resolved_path);
  }

  if (!card.isFileOpen()) {
    SERIAL_ECHOPGM("Spider SD file missing: ");
    SERIAL_ECHOLN(path);
    return false;
  }

  // Mark the file as an interactive spider job after the open, because openFileRead()
  // clears any previous print state internally.
  card.configureInteractiveJob(true, loop, resolved_path);
  card.startOrResumeFilePrinting();
  startOrResumeJob();

  SERIAL_ECHOPGM("Running spider ");
  SERIAL_ECHOPGM(label);
  SERIAL_ECHOPGM(": ");
  SERIAL_ECHOLN(resolved_path);
  return true;
}

static void run_standard_spider_home() {
  GcodeSuite::process_subcommands_now(F("G28 X Y Z A B C"));
}

static void ensure_spider_homed() {
  if (!homing_needed()) return;
  SERIAL_ECHOLNPGM("Spider not homed. Running standard spider home first.");
  run_standard_spider_home();
}

static void apply_spider_motion_tuning() {
  // Re-apply the spider motion profile for every preset / random run so saved EEPROM
  // values or prior tuning commands don't bring back harsh direction changes.
  GcodeSuite::process_subcommands_now(F("M201 X50 Y50 Z50 A50 B50 C50"));
  GcodeSuite::process_subcommands_now(F("M204 P15 T15"));
}

void GcodeSuite::M215() {
  if (!card.isMounted()) card.mount();

  if (parser.seen_test('H')) {
    SERIAL_ECHOLNPGM("Starting spider standard homing.");
    apply_spider_motion_tuning();
    run_standard_spider_home();
    SERIAL_ECHOLNPGM("Spider homing complete.");
    return;
  }

  if (parser.seenval('P')) {
    const uint16_t preset = parser.value_ushort();
    switch (preset) {
      case 1:
        ensure_spider_homed();
        apply_spider_motion_tuning();
        start_spider_sd_path(SPIDER_SD_POS1_FILE, "preset P1");
        return;
      case 2:
        ensure_spider_homed();
        apply_spider_motion_tuning();
        start_spider_sd_path(SPIDER_SD_POS2_FILE, "preset P2");
        return;
      case 3:
        ensure_spider_homed();
        apply_spider_motion_tuning();
        start_spider_sd_path(SPIDER_SD_POS3_FILE, "preset P3");
        return;
      default:
        SERIAL_ECHOPGM("Unknown spider preset: P");
        SERIAL_ECHOLN(preset);
        list_spider_sd_file_codes();
        return;
    }
  }

  if (parser.seen_test('P')) {
    spider_sd_pause();
    return;
  }

  if (parser.seen_test('R')) {
    spider_sd_resume();
    return;
  }

  if (parser.seen_test('X')) {
    spider_sd_abort();
    return;
  }

  if (parser.seen_test('L') || !parser.seenval('S')) {
    list_spider_sd_file_codes();
    return;
  }

  const uint16_t code = parser.value_ushort();

  for (const auto &shortcut : spider_sd_file_codes) {
    if (shortcut.code != code) continue;
    char label[24];
    snprintf(label, sizeof(label), "file code %u", code);
    apply_spider_motion_tuning();
    start_spider_sd_path(shortcut.path, label, true);
    return;
  }

  SERIAL_ECHOPGM("Unknown spider file code: ");
  SERIAL_ECHOLN(code);
  list_spider_sd_file_codes();
}

#endif // HAS_MEDIA && SPIDER_SD_FILE_CODES

# Lumiac Satechi Bluetooth Media Button Monitor

Target: **original ESP32 / ESP32-WROOM-32 family with Bluetooth Classic**.

This project makes the ESP32:

- Wi-Fi hotspot: **Lumiac**
- Wi-Fi password: **12345678**
- Dashboard: **http://192.168.4.1**
- Bluetooth Classic HID host for the Satechi ST-BMB / POP Multimedia media button
- Browser scan/connect/disconnect controls
- Live connection status
- Five live indicators: volume up/down, next, back, play/pause
- Button-down, short press, hold, and hold-release events with durations
- Retained button history and per-button press/hold counts
- Raw HID bytes and descriptor diagnostics
- Rolling event log

## Recommended ESP-IDF

This project targets **ESP-IDF v5.3.1**. Install the ESP32 tools and open the
ESP-IDF terminal so that `IDF_PATH`, Python, and the compiler are configured.

## Windows launcher

With PlatformIO installed, open Command Prompt and run:

```bat
cd /d C:\Users\davoud\Desktop\blu
FLASH_WINDOWS.bat COM5
```

Replace `COM5` with the board's serial port (shown in Device Manager under
**Ports (COM & LPT)**). The script builds, flashes, and opens the serial monitor.
The launcher uses ESP-IDF when run from an **ESP-IDF Command Prompt**; otherwise
it uses PlatformIO. PlatformIO downloads the required tools on its first build.
Its configuration pins Espressif32 platform 6.9.0, which supplies ESP-IDF 5.3.1,
and defaults to COM5. Press **Ctrl+C** to exit the PlatformIO monitor, or
**Ctrl+]** to exit the ESP-IDF monitor.

PlatformIO commands can also be run directly:

```bat
pio run -e esp32dev
pio run -e esp32dev -t upload --upload-port COM5
pio device monitor -e esp32dev --port COM5
```

On Linux/macOS, activate ESP-IDF and run
`bash FLASH_LINUX_MAC.sh /dev/ttyUSB0`, using your board's port.

## Project files

- `CMakeLists.txt`: ESP-IDF project definition.
- `main/CMakeLists.txt`: component registration and dependencies.
- `main/main.c`: firmware source.
- `main/media_keys.c`: HID consumer report decoder and press/hold tracking.
- `main/dashboard.html`: dashboard, embedded automatically by CMake.
- `sdkconfig.defaults`: Bluetooth, partition, and HTTP defaults.
- `platformio.ini`: ESP-IDF 5.3.1 build and COM5 upload/monitor configuration.
- `FLASH_WINDOWS.bat` / `FLASH_LINUX_MAC.sh`: build and flash launchers.

The original files had mismatched names and contents. They are preserved in
`original-files-20260924-150350/` for reference.

## Fast build / flash

1. Install/open ESP-IDF v5.3.1.
2. Open an ESP-IDF terminal in this project folder.
3. Run:

```bash
idf.py set-target esp32
idf.py build
idf.py -p YOUR_PORT flash monitor
```

Examples for `YOUR_PORT`:

- Windows: `COM5`
- Linux: `/dev/ttyUSB0`
- macOS: `/dev/cu.usbserial-...`

After boot:

1. On the phone/PC, join Wi-Fi **Lumiac** using **12345678**.
2. Open **http://192.168.4.1**.
3. Put the Satechi remote in pairing mode.
4. Tap **Scan**.
5. Find `POP Multimedia` (or the Satechi-looking device) and tap **Connect**.
6. Press the media buttons. The dashboard shows the friendly key guess and the exact raw HID bytes.

## Pairing notes

The firmware accepts SSP confirmation automatically and replies `0000` to a legacy four-digit PIN request. If the Satechi is still paired with a phone/computer, forget/unpair it there first and re-enter pairing mode before scanning from the ESP32.

For the Satechi ST-BMB, hold the pairing button for about three seconds until the
blue LED blinks ([Satechi guide](https://support.satechi.com/hc/en-us/articles/39672554567963-Quick-Start-Guide-Bluetooth-Media-Button-ST-BMB)).
Select **Connect** beside the remote, which may initially appear as **Unknown**.
The ESP32 stops discovery before starting the HID connection. The dashboard
shows connection progress, pairing failures, and a 30-second connection timeout.
Scanning is disabled while connected or connecting; disconnect before scanning again.

## Presses and holds

- **DOWN** appears on the live indicator as soon as a mapped button-down report arrives.
- **PRESS** is recorded on release before 650 ms.
- The live indicator switches to **HOLD** at 650 ms, even if no repeat packets arrive.
- On release, history records one **HOLD** with the full duration.
- Disconnecting cancels active keys. A key missing its release for 15 seconds is
  marked **CANCELLED**, rather than remaining stuck on screen.

History stores one result per completed gesture, without intermediate DOWN or
hold-threshold entries. The last 40 results survive dashboard polling, so quick taps remain visible.
Counts and history reset when the ESP32 reboots. Detection uses the remote's HID
descriptor (consumer bitfields or arrays, including report IDs). It does not
guess a key by searching unrelated bytes. If a remote sends only instantaneous
clicks, its physical hold duration cannot be recovered from those clicks; the
dashboard reports the events actually transmitted. Fast-forward/rewind usages
are displayed under Next/Back.

## Hardware compatibility

Works with chips/modules that have Bluetooth Classic, especially ESP32-WROOM-32 / original ESP32.

It will **not** provide this Classic Bluetooth HID host function on BLE-only variants such as ESP32-C3, ESP32-C6, and ESP32-S3.

## If a button is not named correctly

Check **Capture details** and the connection/report log. These include raw HID,
report counts, the descriptor bytes, and the number of mapped media fields.
Unsupported report formats are explicitly shown; keep those diagnostics when
adding support for a different remote.

## Verification

Portable decoder/state-machine tests run on Windows with a host C compiler:

```powershell
New-Item -ItemType Directory .test-tools -Force
uv run --with ziglang python -m ziglang cc -std=c11 -Wall -Wextra -Werror -Imain main/media_keys.c tests/test_media_keys.c -o .test-tools/test_media_keys.exe
.\.test-tools\test_media_keys.exe
npm install --prefix .test-tools --no-package-lock --no-save jsdom@22.1.0
node tests/test_dashboard.cjs
```

The dashboard tests check actual DOM button events, encoded addresses, API error
display, retained taps, hold/release indicators, and safe rendering of device names.

# Lumiac

Lumiac is the control stack for a six-axis hexapod/spider robot built around a BIGTREETECH Octopus v1.1 and an ESP-12S or ESP32 controller. It includes customized Marlin firmware, a Wi-Fi/RF bridge, motion programs embedded in ESP flash, a browser control panel, and a local motion simulator.

> **Hardware warning:** This project controls high-current stepper drivers and moving machinery. Test with the robot raised or the motors disconnected, keep an emergency power cut-off within reach, and verify every pin and current limit before applying motor power.

## What is included

| Path | Purpose |
| --- | --- |
| [`ESP_RF_Octopus`](ESP_RF_Octopus) | ESP-12S / ESP32 firmware: Wi-Fi access point, web UI, RF/button inputs, and UART bridge to Marlin |
| [`Marlin/marlin-2.1.2.6`](Marlin/marlin-2.1.2.6) | Customized Marlin 2.1.2.6 for the BTT Octopus v1.1 and six TMC5160 axes |
| [`gcodes`](gcodes) | Motion and pose programs embedded in ESP firmware at build time |
| [`WebApp`](WebApp) | Local browser UI that reads the configured Marlin motion-file map |
| [`simulator`](simulator) | Standalone browser-based hexapod motion simulator |
| [`Docs/pinMapping`](Docs/pinMapping) | Wiring, pin, driver, endstop, lamp, and command reference |
| [`build.py`](build.py) | Builds/uploads either ESP controller, or builds Marlin and copies its firmware to SD; `build.pt` is an alias |

`gcodes_OLD` is retained as reference material. The much larger upstream Marlin configuration-example bundle is intentionally not published because Lumiac does not use it.

## Requirements

- Python 3.9 or newer
- Git
- Node.js 18 or newer, only for the local WebApp and simulator
- A data-capable USB cable and the correct serial-port driver for your board
- For the complete hardware build: BTT Octopus v1.1, ESP-12S (ESP8266, 4 MB flash) or ESP32 DevKit, six correctly configured TMC5160 drivers, endstops, and a FAT32-formatted SD card for the initial Octopus firmware flash
- For ESP-12S flashing: a USB-to-UART programmer with 3.3 V logic and suitable power/boot wiring; the module has no native USB

PlatformIO is pinned in [`requirements.txt`](requirements.txt), so no separate PlatformIO installation is required.

## Quick start

Clone the repository and create an isolated Python environment:

```bash
git clone https://github.com/SDNT8810/Lumiac.git
cd Lumiac
python -m venv .venv
```

Activate it on Windows PowerShell:

```powershell
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

Or activate it on Linux/macOS:

```bash
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

## Build and upload an ESP controller

ESP-12S replaces the complete ESP32 controller. They are alternatives, not a pair of wireless boards. See [controller wiring and ESP-12S flashing](Docs/esp-controllers.md) before swapping hardware.

Build and upload to the specified serial port:

```bash
python build.py esp12 COM5
python build.py esp32 COM4
```

The requested filename and spelling also work:

```bash
python build.pt eps12 COM5
python build.pt eps32 COM4
```

Omit the port for a build without upload, or add `--build-only`:

```bash
python build.py esp12
python build.py esp32
python build.py esp12 COM5 --build-only
```

The PlatformIO environments are `esp12s` (ESP8266) and `esp32dev`. Their binaries are under `ESP_RF_Octopus/.pio/build/<environment>/firmware.bin`. Each ESP build embeds the current `gcodes/input*.txt` and `gcodes/pos*.txt` files automatically. Editing a motion program requires rebuilding and uploading the chosen ESP firmware; no separate filesystem upload or Octopus SD copy is needed. The helper returns a nonzero exit status on a failed build/upload.

After boot, connect a phone or computer to the selected controller's access point:

- SSID: `ESP_RF_Octopus`
- Default password: `octopus123`
- Control page: <http://192.168.4.1>

The access-point credentials are defaults stored in [`ESP_RF_Octopus/src/main.cpp`](ESP_RF_Octopus/src/main.cpp). Change the password before deploying the robot in a public or shared location.

ESP32 USB logs remain available at 115200 baud. ESP-12S logs are available through the web interface: its UART pins are dedicated to Marlin during normal operation.

## Build and flash the Octopus firmware

The root helper builds the `STM32F446ZE_btt` environment:

```bash
python build.py
```

`python build.py marlin --build-only` compiles Marlin without modifying an SD card. Running the helper without arguments retains the original build-and-copy behavior.

The optional `python build.pt marlin-max --build-only` builds the **256-microstep / 3.0 A RMS test profile**, with separate output at `Marlin/marlin-2.1.2.6/.pio/build/STM32F446ZE_btt_max_power/firmware.bin`. The normal build also uses 256 microsteps, with 2.5 A RMS run current. See [driver tuning](Docs/driver-tuning.md) for cooling requirements and the motion settings.

On Windows it also looks for an SD card at `E:\`. To use another mounted path, set `OCTOPUS_SD_ROOT` before running it:

```powershell
$env:OCTOPUS_SD_ROOT = "F:\"
python .\build.py
```

```bash
OCTOPUS_SD_ROOT=/media/$USER/OCTOPUS python build.py
```

If the SD-card path is not present, the build still completes and the firmware remains at:

```text
Marlin/marlin-2.1.2.6/.pio/build/STM32F446ZE_btt/firmware.bin
```

To flash the board:

1. Format an SD card as FAT32.
2. Copy the generated file to the SD-card root as `firmware.bin`.
3. Turn off the Octopus board, insert the card, and power it on.
4. Confirm that the board renamed `firmware.bin` to `FIRMWARE.CUR`.
5. Turn off the Octopus, remove the SD card, and power it on again. The ESP supplies the motion programs over UART.

This is not stock Marlin. It contains the required six-axis configuration. Its legacy `M215` SD-file commands still require an SD card when sent directly to the Octopus USB port; the ESP handles dashboard, RF, and ESP-terminal `M215` commands locally and streams ordinary G-code to Marlin.

## Run the browser tools

The Node.js projects currently have no third-party packages, so they can be started directly.

Local control/WebApp:

```bash
cd WebApp
npm start
```

Open <http://localhost:3000>. This app expects the customized Marlin tree to be available because it reads the `M215` file mapping from `Configuration_adv.h`.

Motion simulator:

```bash
cd simulator
npm start
```

Open <http://localhost:3000>. If the WebApp is already using that port, start the simulator on another port.

Windows PowerShell:

```powershell
$env:PORT = "3001"
npm start
```

Linux/macOS:

```bash
PORT=3001 npm start
```

## Hardware overview

- The selected ESP controller communicates with the Octopus over a 115200-baud UART link.
- The Octopus firmware exposes six motion axes as `X Y Z A B C`.
- The ESP handles `M215` selections and pause/resume/stop controls for programs stored in its flash, then streams G-code to the Octopus over UART. The Octopus needs no SD card for normal operation through the ESP.
- `M355` controls the lamp through the Octopus bed/heater MOSFET output; this configuration does not use a heated bed.
- The controller's Octopus reset connection is open-drain/active-low. ESP-12S boot/programming constraints are described in [the wiring guide](Docs/esp-controllers.md).

See [`Docs/pinMapping`](Docs/pinMapping) before connecting any hardware. Treat the firmware source as the final authority if documentation and code differ.

## Troubleshooting

For weak movement, noisy random programs, or hot drivers, see [driver cooling and motion tuning](Docs/driver-tuning.md). It includes the normal and higher-current build profiles, microstep comparisons up to 256, and the quiet random-motion acceleration profile.

- **`No module named platformio`:** activate the virtual environment and run `python -m pip install -r requirements.txt`.
- **Marlin project not found:** confirm that `Marlin/marlin-2.1.2.6/platformio.ini` exists.
- **Upload port not found:** replace `COM4` with the port shown by `python -m platformio device list`.
- **WebApp reports a motion-map error:** confirm the customized Marlin source and `gcodes` directory are both present.
- **Octopus does not rename the firmware:** use a small FAT32 SD card, ensure the filename is exactly `firmware.bin`, and fully power-cycle the board.

## Upstream source and licensing

The customized Marlin tree is vendored in this repository so every clone contains the required six-axis and `M215` changes. It is based on [Marlin 2.1.2.6](https://github.com/MarlinFirmware/Marlin) and retains Marlin's upstream license and copyright notices.

The original Lumiac components do not yet have a separate project-level license. Add one before accepting outside contributions or redistribution.

## Contributing

Bug reports and pull requests are welcome. When reporting a hardware issue, include the board revision, driver type, power supply voltage, wiring changes, relevant serial logs, and the exact command that triggered the problem. Do not include Wi-Fi credentials, tokens, or other secrets.

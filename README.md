# Lumiac

Lumiac is the control stack for a six-axis hexapod/spider robot built around a BIGTREETECH Octopus v1.1 and an ESP32. It includes customized Marlin firmware, the ESP32 Wi-Fi/RF bridge, SD-card motion programs, a browser control panel, and a local motion simulator.

> **Hardware warning:** This project controls high-current stepper drivers and moving machinery. Test with the robot raised or the motors disconnected, keep an emergency power cut-off within reach, and verify every pin and current limit before applying motor power.

## What is included

| Path | Purpose |
| --- | --- |
| [`ESP_RF_Octopus`](ESP_RF_Octopus) | ESP32 firmware: Wi-Fi access point, web UI, RF/button inputs, and UART bridge to Marlin |
| [`Marlin/marlin-2.1.2.6`](Marlin/marlin-2.1.2.6) | Customized Marlin 2.1.2.6 for the BTT Octopus v1.1 and six TMC5160 axes |
| [`gcodes`](gcodes) | Motion and pose programs copied to the controller's SD card |
| [`WebApp`](WebApp) | Local browser UI that reads the configured Marlin motion-file map |
| [`simulator`](simulator) | Standalone browser-based hexapod motion simulator |
| [`Docs/pinMapping`](Docs/pinMapping) | Wiring, pin, driver, endstop, lamp, and command reference |
| [`build.py`](build.py) | Builds Marlin and optionally copies `firmware.bin` to an SD card |

`gcodes_OLD` is retained as reference material. The much larger upstream Marlin configuration-example bundle is intentionally not published because Lumiac does not use it.

## Requirements

- Python 3.9 or newer
- Git
- Node.js 18 or newer, only for the local WebApp and simulator
- A data-capable USB cable and the correct serial-port driver for your board
- For the complete hardware build: BTT Octopus v1.1, ESP32 DevKit, six correctly configured TMC5160 drivers, endstops, and a FAT32-formatted SD card

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

## Build and upload the ESP32 firmware

Build the `esp32dev` environment:

```bash
python -m platformio run -d ESP_RF_Octopus
```

Upload it, replacing the example port with your ESP32 port:

```bash
python -m platformio run -d ESP_RF_Octopus -t upload --upload-port COM4
```

Open the serial monitor at 115200 baud:

```bash
python -m platformio device monitor --baud 115200 --port COM4
```

After boot, connect a phone or computer to the ESP32 access point:

- SSID: `ESP_RF_Octopus`
- Default password: `octopus123`
- Control page: <http://192.168.4.1>

The access-point credentials are defaults stored in [`ESP_RF_Octopus/src/main.cpp`](ESP_RF_Octopus/src/main.cpp). Change the password before deploying the robot in a public or shared location.

## Build and flash the Octopus firmware

The root helper builds the `STM32F446ZE_btt` environment:

```bash
python build.py
```

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
3. Copy the repository's `gcodes` directory to `/gcodes` on the same card.
4. Turn off the Octopus board, insert the card, and power it on.
5. Confirm that the board renamed `firmware.bin` to `FIRMWARE.CUR`.

This is not stock Marlin. It contains the six-axis configuration and custom `M215` motion-file commands required by Lumiac. Building an unmodified Marlin checkout will not provide the same behavior.

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

- The ESP32 communicates with the Octopus over a 115200-baud UART link.
- The Octopus firmware exposes six motion axes as `X Y Z A B C`.
- `M215` selects, pauses, resumes, or stops motion programs stored under `/gcodes` on the SD card.
- `M355` controls the lamp through the Octopus bed/heater MOSFET output; this configuration does not use a heated bed.
- The ESP32 reset connection must be open-drain/active-low. Never drive the Octopus reset pin high.

See [`Docs/pinMapping`](Docs/pinMapping) before connecting any hardware. Treat the firmware source as the final authority if documentation and code differ.

## Troubleshooting

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

# Octopus Control Workspace

This repository contains firmware, motion files, and support material for an Octopus-based robot with multiple controller architectures.

## Layout

- [`ESP_Boards`](ESP_Boards)
  - [`ESP_Octopus`](ESP_Boards/ESP_Octopus): ESP32 on the Octopus side with Wi-Fi AP, web UI, Octopus UART bridge, and support for the `ESP_Remote` handheld. Lamp control is forwarded to Marlin with `M355`.
  - [`ESP_Remote`](ESP_Boards/ESP_Remote): ESP32 handheld remote with button input, Wi-Fi client behavior, sleep logic, and shared light / pose control.
  - [`ESP_NRF`](ESP_Boards/ESP_NRF): ESP32 on the Octopus side with the same web/AP behavior as `ESP_Octopus`, but remote commands come from an NRF24 module instead of `ESP_Remote`.

- [`Arduino_Nano_NRF24`](Arduino_Nano_NRF24)
  - [`Remote_Control`](Arduino_Nano_NRF24/Remote_Control): battery-oriented Nano handheld remote using NRF24 and deep sleep.
  - [`Octopus_Board`](Arduino_Nano_NRF24/Octopus_Board): Nano receiver for NRF24, Octopus UART, and reset-line recovery. Lamp control is forwarded to Marlin with `M355`.
  - [`Common`](Arduino_Nano_NRF24/Common): shared NRF24 packet protocol definitions.

- [`Marlin`](Marlin)
  - [`marlin-2.1.2.6`](Marlin/marlin-2.1.2.6): main Marlin firmware project for the Octopus controller board.
  - `MarlinConfigurations-2.1.2.6`: reference configuration bundle.

- [`gcodes`](gcodes)
  - SD card motion files used by `M215`, including `home`, `pos1`, `pos2`, `pos3`, and numbered random-position sequences.

- [`Docs`](Docs)
  - [`pinMapping`](Docs/pinMapping): top-level wiring reference across all current options.
  - `OldCodes`: archived earlier implementations for comparison and reference.
  - `DataSheets`: hardware reference material.

- [`WebApp`](WebApp)
  - local installable web app / browser tooling.

## Controller Options

1. `ESP_Octopus` + `ESP_Remote`
   - full ESP32 solution
   - Wi-Fi remote
   - browser control from phone / PC

2. `Arduino_Nano_NRF24/Octopus_Board` + `Arduino_Nano_NRF24/Remote_Control`
   - full Nano + NRF24 solution
   - lowest remote power consumption
   - no Wi-Fi or browser UI

3. `ESP_NRF` + `Arduino_Nano_NRF24/Remote_Control`
   - hybrid solution
   - Nano remote over NRF24
   - ESP32 on the Octopus side keeps browser UI, logging portal, lamp control through Marlin, and recovery logic

## Build

### Marlin / Octopus board firmware

Use the root helper script:

```powershell
python .\build.py
```

What it does:
- finds the active Marlin PlatformIO project
- builds environment `STM32F446ZE_btt`
- copies `firmware.bin` to the SD card root
- removes `FIRMWARE.CUR` first when present

Optional environment variable:

```powershell
$env:OCTOPUS_SD_ROOT = 'E:\'
python .\build.py
```

### ESP and Nano projects

Each firmware directory is its own PlatformIO project. Examples:

```powershell
python -m platformio run -d .\ESP_Boards\ESP_Octopus
python -m platformio run -d .\ESP_Boards\ESP_Remote
python -m platformio run -d .\ESP_Boards\ESP_NRF
python -m platformio run -d .\Arduino_Nano_NRF24\Remote_Control
python -m platformio run -d .\Arduino_Nano_NRF24\Octopus_Board
```

Upload examples:

```powershell
python -m platformio run -d .\ESP_Boards\ESP_Octopus -t upload --upload-port COM4
python -m platformio run -d .\ESP_Boards\ESP_Remote -t upload --upload-port COM4
python -m platformio run -d .\ESP_Boards\ESP_NRF -t upload --upload-port COM4
```

## Notes

- Lamp outputs are now expected to use the Octopus board bed/heater MOSFET through Marlin `M355`.
- NRF24 modules must be powered from `3.3V` and should have local bulk capacitance.
- The Octopus reset line must only be pulled low by the helper controller. Do not drive RESET high.
- The top-level wiring reference is in [`Docs/pinMapping`](Docs/pinMapping).

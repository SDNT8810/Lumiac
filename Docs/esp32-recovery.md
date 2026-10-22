# ESP12 works, but ESP32 has no dashboard

This symptom does not prove an Octopus UART wiring fault. The ESP32 creates its
own hotspot and HTTP dashboard independently of the Octopus link. A missing
dashboard points first to the selected Wi-Fi network, ESP32 power/boot, board
variant, flash installation, or an ESP32 startup failure. Shorted or conflicting
external wiring can also prevent boot; disconnected UART wires alone should
leave the dashboard working.

Both controller variants broadcast **Lumiac**, use **octopus123**, and serve
**http://192.168.4.1/**. Power the ESP12 off while testing the ESP32 so the phone
cannot silently join the other controller. Disconnect power before changing
wiring. For the initial isolation test, disconnect the ESP32 from Octopus and RF
wiring, and power the ESP32 DevKit from a known working USB data cable. A bare
module needs its own suitable regulated 3.3 V supply; USB applies to the DevKit.

The current `esp32dev` firmware is for an **original ESP32 with 4 MB flash** and
Classic Bluetooth (for example ESP32-WROOM-32). It is not an ESP32-C3 or ESP32-S3
image. Confirm the module marking before choosing the firmware.

## Install the complete verified image set

After obtaining the matching 1:1 package, run from the project directory:

```powershell
python flash_release.py verify
python flash_release.py esp32 --port COM4
```

Replace COM4 with the ESP32's actual USB serial port. This command writes the
bootloader at 0x1000, partition table at 0x8000, initial OTA selection at 0xe000,
and application at 0x10000. Copying only `esp32.bin` onto a board with a previous
partition layout is not equivalent. Release BOOT after flashing and reset the
ESP32. An ESP12 binary cannot run on the ESP32.

For a rebuild from source, `python build.py esp32 COM4` builds and uploads the
configured ESP32 environment. Do not upload to Octopus's USB port instead.

## Separate the failure stages

1. With ESP32 alone, look for Lumiac Wi-Fi, join it, and open the HTTP address.
   If no hotspot appears or it keeps disappearing, capture USB logs at 115200
   baud from reset. The ROM boot reason and application startup messages help
   distinguish download mode, an invalid image, brownout, and a software crash.
2. If the hotspot stays up but the dashboard does not load, try
   `http://192.168.4.1/api/network`. A response means the HTTP server is running;
   a blank dashboard can then be investigated separately from radio/boot.
3. If the dashboard works with ESP32 alone, power off before reconnecting the
   Octopus and RF leads. If it then stops booting, investigate those connections
   and the shared supply.
4. If the dashboard works but Octopus is offline, check the ESP32-specific pins:
   GPIO17 TX goes to Octopus PD9 RX; GPIO16 RX comes from Octopus PD8 TX; share
   ground. The Octopus reset output is GPIO23. These differ from ESP12's GPIO1,
   GPIO3, and GPIO2; swapping boards without adapting those connections will
   not work. See [the complete wiring table](esp-controllers.md).

No single step above establishes the actual cause without the ESP32 boot log
or an isolated-board test. Keep the working ESP12 installation available until
the replacement controller has passed those checks.

# Lumiac 1:1 gearing

This branch includes the controller stability fix and defaults to **1:1** drive.
The normal `RF` branch retains the existing gearbox calibration.

Edit `Marlin/marlin-2.1.2.6/Marlin/lumiac_gearing.h`:

```cpp
#define LUMIAC_GEAR_INPUT_TURNS 1
#define LUMIAC_GEAR_OUTPUT_TURNS 1
```

The ratio means motor revolutions divided by output revolutions. For example,
1 and 1 select direct drive; 1 and 3 reproduce the previous 1:3 calibration;
3 and 1 mean the motor makes three turns per one output turn.
Values must be positive integers. All six arms share this setting.

At the existing 32 microsteps, direct drive uses approximately **3200 steps/mm**
(three times the previous 1066.66). This is derived from the existing calibration,
not a new measurement. Coordinates and the 0-120 mm travel range stay arm-side.
Current, microsteps, requested speeds and accelerations keep their existing values.

For buzzing or jerky motion after this conversion, see the
[exact settings comparison and reversible quiet-mode test](quiet-mode.md).

## Install the prebuilt release on another Windows PC

Release **2026-10-22.2** includes matching Octopus, ESP-12S and ESP32 images.
The gear setting is already 1:1; no source edits or rebuild are needed to install
the committed package. Open PowerShell in the folder where you keep projects:

```powershell
git clone --branch onetoone --single-branch https://github.com/SDNT8810/Lumiac.git
cd Lumiac
```

If the repository is already cloned, use these commands from its root instead:

```powershell
git fetch origin
git switch onetoone
git pull --ff-only origin onetoone
```

Install the tools in a virtual environment. Calling its Python directly avoids
needing to change PowerShell's script execution policy:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe flash_release.py verify
.\.venv\Scripts\python.exe -m serial.tools.list_ports -v
```

Substitute the actual mounted SD-card drive and ESP serial ports below. Connect
one ESP at a time to identify its port; COM5 and COM4 are examples.

```powershell
# Copy the Octopus firmware onto the FAT32 microSD card:
.\.venv\Scripts\python.exe flash_release.py octopus --sd E:\

# Flash the ESP-12S through its 3.3 V USB-to-UART programmer:
.\.venv\Scripts\python.exe flash_release.py esp12 --port COM5

# Flash an original ESP32 with 4 MB flash, such as ESP32-WROOM-32:
.\.venv\Scripts\python.exe flash_release.py esp32 --port COM4
```

The ESP32 command installs all four required images, including the partition
table. Do not substitute an application-only upload when replacing an old flash
layout. For ESP-12S programmer and boot-pin wiring, see [the controller guide](esp-controllers.md).

Safely eject the microSD card, insert it into the powered-off Octopus and boot
the board. Confirm `firmware.bin` becomes `FIRMWARE.CUR`; leave the card installed.
ESP-12S and ESP32 are alternative controllers for the Octopus. Both can be flashed
for testing, but power only the selected controller during the connection test:
both broadcast **Lumiac**, password **octopus123**. Open
**http://192.168.4.1/network** to configure a 2.4 GHz router.

If the ESP32 still has no hotspot, capture its USB startup output with:

```powershell
.\.venv\Scripts\python.exe -m serial.tools.miniterm COM4 115200
```

Press the ESP32's EN/reset button to capture boot messages. Exit the monitor with
Ctrl+] before flashing again. See [ESP32 recovery](esp32-recovery.md).

## Rebuild from source

Build all three matching images:

```powershell
git switch onetoone
python tools/firmware_package.py all
```

Changing the gearing changes the firmware identity. For the **first switch** from
RF to this calibration, install the Octopus image using its microSD and the image
for your active ESP controller using USB:

```powershell
python flash_release.py octopus --sd I:\
python flash_release.py esp32 --port COM10
# Use this instead if your controller is an ESP-12S:
python flash_release.py esp12 --port COM5
```

Replace the drive letter and port with those of the actual PC. ESP32 and ESP-12S
are alternatives; install only the one connected to your Octopus. Reinsert the
microSD and restart the Octopus. Use Home, then verify a short measured movement
before running a full program with the changed mechanism.

After this initial matching installation, **Full update** on the remote panel
automatically builds `onetoone` for this controller. Controllers still on `RF`
continue to build `RF`. A later edit to the gear calibration also requires a
matching local installation, because an OTA update cannot silently change the
machine's physical travel calibration.

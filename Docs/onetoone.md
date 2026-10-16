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

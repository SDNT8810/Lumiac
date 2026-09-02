# Hexapod Marlin Notes

This Marlin tree is configured for:

- Board: `BTT Octopus v1.1`
- MCU env: `STM32F446ZE_btt`
- Drivers: `6 x TMC5160 / TMC5160T` in SPI mode
- Axes: `X Y Z A B C`
- No extruders
- No heaters or thermistors enabled

Axis to Octopus socket mapping:

- `X` -> `MOTOR0`
- `Y` -> `MOTOR1`
- `Z` -> `MOTOR2`
- `A` (`I` internally) -> `MOTOR4`
- `B` (`J` internally) -> `MOTOR5`
- `C` (`K` internally) -> `MOTOR6`

Notes:

- `MOTOR3` and `MOTOR7` are unused in this setup.
- Marlin auto-assigns `I/J/K` step-dir-enable to unused extruder sockets.
- `I/J/K` SPI chip select pins are mapped to the Octopus `E0/E1/E2` CS lines.

Important hardware note:

- For SPI TMC drivers, check the Octopus driver jumpers and remove DIAG jumpers unless you intentionally use sensorless homing.

Build:

```powershell
python -m platformio run
```

Built firmware file:

- `.pio/build/STM32F446ZE_btt/firmware.bin`

First serial checks after flashing:

```gcode
M115
M122
M906 X800 Y800 Z800 A800 B800 C800
```

If `M122` still reports `All HIGH`, check:

- driver orientation
- board power, not only USB
- SPI jumpers under all six sockets
- DIAG jumpers removed

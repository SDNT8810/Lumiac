# ESP-12S and ESP32 controller alternatives

Choose one controller to connect to Octopus. The ESP-12S runs the same access point, browser UI, lamp control, pose/random commands, UART monitoring, reset/recovery, and startup automation as the ESP32. It does not require a second ESP32 over Wi-Fi.

## Build and upload

From the repository root, after installing `requirements.txt`:

```powershell
python build.pt eps12 COM5
python build.pt eps32 COM4
```

These commands build and then upload to the specified port. The normal spellings `python build.py esp12 COM5` and `python build.py esp32 COM4` work too. `esp12s` is another alias for `esp12`. Use the port belonging to the ESP programmer, not the Octopus USB port.

For build-only operation, omit the port or add `--build-only`. Each ESP build embeds the current `gcodes/input*.txt` and `gcodes/pos*.txt` files in firmware. Rebuild and upload the chosen ESP after changing these files. There is no separate filesystem upload, and neither ESP target accesses an Octopus SD card. `python build.py` still builds Marlin and copies its firmware to the configured SD path when available; `python build.py marlin --build-only` skips that copy. Once the Octopus has flashed `firmware.bin` and renamed it `FIRMWARE.CUR`, power off and remove its SD card.

| Target | PlatformIO environment | Binary |
| --- | --- | --- |
| ESP-12S | `esp12s` | `ESP_RF_Octopus/.pio/build/esp12s/firmware.bin` |
| ESP32 DevKit | `esp32dev` | `ESP_RF_Octopus/.pio/build/esp32dev/firmware.bin` |

The ESP-12S profile uses ESP8266 with 4 MB flash, DIO flash mode, and the PlatformIO `esp12e` board definition. Verify the actual module/carrier marking and flash size before uploading; a vendor-branded carrier is not a different CPU target. See [PlatformIO's ESP-12 board definition](https://docs.platformio.org/en/stable/boards/espressif8266/esp12e.html).

## Wiring

These are **ESP GPIO numbers**, not positions on a BIGTREETECH connector. The source of truth for these assignments is [`board_config.h`](../ESP_RF_Octopus/src/board_config.h). Check the exact carrier/socket schematic before plugging a module into a motherboard: firmware pin assignments cannot change PCB routing. Wire the following signals explicitly where the socket does not provide them, and isolate any conflicting existing socket connections.

| Signal | ESP-12S GPIO | ESP32 GPIO | Connection / behavior |
| --- | --- | --- | --- |
| UART TX | 1 | 17 | To Octopus PD9 / UART RX |
| UART RX | 3 | 16 | From Octopus PD8 / UART TX |
| Octopus reset output | 2 | 23 | To Octopus RESET / NRST, active-low; released open-drain in application firmware |
| LIGHT_ON | 14 | 14 | Active-high RF output |
| LIGHT_OFF | 5 | 32 | Active-high RF output |
| POS1 | 4 | 25 | Active-high RF output |
| POS2 | 12 | 33 | Active-high RF output |
| RANDOM | 13 | 26 | Active-high RF output |
| DIMMER | 16 | 13 | Active-high RF output; ESP8266 polls this input |
| POS3 | 15 | 27 | Active-high RF output; ESP8266 must see LOW during boot |
| RESERVE | Unconnected | 21 | Already has no assigned robot action |
| Ground | GND | GND | Common with Octopus and the RF receiver |

The ESP-12S has seven active RF inputs. RESERVE is disabled to leave GPIO0 available for flashing and GPIO2 for Octopus reset. GPIO16 cannot generate GPIO interrupts, so the ESP8266 profile polls/debounces all seven inputs. ESP32 retains its interrupt-based inputs. See [ESP8266 GPIO and serial reference](https://arduino-esp8266.readthedocs.io/en/latest/reference.html).

The current receiver interface is **active HIGH**, with idle LOW; it is not the old active-low/pull-up wiring described in earlier project notes. All inputs use `INPUT`, so the receiver must drive a defined idle level, or provide an external pull-down (typically 10 kohm). If using physical switches instead of receiver outputs, connect each switch between its input and 3.3 V with a pull-down to ground. Unused inputs must not float. RF output voltages must be compatible with 3.3 V logic; use level conversion if the receiver outputs a higher voltage.

## ESP-12S power and boot constraints

Use a regulated 3.3 V supply suitable for ESP8266 Wi-Fi current peaks (allow at least 500 mA), with local decoupling. Bare ESP-12S power and I/O are not 5 V inputs. Only use a carrier's higher-voltage supply input if its own schematic explicitly supports it. Do not assume a USB-to-UART adapter's small 3.3 V output can power the module reliably. See [ESP8266 hardware setup guidance](https://arduino-esp8266.readthedocs.io/en/3.0.2/boards.html).

ESP8266 samples boot straps at reset: GPIO0 and GPIO2 must be HIGH for normal startup, and GPIO15 must be LOW. Use appropriate pulls where the module/carrier does not already provide them. The RF POS3 output must stay LOW during reset/power-up; a latching receiver output left HIGH will prevent boot. Do not hold POS3 while booting. Software cannot fix an incorrect boot strap before it starts. See [Espressif's boot-mode documentation](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html).

GPIO2 is released by the application reset driver, so the Octopus NRST pull-up must keep it HIGH during ESP startup. The ESP8266 ROM also drives GPIO2 as UART TX while programming. **Disconnect the ESP from Octopus, including its reset lead, while uploading.** A standard connector is not proof that these extra control signals are routed correctly.

GPIO0 is reserved for the programmer/BOOT button. GPIO6-11 are flash connections and are not available for remote buttons.

## Flash an ESP-12S on COM5

1. With power off, disconnect the ESP module's Octopus UART/reset connection. Prevent the USB programmer and Octopus TX from driving the same RX pin. Use one suitable power source; do not tie independent power outputs together.
2. Connect the programmer's **3.3 V logic TX to GPIO3/RX**, RX to GPIO1/TX, and ground to the module's ground. Supply the module correctly; keep EN/CH_PD and ESP RST at their normal HIGH levels.
3. If the carrier lacks automatic boot/reset circuitry, hold GPIO0 LOW and pulse the ESP module's RST LOW, then release RST. Keep GPIO2 HIGH and GPIO15 LOW. This enters the ROM bootloader.
4. Run `python build.pt eps12 COM5`. If it remains at `Connecting...`, check the boot straps, programmer port, crossed RX/TX, power, and that no serial monitor is holding COM5 open.
5. After a successful upload, release GPIO0 HIGH and reset/power-cycle the ESP. The application will not boot while GPIO0 remains LOW.
6. Power off before reconnecting the runtime UART and reset wiring. Remove the programmer's TX from GPIO3 so it does not contend with Octopus TX.

A bare ESP-12S has no native USB. COM5 must be the USB-to-UART programmer or a carrier that includes one. Supplying a COM port to the build helper cannot replace the physical bootloader wiring.

The two controller profiles keep SSID `ESP_RF_Octopus`, password `octopus123`, and `http://192.168.4.1/`. Run only the chosen controller. Existing startup behavior is preserved: it homes the robot and starts loop S1 automatically. Arrange the first powered hardware test accordingly, with the driver cooling issue resolved and the mechanism supported.

Dashboard, RF, and ESP-terminal `M215 S1`–`S7` and `M215 P1`–`P3` select programs in ESP flash. The ESP sends the commands over the existing 115200-baud UART link and waits for an Octopus acknowledgement between commands. `M215 P`, `M215 R`, and `M215 X` pause, resume, and stop this ESP stream. Homing uses direct `G28`; `home.txt` is retained as a source reference. Direct `M215` commands sent to the Octopus USB port still use Marlin's legacy SD-card implementation and require the card.

The random programs run forward, then backward, returning to the exact starting position before `@LOOP` repeats. The ESP logs `Looping ESP flash program` at each restart. Startup and ESP-initiated homing have a five-minute limit; Marlin can remain silent while `G28` blocks, so the ESP does not queue periodic position probes during homing.

## Diagnostics and limitations

- ESP32: USB debug logging at 115200 baud remains available.
- ESP-12S: UART0 is dedicated to Marlin at 115200 baud. Application logs go to the browser/history only; sending debug text on that UART would mix it with G-code. The ROM's brief 74880-baud boot message cannot be disabled by application code; startup sends a newline before querying Marlin to terminate any partial line.
- ESP-12S allows four Wi-Fi clients and retains 32 log entries (12 replayed on connection). ESP32 retains its existing eight-client AP and 120-entry history. The ESP8266 also has a lower heap-recovery threshold and a 1024-byte UART receive buffer.
- Compile success verifies both software targets. Actual board power, RF levels, boot straps, sustained Wi-Fi/UART operation, and motion still need a hardware test; no upload is performed during build-only verification.

Build-helper regression checks run with `python -m unittest discover -s tests -v`. These check port/target routing and SD-copy boundaries without accessing serial devices.

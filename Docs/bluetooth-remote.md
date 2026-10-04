# Lumiac Bluetooth remote

The ESP32 dashboard at `http://192.168.4.1` now includes a small **Bluetooth remote** panel: Scan, Pair, Reconnect, Disconnect, and Forget. The HID consumer-report decoder comes from `Docs/blue`; capture history and descriptor diagnostics are kept in that example rather than added to the control dashboard.

Use an original ESP32 / ESP32-WROOM-32 with Bluetooth Classic, such as the existing `esp32dev` board. The ESP-12S build still works with RF inputs but has no Bluetooth panel. ESP32-C3, C6 and S3 are not compatible with this Classic HID remote.

## Install and pair

Scanning, pairing, and reconnecting need only the ESP32 and remote. Power the ESP32 over USB; the Octopus can remain disconnected. Upload the ESP32 firmware, substituting its actual USB port:

```powershell
python build.py esp32 COM5
```

The first ESP32 build downloads ESP-IDF 4.4.7 and compiles Arduino as an IDF component. This enables the Classic HID host omitted from Arduino's precompiled SDK. The SDK defaults and the larger application partition are part of the project; no filesystem upload is needed.

1. Join Wi-Fi `ESP_RF_Octopus` (default password `octopus123`) and open `http://192.168.4.1`.
2. Put the remote into pairing mode. Disconnect/forget it on a previously paired phone if needed.
3. Select **Scan**, then **Pair** beside `POP Multimedia`, Satechi, or the remote's address. A name may initially appear as `Unknown remote`.
4. Wait for **Connected** beside the remote name. Pairing errors and timeouts appear in the panel. The Octopus indicator can stay **Off** throughout pairing.

Lumiac saves one remote and starts reconnecting as soon as Bluetooth is ready after reboot, without opening the dashboard. Failed or timed-out attempts retry after two seconds, so a sleeping or out-of-range remote can return automatically. A scan temporarily pauses retries and allows ten seconds for selecting a result afterward. Explicit **Disconnect** suspends reconnecting until **Reconnect** or reboot; a failed **Reconnect** keeps retrying automatically. **Forget** also removes the saved address and Bluetooth bond. Pairing confirmation is accepted only for the selected or saved remote; legacy PIN requests use zeros, as in the example.

After pairing, remote control works with no phone connected to Wi-Fi. The ESP discovers Octopus capabilities in its control loop, retrying missing reports every three seconds at idle or between completed program commands. Receiving position updates alone does not stop capability discovery. The ESP32 buffers up to 4 KB of serial input so startup replies can arrive while Bluetooth initializes or the dashboard is loading.

The compact **Remote keys** row shows the Bluetooth remote's five keys when connected, and switches to RF keys when an RF input is used. Pressed keys glow green; holding for 400 ms turns them orange. Releases clear the indicators, with a short flash retained for quick taps. This feedback works without Octopus.

**POS 1/2/3** buttons are orange while the selected move is running (including homing or a pause), green after Octopus confirms the final move has finished, and gray otherwise. Stop, standby, random motion, manual movement, or a failed move clears the selection indicator. Brightness changes preserve it. Bluetooth Next/Back, RF POS keys, and phone selections use the same completion tracking and update every connected dashboard. The Next/Back cycle remembers the last selected POS even after its indicator is cleared.

## Enable lamp, motor and fan control

Update both the ESP and Octopus firmware to operate the lamps, motors and automatic FAN0 cooling. Build the Octopus image and copy it to its SD card as `firmware.bin` using the usual procedure:

```powershell
python build.py marlin --build-only
```

This is not required for Bluetooth pairing.

## Buttons

| Button | Action |
| --- | --- |
| `+` / `−` tap | Increase/decrease brightness by 2 percentage points, clamped to 0–100%. |
| `+` / `−` hold | After 400 ms, repeat 2 points 15 times per second: 30 percentage points per second. |
| Next / Back | Cycle POS 1 → 2 → 3 → 1, or in reverse. With no selected position, Next starts at POS 1 and Back at POS 3. Dashboard POS selections update the same selection. |
| Play/Pause while moving | Pause immediately on button-down, retaining the current move and program cursor. |
| Play/Pause while paused | Single click: continue the interrupted move and program after the 600 ms double-click window. |
| Play/Pause while idle | Single click: start random movement after the 600 ms double-click window. |
| Double-click Play/Pause within 600 ms | Start random movement, including when a POS is paused or moving. From standby, wake with lamps at 70%. |
| Hold Play/Pause for 2.5 s | Stop motion, turn lamps and FAN0 off, release all motors, and cancel automatic playback. Releasing the button does not restart motion. |
| Play or Next / Back from standby | Take motor control, set the lamps to 70%, and perform the requested motion. Re-home first because released arms can change position. |
| `+` / `−` in standby or during motion | Change only the lamp. Motors, position selection and pause state are untouched. From off, `+` starts at 2%; `−` stays off. |

A Play hold from idle or pause does not briefly start motion. When a POS program is moving, Play also pauses it; the next tap continues that program. After standby, Play starts random movement rather than resuming a path whose physical position may have changed.

Double-click timing is measured between releases. Pause remains immediate on button-down; single-click resume/start waits 600 ms to distinguish it from a double-click. A hold, disconnect, or Next/Back cancels a pending single click.

The 2.5-second hold also works while Octopus is disconnected or its capabilities are still unknown: the ESP sets the dashboard lamps to OFF/0 and cancels pending motion. It sends the lamp-off command immediately and syncs the saved lamp state when Octopus reconnects. Moving or resuming still requires a connected Octopus with the updated firmware.

Standby removes lamp output, FAN0 output and motor holding current. Wi-Fi and Bluetooth stay available for remote wake and dashboard access; this is not ESP deep sleep. Actual power draw depends on the connected drivers and power supplies.

The remote must transmit down/release states to measure a hold. As in the example, a remote that transmits only instantaneous clicks cannot expose its physical hold duration. Disconnects cancel held inputs, duplicate reports do not create extra taps, and a missing release stops repeating after 15 seconds.

## FAN0 cooling

Connect the fan to the Octopus **FAN0** socket, observing its polarity and setting its voltage jumper to match the fan's rated voltage. See the [BTT Octopus wiring guide](https://global.bttwiki.com/Octopus.html#fan-and-proximity-switch-settings).

| State | FAN0 output |
| --- | --- |
| Idle after boot, position reached, stopped or paused | 30% |
| Normal movement, including POS and homing | 80% |
| Random movement, including its initial homing | 100% |
| Standby after holding Play/Pause for 2.5 s | Off |

These are PWM duty settings, not measured fan RPM. Octopus checks actual queued motion and its pause latch every 100 ms, including during homing. Pause always reduces the output to 30%; resuming restores the moving speed. Brightness changes leave fan and motion state alone, including in standby. A motion command wakes the fan along with the motors.

FAN0 is reserved for this automatic control; `M106`/`M107` cannot override it. Other fan channels retain their normal behavior. Automatic cooling also works with the ESP-12S controller; Bluetooth remains ESP32-only.

## Motion, lamp and fan transport

Updated Octopus firmware advertises `Cap:LUMIAC_REALTIME:1`. Until this capability is seen, Bluetooth motion commands are rejected with a message in the dashboard log. Pairing is independent of the Octopus firmware version. Existing dashboard/RF operation remains available.

- `P000` / `R000` use Marlin's emergency parser to hold/resume the stepper timer without discarding the current block. The ESP uses the equivalent comment aliases `;P000` / `;R000` so repeated pause/resume requests cannot fill the ordinary command queue during a hold. A held timer cannot be re-enabled by a later planner enqueue.
- The ESP keeps one program command in flight. A move is followed by `M400` and an identifying `M118` completion marker, preventing a stopped program from leaving future path segments queued. Each segment completes before the next is submitted, so paths decelerate at segment boundaries.
- Lamp changes use comment lines `;L000` through `;L255`. The serial emergency parser captures the latest PWM value and `idle()` applies it, including inside homing and planner waits. Comment lines never enter or replay through the ordinary G-code queue. Standard terminal `M355` remains available.
- Octopus advertises `Cap:LUMIAC_FAN:1`. The ESP sends `;F000` for standby, `;F001` for normal operation and `;F002` for random operation. The emergency parser captures the mode immediately; the planner applies live FAN0 output instead of stale fan settings saved inside queued moves. The ESP resends its mode when Octopus reconnects.
- `M410` can abort a held block; `M18` releases the motors for standby. Intentional cancellation of six-arm homing leaves the axes unhomed and does not masquerade as an endstop fault. Only genuine endstop completion marks the ESP homed.
- The normal pause leaves drivers energized so motion can continue from the held position. Paused time is excluded from ESP playback acknowledgement timeouts.

The ESP32 startup calls Arduino's `btStarted()` to link its Bluetooth memory-retention hook. Calling only IDF APIs leaves Arduino's weak `btInUse()` implementation in the image, which releases Bluetooth memory before `setup()` and causes `ESP_ERR_INVALID_STATE`. `tests/test_bluetooth_link.py` checks the actual ELF for this regression. Startup failures name the failed stage in the USB serial log.

The firmware source for the served dashboard is `ESP_RF_Octopus/src/web_page.h`; `web_page.html` is an older design reference. The separate Node WebApp is not the ESP-hosted dashboard.

## Verification

Build all affected targets and run the existing Python tests:

```powershell
python build.py esp32 --build-only
python build.py esp12 --build-only
python build.py marlin --build-only
python -m unittest discover -s tests -v
```

Portable HID, timing and real-time lamp/fan parser tests (requires `uv`, which supplies a native compiler):

```powershell
New-Item -ItemType Directory .test-tools -Force
uv run --with ziglang python -m ziglang cc -std=c11 -Wall -Wextra -Werror -IESP_RF_Octopus/src ESP_RF_Octopus/src/media_keys.c tests/test_media_keys.c -o .test-tools/test_media_keys.exe
uv run --with ziglang python -m ziglang c++ -std=c++11 -Wall -Wextra -Werror tests/remote_control_test.cpp -o .test-tools/remote_control_test.exe
.\.test-tools\test_media_keys.exe
.\.test-tools\remote_control_test.exe
uv run --with ziglang python tests/test_realtime_parser.py
uv run --with ziglang python tests/test_standby.py
npm install --prefix .test-tools --no-package-lock --no-save jsdom@22.1.0
node tests/test_bluetooth_dashboard.cjs
```

On hardware, verify pairing/reconnect, a mid-segment pause and resume, brightness during each POS and random movement, a hold through the standby threshold, and wake via each transport key. Check FAN0 at idle, during POS and random motion, paused, and in standby. Also check standby while homing and `+` / `−` while motors are released. Build and host tests do not establish physical pairing, motor/fan behavior or power consumption.

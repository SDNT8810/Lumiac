# Firmware updates by hotspot, cloud or USB

Release 2026-10-19.1 enables all three methods on either the ESP-12S or ESP32
controller. Keep the correct branch: `RF` for normal gearing, `onetoone` for
direct drive. Motor currents, gearing, RF/Bluetooth controls and motion remain
unchanged by this update.

Install the new controller firmware once over USB, using its actual COM port:

```powershell
git pull
python flash_release.py esp12 --port COM5
```

For an ESP32 controller, use `esp32` instead of `esp12`. Leave the Octopus
microSD installed. Its existing SD update receiver must already be installed;
otherwise install `python flash_release.py octopus --sd I:\` once.

For future local updates, pull the repository while the PC has internet, then
connect it to **Lumiac** Wi-Fi (password `octopus123`). Run:

```powershell
python local_update.py --target all
```

This validates all images, selects the connected ESP variant, updates that
controller first, then updates the Octopus. No internet is required during
installation. Keep the PC awake and allow Python through Windows Firewall if
prompted. `--host 192.168.1.4` can target a controller on the same router LAN;
use its actual address.

To update one component:

```powershell
python flash_release.py esp12 --host 192.168.4.1
python flash_release.py octopus --host 192.168.4.1
```

`python local_update.py --target all --build` builds all firmware first. Install
build dependencies while internet is available. Normal USB/SD commands remain
available. The cloud **Full update** button also remains available at
https://lumiac.control.timiac.it/ and builds the installed calibration branch.

Updates park motion, verify SHA-256, image role, hardware identity and revision,
and wait for the exact completion receipt. Interrupted flashes are never
automatically resubmitted. Home the arms before resuming motion.

The local dashboard/network page links to `/api/diagnostics`. A bounded journal
retains the latest 12 short events in flash and survives a restart; ESP-12S
reads those slots on demand to preserve TLS memory. Update checkpoints retain
the last state/progress/error, and the cloud archives received history for 30
days. Low-memory/busy diagnostic requests return an error for retry instead of
competing with a firmware transfer. Hardware commissioning is still required
to confirm Wi-Fi transfers and holding positions on the real installation.

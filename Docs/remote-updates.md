# Lumiac remote control and Wi-Fi firmware updates

Open **https://lumiac.control.timiac.it/** and sign in with the existing **Andrea**
account and password used for the Timiac remote panel. The page controls Lumiac's
home, positions, random motion, pause/resume, speed, lamps, brightness and manual
axes, and shows its live positions and connection state.

Lumiac uses **either an ESP32 or an ESP-12S controller**, connected by UART to one
Octopus. Both controller variants support remote control and updates. Use one
controller at a time; the panel identifies the connected variant and rejects
firmware for the other ESP type.

## Initial installation in Italy

One local USB/SD installation is required. The ESP32 changes from a single large
application to two update slots. Existing bootloaders are retained; no Octopus
BOOT jumper changes or extra wires are needed.

1. Open the Lumiac repository folder in VS Code. This update is on its **RF**
   branch. In the terminal run:

   ```powershell
   git switch RF
   git pull
   python -m pip install -r requirements.txt
   python flash_release.py verify
   ```

2. With motors safely clear, update the Octopus's FAT32 microSD. Replace `I:`
   with the actual card drive:

   ```powershell
   python flash_release.py octopus --sd I:\
   ```

   Insert it into the powered-off Octopus and power on. Confirm `firmware.bin`
   became `FIRMWARE.CUR`. **Leave the card installed** for future remote updates.

3. Update the controller in use. Replace the COM port with its Device Manager
   port. Use the ESP-12S 3.3 V programming adapter and boot mode if applicable:

   ```powershell
   python flash_release.py esp32 --port COM4
   # Or, for the ESP-12S controller:
   python flash_release.py esp12 --port COM5
   ```

   The ESP32 command installs its bootloader, new partition table, initial OTA
   boot selection and application. Future updates install the application only.

4. Reconnect the normal UART and power wiring. The existing startup home/motion
   behavior remains. Join the controller's **Lumiac** Wi-Fi (password
   `octopus123`) and open **http://192.168.4.1/network**. Enter the internet
   router's **2.4 GHz** Wi-Fi name and password, then **Save and connect**.
   The local dashboard also links to this page as **Internet / remote access**.

5. Reconnect the PC/phone to its normal internet connection. Sign in at the
   remote panel. Check the controller and Octopus are online, then test Stop,
   Home, lighting and one position. Test the first firmware update while someone
   is beside the installation.

## Later updates from Canada

Pull the matching firmware package on your PC and run:

```powershell
git pull
python remote_update.py --target all
```

Enter the Andrea dashboard password. This verifies the local files and updates
the Octopus, then whichever ESP controller is connected. Use `--target controller`
or `--target octopus` for one component. `--target esp12` and `--target esp32`
also work, provided that is the connected controller.

Alternatively, use **Firmware updates** in the remote panel:

| Panel target | File |
|---|---|
| ESP32 controller | `firmware/esp32.bin` |
| ESP-12S controller | `firmware/esp12.bin` |
| Octopus | `firmware/octopus.bin` |

Select the application file, never the ESP bootloader or partition-table file.
The local ESP dashboard and motion programs are embedded in its application and
update together. No separate dashboard filesystem upload is needed.

Updates stop motion and wait for an Octopus completion fence before writing.
They leave motion parked across reboot. When finished, select **Home**, then a
position or **Run motion loop**. Firmware downloads use verified HTTPS and
SHA-256. The Octopus receives numbered CRC-checked blocks into `lumiac.upd`,
checks the complete SD file by readback, then promotes it to `firmware.bin` for
the factory SD bootloader. A truncated or unverified transfer cannot activate.
Stop cancels transfer before activation; keep power connected once reboot starts.

Commands expire after one minute and belong to one ESP boot, preventing replay
after a reboot or long disconnection. Firmware images must match the controller
role and hardware identity. A hardware-contract change needs a new matched local
installation. Failed flashing jobs are never automatically resubmitted.

## Build and verification

Existing `python build.py esp12`, `esp32` and `marlin --build-only` commands remain
available. To build and export the complete package without flashing:

```powershell
python tools/firmware_package.py all
python flash_release.py verify
```

The package includes SHA-256 hashes in `firmware/manifest.json`. ESP32 Bluetooth
remains enabled, with its Classic HID host and existing controls. Motor currents,
motion tuning and wiring are unchanged by this update.

The cloud panel is hosted on Cloudflare; the controller makes outgoing HTTPS
connections to it. The page remains available while the robot is off and shows
**Lumiac is offline**. No router port forwarding is required. Router settings and
the parked-update flag are stored on the ESP and survive application updates.
Both firmware variants and the SD receiver are built and host tested; actual
over-the-air installation still needs the first on-hardware commissioning test.

## Full update: one button

Open the remote dashboard, sign in, and press **Full update** under Firmware updates. No firmware files need to be selected.

1. The server requests a fresh GitHub Actions build of the latest pushed commit (RF).
2. Every firmware is built and checked before any board is updated.
3. The server installs the complete package in sequence, waiting for each board's verified success and reboot before continuing. Progress survives closing the browser.
4. If any build or installation fails, the sequence stops. Check the message/build log before trying again. Keep power connected throughout installation. **Stop** cancels the remaining sequence; a flash already activating must finish safely.
5. After success, calibrate/home the arms and resume operation from the dashboard.

Lumiac builds the Octopus, ESP32, and ESP-12S firmware. It installs the Octopus first, then whichever ESP controller is connected. ESP32 and ESP-12S are alternatives: an absent controller cannot be flashed.

The initial USB/SD setup is still required once. Leave all Octopus microSD cards inserted and connect the controller to Internet Wi-Fi. Push your code before using Full update: uncommitted changes on a PC are not included.

### Automatic-build setup (maintainers)

The gateway uses `GITHUB_REPOSITORY`, `GITHUB_REF`, and the Worker secrets `GITHUB_BUILD_TOKEN` (repository Actions write/content read) and `FIRMWARE_BUILD_KEY`. The matching repository Actions secret `FIRMWARE_BUILD_KEY` authenticates uploads only; browser accounts and device keys cannot publish builds. `.github/workflows/full-update.yml` must also exist on the repository default branch for workflow dispatch registration. Lumiac's source/build branch remains RF.

The workflow runs on GitHub-hosted runners; no local PC or VPS build agent is needed. Build failures are reported to the dashboard. GitHub Actions must be enabled and have available build minutes. The builder uploads the verified complete bundle; bootloaders and partition tables are excluded from OTA.

For a build-only smoke test, an authenticated administrator can POST `{"validateOnly":true}` to `/api/devices/DEVICE_ID/full-update`. This builds and verifies everything without creating hardware update jobs. Normal dashboard clicks always build and install.

### Using another PC

The firmware's paired cloud configuration is committed with the source. `CloudGateway/project-secrets.json` also contains the project device key, Firebase API key, and full-build upload key, as requested. A normal pull includes these files; no credential copy/paste is needed to flash firmware or use the existing Full update service.

To reapply the project secrets to the existing Cloudflare Worker after signing into Wrangler, run `npm run configure:secrets --prefix CloudGateway`. This preserves its separately configured GitHub build account token. Repository/account authorization itself uses your normal GitHub and Cloudflare sign-in.

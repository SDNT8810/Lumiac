# Lumiac Satechi Bluetooth Media Button Monitor

Target: **original ESP32 / ESP32-WROOM-32 family with Bluetooth Classic**.

This project makes the ESP32:

- Wi-Fi hotspot: **Lumiac**
- Wi-Fi password: **12345678**
- Dashboard: **http://192.168.4.1**
- Bluetooth Classic HID host for the Satechi ST-BMB / POP Multimedia media button
- Browser scan/connect/disconnect controls
- Live connection status
- Last media key + raw HID bytes
- Rolling event log

## Recommended ESP-IDF

Use **ESP-IDF v5.3.1**. The official `esp_hid_host` example is known to work on this release; a reported ESP-IDF 5.5 regression affected the HID host example.

## Fast build / flash

1. Install/open ESP-IDF v5.3.1.
2. Open an ESP-IDF terminal in this project folder.
3. Run:

```bash
idf.py set-target esp32
idf.py build
idf.py -p YOUR_PORT flash monitor
```

Examples for `YOUR_PORT`:

- Windows: `COM5`
- Linux: `/dev/ttyUSB0`
- macOS: `/dev/cu.usbserial-...`

After boot:

1. On the phone/PC, join Wi-Fi **Lumiac** using **12345678**.
2. Open **http://192.168.4.1**.
3. Put the Satechi remote in pairing mode.
4. Tap **Scan**.
5. Find `POP Multimedia` (or the Satechi-looking device) and tap **Connect**.
6. Press the media buttons. The dashboard shows the friendly key guess and the exact raw HID bytes.

## Pairing notes

The firmware accepts SSP confirmation automatically and replies `0000` to a legacy four-digit PIN request. If the Satechi is still paired with a phone/computer, forget/unpair it there first and re-enter pairing mode before scanning from the ESP32.

## Hardware compatibility

Works with chips/modules that have Bluetooth Classic, especially ESP32-WROOM-32 / original ESP32.

It will **not** provide this Classic Bluetooth HID host function on BLE-only variants such as ESP32-C3, ESP32-C6, and ESP32-S3.

## If a button is not named correctly

That is expected for some remote revisions. The dashboard always shows `Raw HID`. Press each button and use the raw value to identify it. Add the usage to `decode_consumer_usage()` in `main/main.c` if you want a custom label.

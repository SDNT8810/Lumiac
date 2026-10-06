LUMIAC QUICK START
==================

1) Install/open ESP-IDF v5.3.1.
2) Open a terminal in this folder.
3) Run:

   idf.py set-target esp32
   idf.py fullclean
   idf.py build
   idf.py -p COM5 flash monitor

   Change COM5 to your ESP32 serial port.

4) Connect phone/PC Wi-Fi:
   Name: Lumiac
   Password: 12345678

5) Open:
   http://192.168.4.1

6) Put Satechi remote in pairing mode.
7) Dashboard -> Scan Bluetooth -> Pair / Connect.
8) Press buttons and watch Last key + Raw HID + logs.

BOARD REQUIREMENT:
Original ESP32 with Bluetooth Classic (ESP32-WROOM-32 / DevKitC / NodeMCU-32S).

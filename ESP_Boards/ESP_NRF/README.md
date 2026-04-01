# ESP_NRF

This project is the third control option:

- `ESP_NRF` on the Octopus side
- `Arduino_Nano_NRF24/Remote_Control` on the handheld side
- browser / phone / PC control through the ESP32 hotspot, like `ESP_Octopus`

What it keeps from `ESP_Octopus`
- Wi-Fi access point and browser control page
- live logs in the web portal
- Octopus UART communication
- PWM lamp control
- M215 random-code discovery
- staged Octopus recovery with UART reinit and reset-line pulse

What changes
- the remote-control device is no longer `ESP_Remote`
- remote commands are received from an NRF24 module connected to the ESP32
- remote-online state is based on recent NRF24 traffic

Remote compatibility
- Compatible with `Arduino_Nano_NRF24/Remote_Control`
- Not intended for `ESP_Remote`

Build
- PlatformIO project for `esp32dev`
- required libraries:
  - `ArduinoJson`
  - `WebSockets`
  - `RF24`

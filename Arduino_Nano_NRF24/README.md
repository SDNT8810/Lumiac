# Arduino Nano + NRF24 Alternative

This directory contains a Wi-Fi-free alternative to the ESP-based system.

Projects
- `Remote_Control`: battery-powered Nano remote using deep sleep and NRF24
- `Octopus_Board`: Nano receiver using NRF24, PWM lamp output, serial to Octopus, and hardware reset recovery

Build
- Each subdirectory is a separate PlatformIO project.
- Shared packet definitions are in `Common/NrfOctopusProtocol.h`.

Libraries
- `RF24`

Long-term design choices
- fixed-size radio packets
- no JSON
- no `String`
- watchdog-based self-recovery
- staged serial/radio recovery on the receiver
- deep sleep on the remote
- aggressive `5s` remote idle timeout, based on the older transmitter behavior
- deeper remote sleep entry by powering down the radio and unused AVR peripherals before sleep

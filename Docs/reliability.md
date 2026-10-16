# Controller reliability, 2026-10-16

Slow DNS, TLS or gateway replies previously blocked the same loop that reads
Octopus acknowledgements, streams the next move, and checks connection deadlines.
A delayed reply could therefore look like a board failure. This is a software
failure path found in the code, not confirmation of a particular hardware fault.

Both controller variants now exchange serialized messages with a separate cloud
transport. ESP32 uses a FreeRTOS task. ESP-12S uses a separate continuation in the
pinned Arduino ESP8266 3.1.2 core: its network yield points return to hardware
control, restoring the Arduino context before the hardware loop continues. Only
one HTTPS operation runs at a time; firmware downloads wait for sync to finish.
TLS certificate checks remain enabled. Responses, timeouts, memory use, and retry
frequency are bounded. Temporary TLS allocations no longer trigger the retained
heap watchdog. Serial input is drained again before connection deadlines and
overlong lines are discarded rather than interpreted as partial messages.
ESP-12S keeps the public certificates in flash and retains 16 local log entries
to leave RAM for its cloud continuation and TLS handshake.

The controller reports reset cause, uptime, free heap, cloud failures and request
duration under `reliability` in `/api/state` and remote status. The last connection
or command failure is saved across reboots and shown on the remote panel.
An unexpected Octopus startup banner invalidates homing and stops the old program;
it requires a new Home command. Genuine board faults remain faults.

Validation: host tests compile the production transport for both ESP families,
hold an HTTP request across 86,400 simulated controller ticks, then verify retry,
bounded response size and TLS resource ownership. ESP8266 host continuations use
fibers; this does not replace an endurance run on the physical ESP and mechanics.

`RF` retains the existing gearbox calibration. A controller from `onetoone`
reports that branch, so the same remote panel builds its matching branch for
Full update. Old firmware without a branch field continues to use `RF`.

## ESP-12S connection correction, 2026-10-16.3

`HTTPS status -1` was the HTTP client's connection-failure code, not a response
from the gateway. The initial short network deadlines could reject a slow Wi-Fi
connection. Sync and parked firmware downloads now share a 10-second ESP32 TCP
deadline, 20-second ESP32 TLS deadline and 15-second HTTP deadline. On ESP8266 the
HTTP timeout also covers TCP connection setup; BearSSL allows its normal
15-second handshake. The ESP-12S CPU runs at its supported 160 MHz setting to
reduce certificate-verification time. Control still runs independently of HTTPS.
Sync sends its existing status bytes directly instead of allocating a second
copy of the packet while BearSSL holds its handshake buffers.

Failures now include the TLS library's actual reason when available. The local
network page's Connection details shows firmware, free memory, router gateway,
DNS, signal level and UTC time. This helps distinguish memory, routing, time and
certificate failures. Certificate verification stays enabled.

The live gateway, bundled CA chain with TLS 1.2, and configured device key were
verified from this PC. A successful handshake from the physical ESP-12S still
needs confirmation after installing the new ESP firmware. Octopus calibration
and machine identity do not change with this correction.

# CANLoopback

[日本語](README.ja.md)

Checks the CAN controller with no transceiver and no bus: in silent loopback it sends a 4-byte frame with standard
identifier 0x123 each second and prints what comes back on the hardware `Serial` (115200 baud). No wiring. For a real
bus, drop `setLoopback()` and put a transceiver on RX / TX. See [Arduino_CAN](../../README.md).

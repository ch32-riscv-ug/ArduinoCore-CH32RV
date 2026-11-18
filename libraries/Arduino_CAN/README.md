# Arduino_CAN

[日本語](README.ja.md)

CAN 2.0B through ArduinoCore-API's `HardwareCAN`. As on the official Arduino cores (UNO R4 and others),
`#include <Arduino_CAN.h>` gives `CAN` (and `CAN1` on series with two).

```cpp
#include <Arduino_CAN.h>

void setup() {
  CAN.begin(CanBitRate::BR_500k);           // the default pins (the variant's CH32RV_CAN1_RX / _TX)
}

void loop() {
  uint8_t data[] = {1, 2, 3};
  CAN.write(CanMsg(CanStandardId(0x123), sizeof data, data));
  while (CAN.available()) {
    CanMsg m = CAN.read();                   // m.id, m.data_length, m.data
  }
  delay(100);
}
```

## Where it runs

The bxCAN of V20x, V205, V30x, V4x7, L103 / M103 and H41x (pin routes generated from device-data). On a series
without CAN (V00x, X035 ...) the header defines nothing.

## Things worth knowing

- The bus needs a transceiver (TJA1050, SN65HVD230 ...) on RX / TX.
- Every frame is received (one filter that passes all), standard and extended identifiers alike
  (`CanStandardId()`, `CanExtendedId()`).
- `write()` returns 0 when the three transmit mailboxes are full, -1 before `begin()`.
- Received frames wait in the hardware FIFO (3) and in RAM (8). No interrupt is used: call `available()` or `read()`
  often enough to move them out of the FIFO (more than 3 waiting there are lost).
- `begin(bitrate)` takes any bit rate the CAN clock (APB1) divides exactly into 8 - 25 time quanta (sample point
  near 87.5 %); others are `false`.
- `setLoopback(true, true)` before `begin()` hands every frame sent back to the receiver and keeps TX recessive
  (silent): the controller is checked with no transceiver and no bus.
- `setPins(rx, tx)` moves it to a route of the variant's `CH32RV_CANn_ROUTES` (before `begin()`).
- `txErrors()` / `rxErrors()` / `busOff()` read the error counters and bus-off. Bus-off recovers by itself (ABOM).

## What was checked

The silent loopback test (selfapi::test_can_loopback) passes on V203, V205 and H417 (2026-11-18): at 125 k, 500 k and
1 Mbit/s, 20 frames - standard and extended identifiers in turn, 0 to 8 bytes - come back whole, with no error
counted. Sending and receiving on a real bus (a transceiver and a second node) is not checked yet.

On H417 the receive FIFO's output has to be waited for after it is released, or the same frame is read twice (the
library waits); the FIFO also keeps its frames through the initialisation mode, so `begin()` lets them go.

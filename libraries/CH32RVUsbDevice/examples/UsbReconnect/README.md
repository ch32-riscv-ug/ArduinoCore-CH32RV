# UsbReconnect

[日本語](README.ja.md)

A logger over USB CDC that survives the host going away. A reading every 200 ms is kept in a small ring (32) while no
terminal has the port open, and sent oldest first once one opens it again, with how many were lost to the ring's size.
Bus events (reset, configured, suspended, resumed) are logged on the hardware UART.

## What you need and the wiring

- A part with a USB device port, and a USB cable to the PC.
- A UART monitor at 115200 bps, 8N1, on `RX` / `TX` (the USB side comes and goes, so the log goes here).

## What to do and what to expect

1. Open the USB serial port in a terminal on the PC: `reading 0`, `reading 1` ... arrive every 200 ms.
2. Close the terminal, wait 3 s or so and open it again: what was kept comes first.

   ```
   resumed: 15 kept, 0 lost while closed
   reading 37 (kept)
   ...
   ```

   Kept closed for 10 s or so, the readings past 32 are counted as `lost`.
3. Unplug and replug the USB cable: the UART shows `bus: reset`, `bus: configured` and the like. A sleeping PC gives
   `suspended`.

## The API and the flow

| API | Its use here |
|---|---|
| `SerialUSB.connected()` | True while a terminal has the port open (DTR up). While closed, readings go to the ring instead. |
| `SerialUSB.setTxTimeoutMs(50)` | How long to wait for a host that stopped reading; after it, bytes are dropped as on an open line. |
| `USBDevice.onBusEvent()` | Reset, configured, suspended and resumed, in order, from `deliverEvents()`. |

Closed is told by DTR. A terminal that never raises DTR (some tools) keeps `connected()` false even when open.

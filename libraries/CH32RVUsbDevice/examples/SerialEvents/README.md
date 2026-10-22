# SerialEvents

日本語: [README.ja.md](README.ja.md)

Shows where the USB callbacks run. `onRx()`, `onLineState()` and `onLineCoding()` run only inside
`deliverEvents()` in `loop()`. Every 10 s `loop()` is busy for 3 s in `delay()`: USB keeps working
meanwhile, but no callback runs.

## Hardware and connections

A board with USB Device support and a USB data cable, a PC serial terminal (CDC), and a 115200-baud
8N1 UART monitor on the `RX` / `TX` pads.

## Steps and expected results

1. Open the UART monitor, then connect USB to the PC: `SerialEvents ready`.
2. Open the CDC port in the terminal: the UART shows `LINE_CODING baud=...`, `LINE_STATE dtr=1 rts=...`
   and `DELIVER ran=...`.
3. Send `abc`: the characters come back, and the UART shows `RX available=3 echoed=3` and
   `DELIVER ran=1` (one line per character if the terminal sends them one at a time).
4. While the UART shows `BUSY 3 s: type now, the callbacks wait`, type a few characters. Nothing comes back yet.
5. After `BUSY end` they all come back at once, with a single `RX available=n echoed=n` line. Nothing
   was dropped: the data waited in the port's buffer.

## Settings and limits

- No callback runs until `deliverEvents()` is called. Inside `delay()` USB transfers go on; callbacks do not.
- Several notifications of one kind before a delivery arrive as one. `onRx()` gets what can be read then.
- When the receive buffer (256 bytes at Full Speed) is full, the PC waits before sending more. No data is lost.
- `SerialUSB` is the USB CDC port, `Serial` the UART.

## Related documentation

- [How a sketch is put together (Japanese)](../../../../docs/sketch-guide.ja.md): interrupts, not waiting, callbacks
- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)

[SerialEvents.ino](SerialEvents.ino)

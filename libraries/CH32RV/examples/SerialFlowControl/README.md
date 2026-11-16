# SerialFlowControl

[日本語](README.ja.md)

Hardware flow control on `Serial`: RTS tells a fast sender to wait while the sketch reads slowly, and CTS holds what
the board sends until the other side is ready.

## Hardware and connections

A USB-UART adapter with RTS / CTS (FT232R, CP2102N and the like). Board TX to adapter RX, board RX to adapter TX, board
RTS to adapter CTS, board CTS to adapter RTS, common GND. The board's CTS / RTS pads are the USART's own on Serial's
route: see `CH32RV_SERIAL1_CTS_ROUTES` / `_RTS_ROUTES` in the variant's `pins_arduino.h` (CH32V003's default route:
CTS PD3, RTS PC2). Turn hardware flow control on in the terminal.

## Steps and expected results

Open at 115200 8N1 with RTS/CTS. The board prints `Flow control on`. Paste a long text: it comes back whole, slowly
(5 ms a byte), with nothing lost - the adapter pauses whenever 32 bytes wait unread. Without flow control in the
terminal, the same paste loses characters once the 63-byte buffer is full.

If the board prints `This route has no CTS / RTS pads`, the USART on Serial's route has no CTS / RTS (some UART4-8),
and `setHwFlowCtrlMode()` returned false.

## API and processing flow

`setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS_RTS, 32)` before or after `begin()`: from 32 unread bytes on, the USART keeps
the next byte in its data register and raises RTS at its stop bit; `read()` lets it in and RTS falls again. CTS is
pulled up, so with nothing connected the board sends nothing. `setPins(-1, -1, cts, rts)` names other pads (they must
still be the USART's CTS / RTS on that route).

## Adapting the example

`UART_HW_FLOWCTRL_RTS` or `_CTS` alone uses only that pad. A smaller threshold gives the sender more time to react;
the largest is the buffer (63 bytes by default, `CH32RV_SERIAL_RX_BUFFER_SIZE` - 1).

## Related documentation

- [Sketch](SerialFlowControl.ino)
- [Basic example setup and index](../README.md)

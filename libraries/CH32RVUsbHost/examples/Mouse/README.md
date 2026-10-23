# Mouse

日本語: [README.ja.md](README.ja.md)

UART inspection of USB mouse relative movement, wheel and button bitmap.

## Hardware and connections

A USB mouse, powered host wiring and a 115200-baud UART monitor.

## Steps and expected results

1. Move the mouse to see `x=... y=... wheel=... buttons=0x...`.
2. Test the wheel and buttons and observe the values.
3. No output while stationary is normal.

## Settings and limits

The report descriptor determines field layout, but unusual proprietary features may need additional handling. The board prints values; it does not forward mouse input to the PC. Rate-limit logging if UART output cannot keep up.

## API usage and adaptation

| API or value | Use in this example |
|---|---|
| `usb.onMouse(callback)` | Delivers decoded mouse-report events through `deliverEvents()`. |
| `e.x` / `e.y` / `e.wheel` | Relative amounts, not absolute screen coordinates or pixels. |
| `e.buttons` | The current button bitmap, not a click count. |

XOR the previous and current bitmap to find button transitions. Use event data during the callback and save necessary values in your own state.

For missing proprietary controls, inspect the interface with [DeviceInfo](../DeviceInfo/README.md) and its report descriptor. Printing every high-rate event can slow processing; accumulate movement and report periodically if needed.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbHost API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[Mouse.ino](Mouse.ino)

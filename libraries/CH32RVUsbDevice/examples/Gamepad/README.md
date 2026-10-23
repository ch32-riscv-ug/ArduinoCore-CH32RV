# Gamepad

日本語: [README.ja.md](README.ja.md)

Generated HID gamepad input with six axes, a hat and a button bitmap.

## Hardware and connections

A PC game-controller viewer and UART monitor. No physical joystick is needed.

## Steps and expected results

1. Axes, hat and buttons change about every 500 ms.
2. Compare the PC state with UART `GAMEPAD_TX` lines.
3. The first eight buttons cycle and axis test values span approximately -127 to 127.

## Settings and limits

The report supports 32 buttons; this example cycles eight. For physical analog controls, map ADC resolution and center values into the signed axis range.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `send(x, y, z, rz, rx, ry, hat, buttons)` | Sends all six axes, the hat and buttons together. Note the axis argument order. |
| Hat constants | Specify neutral, cardinal and diagonal positions; the value is not an angle in degrees. |
| `buttons` | A bitmap: `1UL << 0` is the first button. OR bits together for multiple buttons. |
| `sendReport(report)` | Sends state assembled in a report structure. |

Map ADC readings according to their configured resolution, with the joystick center at zero. Add a dead zone for center noise, clamp the result, then convert to `int8_t`.

Reports describe full state, not changes. Clear a button bit on release and send a neutral hat when released. If `send()` fails, check enumeration and pacing, then send the latest state on a later iteration.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[Gamepad.ino](Gamepad.ino)

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

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[Gamepad.ino](Gamepad.ino)

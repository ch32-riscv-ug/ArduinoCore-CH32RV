# HostHidReports

[日本語](README.ja.md)

Shows the HID reports the keyboard and mouse examples leave out: a gamepad's report (when it changes), media keys
(consumer control: volume, play and so on) and system keys (power, sleep). Each comes with the device it came from
(address, interface, VID:PID), so two devices behind a hub can be told apart.

## What you need and the wiring

- A part with a USB host port (USBFS), and 5 V out to the device (VBUS).
- A USB gamepad, a keyboard with media keys, or a remote and the like.
- A UART monitor at 115200 bps (`Serial`).

## What to do and what to expect

Moving a gamepad's buttons or sticks prints a line each time the report's bytes change. Pressing and releasing a
keyboard's volume key prints two `consumer` lines.

```
[dev 1 if 0 054c:05c4] gamepad 01 80 80 80 80 08 00 00
[dev 2 if 1 046d:c52b] consumer 0x0e9 pressed
[dev 2 if 1 046d:c52b] consumer 0x0e9 released
[dev 2 if 1 046d:c52b] system 0x82 pressed
```

## The API and the flow

| API | Its use here |
|---|---|
| `onGamepad()` | A gamepad's report, printed only when `changed` is true. The layout differs by pad, so the raw bytes of `reportData` are shown. |
| `onConsumerControl()` | A Consumer page usage (0xE9 volume up, 0xCD play/pause and so on), pressed or released. |
| `onSystemControl()` | A Generic Desktop page usage (0x81 power down, 0x82 sleep, 0x83 wake up). |
| `address` / `interfaceNumber` / `vid` / `pid` | Which device and interface it came from. |

The callbacks run from `deliverEvents()` in `loop()`.

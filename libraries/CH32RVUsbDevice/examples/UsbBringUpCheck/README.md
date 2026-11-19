# UsbBringUpCheck

[日本語](README.ja.md)

The first sketch for a new board or cable: a USB device bring-up check (the role of EspUsbDevice's
EspUsbDeviceBringUpCheck). It tells "the device started" (`BEGIN ok`) from "the host configured it" (`MOUNTED`),
follows suspend and resume, and checks the host -> device direction (the keyboard's lock LEDs). When no host has
enumerated it after 10 s it prints a checklist. A HID keyboard is registered but never types.

## What you need and the wiring

- A part with a USB device port, and a USB cable to the PC (on the chip's own USB connector).
- A UART monitor at 115200 bps, 8N1, on `RX` / `TX`.

## What to do and what to expect

```
=== CH32RVUsbDevice bring-up check ===
CORE ch32rv 0.9.0  F_CPU 48000000 Hz
BEGIN ok
DESCRIPTOR config_bytes=34
VID_PID 1209:0001
Now connect the USB connector to a host and watch for MOUNTED.
MOUNTED t=812ms speed=full
HOST_OUTPUT_REPORT leds=0x02 num=0 caps=1 scroll=0
STATUS mounted=1 suspended=0 t=5000ms leds=0x02
```

- `BEGIN failed` is the board's own problem (no host is involved yet); the name printed narrows it down.
- `BEGIN ok` without `MOUNTED` prints `CHECKLIST` after 10 s (cable, connector, the CPU Clock menu, power, the host's
  log, the descriptors).
- Pressing CapsLock on the PC's keyboard prints `HOST_OUTPUT_REPORT`: the host -> device direction works too.
- A sleeping PC gives `SUSPENDED`, and `RESUMED` when it wakes.

## The API and the flow

| API | Its use here |
|---|---|
| `device.begin(config)` / `lastErrorName()` | Whether it started, and why not. |
| `device.ready()` | The host chose the configuration (enumeration done). |
| `device.speed()` / `device.suspended()` | The speed and suspend. `ready()` stays true while suspended. |
| `keyboard.onOutputReport()` / `ledState()` | The lock LEDs the host sends: the host -> device direction. |

The descriptors themselves are [DescriptorDump](../DescriptorDump/README.md)'s.

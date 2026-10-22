# MIDI

日本語: [README.ja.md](README.ja.md)

UART logging of USB MIDI messages, with periodic transmission of Note 60.

## Hardware and connections

A keyboard/synthesizer exposing a USB MIDI device port, host VBUS wiring and a UART monitor. Use a device port, not another host port.

## Steps and expected results

1. Operate the device and inspect UART cable/status/data fields.
2. Once ready, Note 60 with velocity 100 is sent on API channel 0 about once a second.
3. Note Off follows roughly 200 ms later. A device with a sound engine can play it.

## Settings and limits

API channel 0 is conventional channel 1. A MIDI controller without a sound engine will not produce audio from received notes. Host processing continues inside delay(200); the callbacks wait for deliverEvents() at the top of loop().

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbHost API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MIDI.ino](MIDI.ino)

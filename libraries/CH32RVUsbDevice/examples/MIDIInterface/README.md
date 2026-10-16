# MIDIInterface

日本語: [README.ja.md](README.ja.md)

A bidirectional bridge between USB MIDI and 31250-baud UART MIDI messages.

## Hardware and connections

Use appropriate MIDI input/output circuits on Serial1 RX=PA3 and TX=PA2, including an isolated MIDI input. Do not wire a DIN MIDI cable directly to GPIO. Verify that Serial logging and Serial1 use distinct USART routes.

## Steps and expected results

1. Connect a PC MIDI monitor and a UART-side MIDI device through the interface circuits.
2. Channel messages from UART appear on the PC and as `DIN_TO_USB` logs.
3. PC messages produce `USB_TO_DIN` logs and output to the MIDI device.

## Settings and limits

The UART-to-USB parser only handles explicit-status channel voice messages. It is not a complete running-status, SysEx or system-realtime parser. It waits up to 10 ms for remaining bytes and handles one UART message per loop; replace the parser for broader protocol coverage.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MIDIInterface.ino](MIDIInterface.ino)

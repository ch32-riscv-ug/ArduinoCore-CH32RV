# MIDI

日本語: [README.ja.md](README.ja.md)

Generated USB MIDI 1.0 notes and control changes, with received packets logged on UART.

## Hardware and connections

A PC MIDI monitor, optionally a software synthesizer, and a UART monitor.

## Steps and expected results

1. Select the board as a MIDI input on the PC.
2. Notes 60→64→67→72 advance about once a second with velocity 96 and changing CC74.
3. Send MIDI from the PC to see UART `MIDI_RX` packets.

## Settings and limits

API channel 0 corresponds to conventional MIDI channel 1; cable defaults to 0. MIDI messages are not PCM audio, so audible output requires a synthesizer.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MIDI.ino](MIDI.ino)

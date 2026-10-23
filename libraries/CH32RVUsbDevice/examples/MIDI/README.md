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

## API usage and adaptation

| API | Use in this example |
|---|---|
| `noteOn(channel, note, velocity)` / `noteOff(...)` | Channel is zero-based; note and velocity are MIDI seven-bit values. Send note endings as well as starts. |
| `controlChange(channel, control, value)` | Sends a controller number and value; its effect depends on the sound generator. |
| `readPacket(packet)` | Removes one USB MIDI event packet, returning `true` when available. |
| `packet.header` | Contains cable information in its upper part and CIN in its lower part, separately from the MIDI status byte. |

User-facing channel 1 is API channel `0`. Inspect Note On, Note Off and CC in a MIDI monitor before testing a sound generator. Send from the PC as well and check `MIDI_RX`.

The periodic log records a transmission attempt, not the success of every send. Check boolean results for performance use; a lost Note Off can leave a note sounding.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MIDI.ino](MIDI.ino)

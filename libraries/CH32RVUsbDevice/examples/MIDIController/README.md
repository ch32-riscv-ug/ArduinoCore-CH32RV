# MIDIController

日本語: [README.ja.md](README.ja.md)

A potentiometer controlling CC74 and a button controlling Note 60 over USB MIDI.

## Hardware and connections

Wire a potentiometer between the board ADC supply/reference level and GND, with its wiper on A0. Connect a button between PA1 and GND. Use PC MIDI and UART monitors.

## Steps and expected results

1. Turn the potentiometer: `MIDI_CC control=74 value=...` logs values sent to the PC. With the core's default 10-bit ADC, the example's mapping yields roughly 0–31.
2. Press PA1 for Note On 60 on API channel 0; release for Note Off.
3. UART logs `MIDI_BUTTON ON/OFF`.

## Settings and limits

The mapping assumes a 12-bit ADC, while this core defaults to 10 bits. For the full 0–127 CC range, add `analogReadResolution(12);` before the first analogRead in setup, or change the mapping input maximum to 1023. ADC readings use a 15/16 smoothing filter. The button has no dedicated debounce; add it if the physical switch generates repeated events.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MIDIController.ino](MIDIController.ino)

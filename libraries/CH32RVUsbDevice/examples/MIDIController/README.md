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

## API usage and adaptation

| API | Use in this example |
|---|---|
| `analogRead(CONTROLLER_PIN)` | Reads the potentiometer. Match ADC resolution to the input range of `map()`. |
| `MIDI.controlChange(channel, control, value)` | Sends only when the smoothed value changes. Channel is zero-based. |
| `noteOn()` / `noteOff()` | Start a note on button press and stop it on release. With `INPUT_PULLUP`, a press reads Low. |

Connect the potentiometer ends to GND and a supply within the ADC input range, with its wiper on `CONTROLLER_PIN`. Apply resolution before the initial reading as well, and check full CC travel in a monitor. ADC smoothing is not button debounce.

The sample updates remembered CC/button state before sending, so a failed value is not automatically retried while input remains unchanged. Update the last-sent state only on success, or retain pending state for the next iteration.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MIDIController.ino](MIDIController.ino)

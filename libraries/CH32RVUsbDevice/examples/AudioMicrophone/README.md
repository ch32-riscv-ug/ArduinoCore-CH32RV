# AudioMicrophone

日本語: [README.ja.md](README.ja.md)

A UAC1 capture source generating a 440-Hz test tone instead of using a physical microphone.

## Hardware and connections

A PC recording application and UART monitor. No physical microphone is needed. See the accompanying `sketch.yaml` for build targets and memory conditions.

## Steps and expected results

1. Select the USB Microphone as a PC recording device.
2. Record 32000-Hz, signed 16-bit mono PCM.
3. Check for an approximately 440-Hz tone.
4. UART `AUDIO_CAPTURE enabled=1 alt=...` marks the host starting capture.

## Settings and limits

A 64-entry table and 32-bit phase generate samples paced by elapsed time; catch-up after a stall is limited to 10 ms. This demo does not inspect partial capture.write results. Real applications should handle FIFO capacity and partial writes.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `audio.addCaptureStream()` | Adds a Device-to-Host recording stream. |
| `capture.addFormat({...})` | Sets the advertised PCM format before USB startup. |
| `capture.write(samples, byteCount)` | Returns bytes accepted by the FIFO. Its length argument is bytes, not samples. |
| `capture.availableForWrite()` | Reports available bytes when handling partial writes. |

Computing sample count from elapsed `micros()` prevents pitch from depending on loop speed. Phase advances the waveform table. Change both advertised format and phase increment when changing sample rate.

For a real ADC, match sampling time, signed representation, byte order and channel order to the PCM format. Retain or explicitly count discarded data after a short write. USB background processing cannot maintain continuous recording if the sketch stops supplying PCM.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[AudioMicrophone.ino](AudioMicrophone.ino)

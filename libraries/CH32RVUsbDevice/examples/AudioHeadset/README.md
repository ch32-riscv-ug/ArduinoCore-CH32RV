# AudioHeadset

日本語: [README.ja.md](README.ja.md)

A UAC1 playback/capture loopback returning PC playback PCM through the recording stream.

## Hardware and connections

A PC application supporting simultaneous playback and recording, plus a UART monitor. No physical headset is needed. See the accompanying `sketch.yaml` for build targets and memory conditions.

## Steps and expected results

1. Select the USB Headset for playback and capture.
2. Start both at 32000-Hz, 16-bit mono.
3. Confirm that the playback test tone appears in the recording.
4. UART `AUDIO_STREAM target=... enabled=...` reports stream states.

## Settings and limits

Both streams share a clock and sample rate. Audio travels USB→RAM FIFO→USB with no analog I/O. Partial capture writes are not handled by this demo, so differing start times or overload can lose samples.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `addPlaybackStream()` / `addCaptureStream()` | Add receive and transmit streams. |
| Each stream's `addFormat()` | Registers matching PCM formats so no conversion is needed. |
| `playback.read()` / `capture.write()` | Forward only received bytes; both return actual processed lengths. |
| `audio.pollEvent()` | Use `target` and `enabled` to distinguish start/stop of each direction. |

The host starts playback and capture independently. A capture FIFO can fill while only playback is active. Decide whether to retain or discard old PCM according to both stream states.

When processing audio, preserve channel ordering and signed PCM range. This sample does not retain the unwritten remainder if `capture.write()` returns less than `received`. For lossless forwarding, add a pending buffer and avoid overwriting unsent data.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[AudioHeadset.ino](AudioHeadset.ino)

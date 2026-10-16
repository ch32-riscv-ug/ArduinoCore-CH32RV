# AudioHeadset

日本語: [README.ja.md](README.ja.md)

A UAC1 playback/capture loopback returning PC playback PCM through the recording stream.

## Hardware and connections

A PC application supporting simultaneous playback and recording, plus a UART monitor. No physical headset is needed. Profiles require 48 KiB flash.

## Steps and expected results

1. Select the USB Headset for playback and capture.
2. Start both at 32000-Hz, 16-bit mono.
3. Confirm that the playback test tone appears in the recording.
4. UART `AUDIO_STREAM target=... enabled=...` reports stream states.

## Settings and limits

Both streams share a clock and sample rate. Audio travels USB→RAM FIFO→USB with no analog I/O. Partial capture writes are not handled by this demo, so differing start times or overload can lose samples.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[AudioHeadset.ino](AudioHeadset.ino)

# AudioSpeaker

日本語: [README.ja.md](README.ja.md)

A UAC1 playback sink counting received PCM bytes on UART; it does not drive a speaker or DAC.

## Hardware and connections

A PC audio player and UART monitor. No audio hardware is needed. See the accompanying `sketch.yaml` for build targets and memory conditions.

## Steps and expected results

1. Select the USB Speaker as a PC playback device.
2. Play 16000-Hz, signed 16-bit stereo PCM.
3. Observe `AUDIO_PLAYBACK enabled=1` and `AUDIO_RX`; continuous PCM is approximately 64000 bytes/s.
4. Host mute/volume changes generate corresponding events.

## Settings and limits

Format fields are `{sampleRate, channels, bytesPerSample, bitsPerSample}`. Forward read PCM to DAC/I2S for physical audio. Volume is a control value in 1/256 dB, not automatic PCM processing. The format fits X035 at 64 B/ms; raising it to 48-kHz stereo requires a suitable controller/endpoint.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `audio.addPlaybackStream()` | Adds Host-to-Device audio; retain the returned reference. |
| `playback.addFormat({...})` | Registers PCM format before USB startup; field meanings are given above. |
| `playback.read(pcm, sizeof(pcm))` | Returns actual bytes read, which may be fewer than the array size. |
| `audio.pollEvent(event)` | Polls stream, mute and volume events instead of notification callbacks. |
| `playback.stats()` | Inspects transfer and overrun information to check whether reading keeps up. |

A PCM frame contains one sample for every channel. Bytes per second are `sampleRate × channels × bytesPerSample`. When forwarding to a DAC, distinguish bytes from samples and process only the returned data.

Enumeration alone does not start audio. Check stream-start events from the host. If data is lost, reduce long UART output and heavy work so the FIFO can be drained regularly.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[AudioSpeaker.ino](AudioSpeaker.ino)

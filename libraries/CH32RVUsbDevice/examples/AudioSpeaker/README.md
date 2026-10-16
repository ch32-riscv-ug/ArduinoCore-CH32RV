# AudioSpeaker

日本語: [README.ja.md](README.ja.md)

A UAC1 playback sink counting received PCM bytes on UART; it does not drive a speaker or DAC.

## Hardware and connections

A PC audio player and UART monitor. No audio hardware is needed. Generated profiles require at least 48 KiB flash.

## Steps and expected results

1. Select the USB Speaker as a PC playback device.
2. Play 16000-Hz, signed 16-bit stereo PCM.
3. Observe `AUDIO_PLAYBACK enabled=1` and `AUDIO_RX`; continuous PCM is approximately 64000 bytes/s.
4. Host mute/volume changes generate corresponding events.

## Settings and limits

Format fields are `{sampleRate, channels, bytesPerSample, bitsPerSample}`. Forward read PCM to DAC/I2S for physical audio. Volume is a control value in 1/256 dB, not automatic PCM processing. The format fits X035 at 64 B/ms; raising it to 48-kHz stereo requires a suitable controller/endpoint.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[AudioSpeaker.ino](AudioSpeaker.ino)

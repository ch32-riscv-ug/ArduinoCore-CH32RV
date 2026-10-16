# AudioMicrophone

日本語: [README.ja.md](README.ja.md)

A UAC1 capture source generating a 440-Hz test tone instead of using a physical microphone.

## Hardware and connections

A PC recording application and UART monitor. No physical microphone is needed. Profiles require at least 48 KiB flash.

## Steps and expected results

1. Select the USB Microphone as a PC recording device.
2. Record 32000-Hz, signed 16-bit mono PCM.
3. Check for an approximately 440-Hz tone.
4. UART `AUDIO_CAPTURE enabled=1 alt=...` marks the host starting capture.

## Settings and limits

A 64-entry table and 32-bit phase generate samples paced by elapsed time; catch-up after a stall is limited to 10 ms. This demo does not inspect partial capture.write results. Real applications should handle FIFO capacity and partial writes.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[AudioMicrophone.ino](AudioMicrophone.ino)

# AudioStreams

日本語: [README.ja.md](README.ja.md)

Lists a USB audio device's PCM streams (UAC1 / UAC2: a USB headset, microphone, speaker, an ESP32 running an audio
device ...), plays a 440 Hz sine on its output and prints the input's peak level every second on the UART.

## What you need and the wiring

A board with a host port, host wiring that supplies 5 V VBUS, a USB audio device, a 115200 bps UART monitor. On a
full-speed port, 48 kHz 16-bit stereo is a 192-byte isochronous transfer every millisecond.

## What to do and what to expect

Plug the device in: the stream list (direction, channels, bits, rate, UAC version) is printed and the output plays
440 Hz. Speak into the microphone and `input peak` rises. `output underruns` counts the times the output queue ran
dry (it grows when loop() stalls).

## Settings and limits

- One input and one output. A 16-bit stream is chosen, at 48 kHz or else the device's first rate.
- No feedback endpoint (an asynchronous device is fed at the nominal rate). No volume / mute (Feature Unit) yet.
- The PCM rings are `CH32RV_USB_HOST_AUDIO_RING_SIZE` (2048 bytes by default), from the heap at the start.

## API and where to change it

| API | Here |
|---|---|
| `usb.getAudioStreams()` | Reads the device's stream formats and rates. |
| `usb.audioInputStart()` / `onAudioData()` | Starts the input; received PCM comes inside `deliverEvents()`. |
| `usb.audioOutputStart()` / `audioSend()` / `audioOutputAvailableForWrite()` | Starts the output and queues PCM as room allows. |
| `usb.audioOutputUnderruns()` | Silence sent for lack of PCM. |

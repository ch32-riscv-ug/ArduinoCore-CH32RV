# MidiEcho

[日本語](README.ja.md)

Every USB MIDI packet the PC sends comes back unchanged. A packet the IN queue cannot take yet is held and the next
one is read only once it is sent, so none is lost (as EspUsbDevice's MidiEcho).

## What you need and the wiring

A part with a USB device port, and a USB cable to the PC. Failures show on a 115200 bps UART monitor (`RX` / `TX`).

## What to do and what to expect

Send notes or CCs to the board's MIDI output from a DAW or a MIDI monitor tool: the same come back from the board's
MIDI input. Sent fast and many, they all come back in order.

## The API and the flow

| API | Its use here |
|---|---|
| `midi.configure(config)` | The cable counts and the packets each receive and send queue holds. Before `device.begin()`. |
| `midi.readPacket(packet)` | Takes one received packet; false when none. |
| `midi.writePacket(packet)` | Sends; false when the queue is full, and the packet is held and sent again on a later turn. |
| `device.ready()` | Whether the host chose the configuration; on an unplug the held packet is dropped. |

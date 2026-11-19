# MidiChannelsAndReceive

[日本語](README.ja.md)

A device with two USB MIDI ports (cables), to show a MIDI channel (1-16) apart from a USB cable (a port). Notes received
on port 1 light an LED by their velocity, every message is logged on the UART with its cable and channel, and notes go
back out on port 2 on the channel they came in on.

## What you need and the wiring

- A part with a USB device port, and a USB cable to the PC.
- An LED with a resistor on `LED_PIN` (`PA1` by default). On a pin with PWM the velocity becomes the brightness.
- A UART monitor at 115200 bps, 8N1, on `RX` / `TX`.

## What to do and what to expect

The PC sees two MIDI inputs and two outputs (named by the OS). Send a Note On on channel 3 to the first output from a
DAW or a MIDI monitor tool: the LED lights, the UART shows the lines below, and the same note arrives at the second
input on channel 3.

```
USB MIDI with 2 ports ready
RX port 1 channel 3 note-on 60 100
RX port 1 channel 3 note-off 60 0
```

What is sent to the second output is only logged (`RX port 2 ...`).

## The API and the flow

- `CH32RVUsbDeviceMidi MIDI(device, 2, 2)` gives two cables each way. A cable is a USB port, a separate MIDI port on the
  host. A channel is the number inside a MIDI message, 16 on one port.
- In a received packet the header's high 4 bits are the cable and the status byte's low 4 bits the channel (the API's
  channel counts from 0).
- `noteOn(channel, note, velocity, cable)` picks the port to send on with its last argument.

Checked on CH32H417 over SuperSpeed (2026-11-20, USB-MIDI packets from PyUSB): a note on and off on port 1, channel 3,
came back on port 2, channel 3; a note on port 2 and a control change were not echoed.

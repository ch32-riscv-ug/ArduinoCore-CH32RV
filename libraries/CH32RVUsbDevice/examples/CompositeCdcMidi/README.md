# CompositeCdcMidi

[日本語](README.ja.md)

A composite device with two functions: a serial port (CDC) named `Console` and a MIDI port, each echoing what the PC
sends. Each function has its own buffers and back-pressure, so a busy one does not hold up the other (as EspUsbDevice's
CompositeCdcMidi).

## What you need and the wiring

A part with a USB device port, and a USB cable to the PC. Failures show on a 115200 bps UART monitor (`RX` / `TX`).

## What to do and what to expect

The PC sees one serial port (interface name `Console`) and one MIDI input and output (product name
`CDC and MIDI echo`). Characters typed in a terminal come back; notes sent to the MIDI output come back from the MIDI
input.

## The API and the flow

| API | Its use here |
|---|---|
| `CH32RVUsbDeviceCdcSerial console(device, "Console")` | A CDC function with an interface name. |
| `console.configure()` / `midi.configure()` | Each function's buffer sizes. Before `device.begin()`. |
| `console.writeAvailable()` | Room on the sending side: only that much is read and echoed, so nothing is lost. |
| `midi.readPacket()` / `writePacket()` | One MIDI packet at a time; one that cannot be sent is held and sent on a later turn. |

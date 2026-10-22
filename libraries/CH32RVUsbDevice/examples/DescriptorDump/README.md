# DescriptorDump

日本語: [README.ja.md](README.ja.md)

Prints, on the UART, what the device shows the host - the device,
configuration, BOS, Microsoft OS 2.0, string and HID report descriptors - and
every control request and bus event. Useful when a host will not bind a
driver.

## What you need

A PC and a UART monitor at 115200 8N1.

## Steps and expected result

1. Open the UART monitor and plug the board into the PC.
2. The enumeration's requests scroll by (`ACK  80 06 0100 0000   64 -> ok 18`),
   and after `bus: configured` the descriptors are printed.
3. Send `d` to print them again.

## Notes

- The descriptors come from `deviceDescriptor()`, `configurationDescriptor()`
  and the rest. Nothing is kept resident: each call builds into one shared
  buffer, valid until the next such call.
- `onAnyControlRequest()` reports vendor requests at SETUP and every control
  transfer at ACK. The last number on an ACK line is how many bytes the
  device answered.
- The callbacks run from `deliverEvents()` at the top of `loop()`. Eight
  requests are kept until then; more are dropped
  (`USBDevice.droppedControlRequests()`).

## See also

- [Common setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and coverage](../../README.md)

[DescriptorDump.ino](DescriptorDump.ino)

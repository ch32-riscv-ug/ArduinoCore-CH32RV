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
  buffer. Consume it before the next descriptor-building call, including a
  different getter.
- `onAnyControlRequest()` reports vendor requests at SETUP and every control
  transfer at ACK. The last number on an ACK line is how many bytes the
  device answered.
- The callbacks run from `deliverEvents()` at the top of `loop()`. Pending
  notification storage is finite; inspect `USBDevice.droppedControlRequests()`
  for lost observation records.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `deviceDescriptor()` / `configurationDescriptor()` | Inspect identity and interfaces/endpoints. Check for `nullptr` before accessing. |
| `hidReportDescriptor()` | Reads HID data layout, separately from the HID descriptor in the configuration. |
| `onAnyControlRequest()` | Observes requests; it does not register their response provider. |
| `onBusEvent()` / `deliverEvents()` | Deliver bus notifications and request a dump after configuration. |
| `droppedControlRequests()` | Detect lost observation records. An absent log is not proof a request never arrived. |

Configuration, BOS, MS OS 2.0 and HID report getters share scratch storage. **Display or copy each result before another descriptor-building call, including a different getter.** Use string descriptors before the next `stringDescriptor()` call too.

Large UART dumps take time. Narrow the output when investigating timing, and account for logging overhead. String display is simplified; inspect raw data when validating non-ASCII names.

## See also

- [Common setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and coverage](../../README.md)

[DescriptorDump.ino](DescriptorDump.ino)

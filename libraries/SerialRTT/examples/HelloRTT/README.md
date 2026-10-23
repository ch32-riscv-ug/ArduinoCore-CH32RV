# HelloRTT

[日本語](README.ja.md)

Read a log through a RAM ring buffer and debug probe.

## Hardware and connections

Use an RTT-capable probe and debug connection; no UART wiring is needed.

## Steps and expected results

Start a monitor such as `ch32rv monitor --source rtt --chip CH32V203`, adjusting the chip, and inspect startup text and uptime.

## API and processing flow

After `SerialRTT.begin()`, use `print()`/`println()`. Baud does not configure UART; the host locates RTT information in RAM.

## Adapting the example

Understand full-buffer behavior rather than assuming unlimited storage without a host. Probe memory-access methods and their effects vary; see [debug output (Japanese)](../../../../docs/debug-output.ja.md).

## Related documentation

- [Sketch](HelloRTT.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)

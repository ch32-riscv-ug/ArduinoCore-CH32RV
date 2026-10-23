# PrintfToSDI

[日本語](README.ja.md)

Switch `printf()` output between UART and SDI.

## Hardware and connections

Provide an SDI debug connection. Connect UART too if inspecting the initial and restored UART output.

## Steps and expected results

At startup, output moves UART → SDI → UART; later ticks go to UART. See [HelloSDI](../HelloSDI/README.md) for the monitor.

## API and processing flow

`ch32rv_set_stdout(&SerialSDI)` redirects stdio. Pass `&Serial` to restore UART. Keep the selected output object alive during use.

## Adapting the example

This redirects stdio; `Serial.print()` still uses UART. Distinguish libraries using `Serial` directly from those using `printf()` when tracking log destinations.

## Related documentation

- [Sketch](PrintfToSDI.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)

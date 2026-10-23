# HelloDMSeq

[日本語](README.ja.md)

Use a bidirectional debug terminal to echo input in uppercase.

## Hardware and connections

Use a compatible probe and debug connection; UART pins are unused.

## Steps and expected results

Open `ch32rv monitor --source dmseq` and inspect echo and uptime. Add target selection if required by the environment.

## API and processing flow

After `SerialDMSeq.begin()`, use `available()`/`read()`/`write()`. `available()` also advances the exchange needed for host input, so call it even when not printing.

## Adapting the example

Use a DMSeq-capable host, without SDI or DMDATA on the shared registers. See the [library README](../../README.md) for send waiting and absent-host behavior.

## Related documentation

- [Sketch](HelloDMSeq.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)

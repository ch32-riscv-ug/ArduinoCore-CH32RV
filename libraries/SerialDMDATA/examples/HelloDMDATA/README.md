# HelloDMDATA

[日本語](README.ja.md)

Receive text through debug registers and echo lowercase ASCII as uppercase.

## Hardware and connections

Use a compatible probe and debug connection; no additional UART is needed.

## Steps and expected results

Open a matching terminal (`minichlink -T` in the sketch) and confirm uppercase echo and uptime output.

## API and processing flow

`available()` retrieves incoming register data; `read()`/`write()` forward bytes. Baud is a compatibility argument.

## Adapting the example

Poll input regularly because receive and transmit share registers. Do not mix SDI/DMSeq framing on those registers; select DMDATA on the host as well.

## Related documentation

- [Sketch](HelloDMDATA.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)

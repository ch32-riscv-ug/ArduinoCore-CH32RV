# CcidReader

日本語: [README.ja.md](README.ja.md)

A USB CCID reader example printing card ATR and a SELECT MF status word.

## Hardware and connections

A USB CCID reader and compatible card, powered host wiring and a UART monitor. The SmartCardReader device example can also act as a peer.

## Steps and expected results

1. Reader connection or card insertion triggers processing.
2. Successful card power-on prints `ATR: ...`.
3. The sketch sends `00 A4 00 00 02 3F 00` (SELECT MF) and prints its status word after a successful exchange.
4. Removing the card prints `card removed`.

## Settings and limits

Transport success is distinct from APDU success. The SmartCardReader device demo does not implement SELECT MF and returns `6D00`. ccidApdu separates response data and SW, but responseCapacity must also accommodate the two status bytes.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbHost API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[CcidReader.ino](CcidReader.ino)

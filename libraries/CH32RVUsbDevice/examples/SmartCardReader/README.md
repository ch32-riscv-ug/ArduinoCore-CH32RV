# SmartCardReader

日本語: [README.ja.md](README.ja.md)

A one-slot CCID reader whose card is emulated in software; no physical card interface is needed.

## Hardware and connections

A PC with PC/SC and an APDU tool, plus a 115200-baud UART monitor.

## Steps and expected results

1. List the reader and ATR through PC/SC. The card starts inserted.
2. `FF CA 00 00 00` returns `04 11 22 33 90 00`.
3. `80 01 00 00 03 61 62 63` returns `61 62 63 90 00`.
4. UART r removes the card and i inserts it; state and counters are printed about every two seconds.

## Settings and limits

Other instructions return `6D00`. The synthetic ATR does not implement the full advertised card technology. The device APDU callback returns payload plus SW1/SW2; distinguish this from the host ccidApdu API, which separates the status word.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[SmartCardReader.ino](SmartCardReader.ino)

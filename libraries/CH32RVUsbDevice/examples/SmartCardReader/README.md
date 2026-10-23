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

## API usage and adaptation

| API | Use in this example |
|---|---|
| `Ccid.insertCard(atr, length)` / `removeCard()` | Set the emulated slot's presence state; they do not detect a physical card. |
| `onApdu(apdu, length, response, capacity)` | Registers a provider that validates the command, writes response including SW1/SW2, and returns total bytes. |
| `onPower()` | Receives activation/deactivation notification through `deliverEvents()`. |
| `cardPresent()` / `cardPowered()` | Distinguish presence from host activation. |

For additional APDUs, validate header and Lc against available bytes and keep responses within `capacity`. Returning zero means command failure, not a successful empty response.

`onApdu()` prepares a USB response immediately and does not wait for `deliverEvents()`. Avoid long operations there. A physical reader additionally needs card circuitry and communication; an ATR alone does not implement a card's behavior.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[SmartCardReader.ino](SmartCardReader.ino)

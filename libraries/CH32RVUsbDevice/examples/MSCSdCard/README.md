# MSCSdCard

日本語: [README.ja.md](README.ja.md)

An SPI SD card exposed as USB mass storage; the PC owns the filesystem.

## Hardware and connections

A 3.3 V SD socket/module, card and device board. Use board SCK/MISO/MOSI pins and normally PA4 for CS; the code falls back to PC4 where PA4 is absent. Connect power and GND.

## Steps and expected results

1. Match all four SPI pin constants to the wiring.
2. Check UART for `USB SD MSC ready blocks=... block_size=512`.
3. Read/write existing files on the PC, then eject and observe `SD_EJECT`.

## Settings and limits

The sketch uses 4 MHz SPI. For `SD_BEGIN_FAILED`, check power, CS and SPI wiring first. Since the PC writes blocks directly, another board-side filesystem must not modify the same media while mounted by the PC. Use a backed-up test card.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MSCSdCard.ino](MSCSdCard.ino)

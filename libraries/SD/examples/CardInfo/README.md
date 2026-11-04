# CardInfo

日本語: [README.ja.md](README.ja.md)

What an SD card over SPI is, a file written and read back, the root listed, and 256 KB
written and read with the time each took.

## Hardware and connections

An SD card on the board's SPI (SCK, MISO, MOSI of the bus, chip select on `SS`, or PA4 where the variant has no SS; set `SD_CS_PIN` in build_opt.h for another), a card with a FAT volume, the serial monitor.

## Steps and expected results

Open the monitor at 115200 baud and reset: `card SDHC/SDXC, 30436 MB, FAT type 3, ...`, `read back: written at ...`,
the root's entries, and `256 KB: write ... ms, read ... ms (262144 bytes back)`.
`..._BEGIN_FAILED` means no card, no FAT volume on it, or the wiring.

## Settings and limits

The card keeps `/ch32rv.txt`; `/speed.bin` is removed after the timing.
Not run on a chip yet.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `begin()` | Identify the card and mount its FAT volume |
| `cardType()`, `cardSize()` | From the card |
| `fatType()`, `freeBytes()` | From the volume (FatFS) |
| `open()`, `openNextFile()`, `remove()` | Files, as in the FS library |

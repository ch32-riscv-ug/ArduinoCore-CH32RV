# CardInfo

日本語: [README.ja.md](README.ja.md)

What an SD card on the SDIO host is, a file written and read back, the root listed, and 256 KB
written and read with the time each took.

## Hardware and connections

A board with SDIO (CH32V30x, V4x7, H41x) and a card slot on the series' default pins (libraries/SD_MMC/README.md; the CH32V307V-R2 evaluation board has one), pull-ups on CMD and D0-D3, a card with a FAT volume, the serial monitor.

## Steps and expected results

Open the monitor at 115200 baud and reset: `SDIO_CK 18000 kHz, 4 bits`, then `card SDHC/SDXC, ...`, `read back: written at ...`,
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

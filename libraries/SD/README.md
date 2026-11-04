# SD — SD cards over SPI (ESP32's SD API)

[日本語](README.ja.md)

`SD` is a `FatFS` (the [FS library](../FS/README.md)) on an SD card in SPI
mode: `SD.open()`, `File` and the rest are the FS library's.

```cpp
#include <SD.h>

void setup() {
    SPI.begin();                          // the bus, as for any SPI device
    if (!SD.begin(PA4)) {                 // the card's chip select
        return;                           // no card, no FAT volume, or the wiring
    }
    File f = SD.open("/log.txt", FILE_APPEND);
    f.println(millis());
}
void loop() { }
```

## API

| | |
|---|---|
| `begin(ssPin = SS, spi = SPI, frequency = 4000000, mountpoint, max_files, format_if_empty = false)` | Identify the card and mount its volume. The bus is begun by the sketch; `frequency` is the clock after the 400 kHz identification (SD cards take up to 25 MHz in SPI mode; the wiring decides). `mountpoint` and `max_files` are ESP32's and are not used. `format_if_empty` formats a card with no FAT volume |
| `end()` | Unmount |
| `cardType()` | `CARD_SD` (SDSC), `CARD_SDHC` (SDHC / SDXC), `CARD_MMC`, `CARD_NONE` |
| `cardSize()`, `numSectors()`, `sectorSize()` | From the card's CSD |
| `readRAW(buffer, sector)`, `writeRAW(buffer, sector)` | One 512-byte block, bypassing the file system |
| `card()` | The `SDSPIBlockDevice` itself |

`SDSPIBlockDevice` can stand alone as an `fs::BlockDevice` (another `FatFS`,
or `CH32RVUsbDeviceMscBlockStorage` to show the card to a PC).

## The driver

- SDSC (v1 and v2), SDHC, SDXC and MMC. CRC on for every command and data
  block (CMD59); `-DSD_SPI_CRC=0` in `build_opt.h` turns it off.
- Several blocks at once with CMD18 / CMD25. Every write waits out the card's
  programming and then asks its status (CMD13), so a write the card took but
  could not program fails.
- Any `SPIClass`: a hardware SPI or a SoftSPI.
- Not checked: a card's write protection switch, or a card being taken out
  while mounted.

## Checked on

The host test (tests/unit/fs, `test_sd_spi_*`) runs the library against a
simulated card that checks every command's and block's CRC: SDHC, SDSC v2 and
v1 identified and formatted, files written and read with multiple-block
transfers, a broken CRC and a refused write reported, `fsck` clean afterwards;
no card on the bus fails `begin()`. On a chip it has been built only.

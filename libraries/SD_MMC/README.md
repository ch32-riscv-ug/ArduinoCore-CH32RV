# SD_MMC — SD cards on the SDIO host (ESP32's SD_MMC API)

[日本語](README.ja.md)

`SD_MMC` is a `FatFS` (the [FS library](../FS/README.md)) on an SD card on the
SDIO host: CH32V303 / V305 / V307 / V317 (the packages that have SDIO),
V407 / V467, H415 / H417. Elsewhere `begin()` is false.

```cpp
#include <SD_MMC.h>

void setup() {
    if (!SD_MMC.begin()) {                // the series' default pins, 4 data lines
        return;
    }
    File f = SD_MMC.open("/log.txt", FILE_APPEND);
    f.println(millis());
}
void loop() { }
```

## Pins

| Series | CK | CMD | D0 | D1 | D2 | D3 |
|---|---|---|---|---|---|---|
| V30x (fixed) | PC12 | PD2 | PC8 | PC9 | PC10 | PC11 |
| V4x7 | PC12 | PD2 | PC8 or PB14 | PC9 or PB15 | PC10 | PC11 |
| H417 | PB11 or PC1 | PB10 or PA0 | PE8 or PB14 | PE9 or PB15 | PE10 or PB3 | PE11 or PB4 |
| H415 | PB11 or PC1 | PB10 or PA0 | PB14 | PB15 | PB3 | PB4 or PE11 |

The first of each is the default (on H417 the EVT's). `setPins()` picks the
others before `begin()`; on V4x7 D0 and D1 move together. Put a pull-up
(10-47 kΩ) on CMD and D0-D3: the pads are push-pull outputs and inputs without
their own. The CH32V307V-R2 evaluation board has the slot on the V30x pins.

## API

| | |
|---|---|
| `setPins(clk, cmd, d0)` / `setPins(clk, cmd, d0, d1, d2, d3)` | Before `begin()`; three pins mean 1-bit. False when the series cannot put the SDIO there |
| `begin(mountpoint, mode1bit = false, format_if_mount_failed = false, sdmmc_frequency = 20000, maxOpenFiles)` | Identify the card and mount. `sdmmc_frequency` is a ceiling in kHz; the clock is HCLK / n, and at most 25 MHz (default speed; high speed is not switched to). `mountpoint` and `maxOpenFiles` are ESP32's and are not used |
| `end()`, `cardType()`, `cardSize()`, `numSectors()`, `sectorSize()`, `readRAW()`, `writeRAW()` | As in SD |
| `card()` | The `SDIOBlockDevice`: `clockHz()`, `wide()` |

## The driver

- Identification in 1-bit mode at the slowest clock the divider gives:
  400 kHz up to 103 MHz HCLK, HCLK / 257 above (560 kHz at 144 MHz, which the
  specification's 400 kHz is under; cards answer in practice, and the EVT does
  the same). Then 4 lines (ACMD6) and the transfer clock.
- The FIFO is moved by the CPU, with hardware flow control: the clock stops
  while the FIFO is full (reading) or empty (writing), so an interrupt in
  between costs time and not data. No DMA.
- SDSC, SDHC, SDXC; MMC in 1-bit.
- Not run on a chip: the timings, HCLK above 100 MHz, the H41x AF pins, V4x7's
  remap, hardware flow control on WCH's block (STM32F1's has an erratum).

## Checked on

Built for V303, V307, V317, V407, H415 and H417. Not run on a chip.

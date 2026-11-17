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

| Series | CK | CMD | D0 | D1 | D2 | D3 | D4-D7 (MMC's 8 bits) |
|---|---|---|---|---|---|---|---|
| V30x (fixed) | PC12 | PD2 | PC8 | PC9 | PC10 | PC11 | PB8, PB9, PC6, PC7 |
| V4x7 | PC12 | PD2 | PC8 or PB14 | PC9 or PB15 | PC10 | PC11 | PB8, PB9, PC6 or PA9, PC7 or PA10 |
| H417 | PB11 or PC1 | PB10 or PA0 | PE8 or PB14 | PE9 or PB15 | PE10 or PB3 | PE11 or PB4 | PE12-PE15 or PB8, PB9, PC6, PC7 |
| H415 | PB11 or PC1 | PB10 or PA0 | PB14 | PB15 | PB3 | PE11 or PB4 | PE12-PE15 or PB8, PB9, PC6, PC7 |

The table is the variant's `CH32RV_SDIO_PADS` (generated from ch32-device-data); the first of each is the
default (on H417 the EVT's). `setPins()` picks the others before `begin()`. H41x selects the function pad by
pad, so any mix works; on V4x7 one bit (PCFR2 SDIO_RM) moves D0, D1, D6 and D7 together, so only one side's set.
H416 has no D0 pad in device-data and `begin()` is false there. Put a pull-up (10-47 kΩ) on CMD and D0-D7: the
pads are push-pull outputs (on H41x at their fastest edge) and inputs without their own. The CH32V307V-R2
evaluation board has the slot on the V30x pins.

On V30x and V4x7, D0 and D1 move to PB14 / PB15 by themselves while the Ethernet MAC is clocked (RCC ETHMACEN;
on V30x only some lots, the datasheets' notes). This library never clocks the MAC to take that path. With ETH
and SD_MMC together on V4x7, choose the PB14 / PB15 set with `setPins()`.

## API

ESP32's SD_MMC names and shapes.

| | |
|---|---|
| `setPins(clk, cmd, d0)` / `setPins(clk, cmd, d0, d1, d2, d3)` | Before `begin()`; three pins mean 1-bit. False when the series cannot put the SDIO there |
| `setPins(clk, cmd, d0, ..., d7)` | 8 bits (MMC / eMMC). Not in ESP32's; this core's addition |
| `begin(mountpoint, mode1bit = false, format_if_mount_failed = false, sdmmc_frequency = BOARD_MAX_SDMMC_FREQ, maxOpenFiles)` | Identify the card and mount. `sdmmc_frequency` is a ceiling in kHz, by default ESP32's `SDMMC_FREQ_HIGHSPEED` (40000). Above default speed (SD 25 MHz, MMC 26) the card is switched to high speed (SD: CMD6, 50 MHz; MMC: HS_TIMING, 52 / 26 MHz). The clock is HCLK / n under the lower of the two ceilings. `mountpoint` and `maxOpenFiles` are ESP32's and are not used |
| `end()`, `cardType()`, `cardSize()`, `numSectors()`, `sectorSize()`, `readRAW()`, `writeRAW()` | As in SD |
| `card()` | The `SDIOBlockDevice`: `clockHz()`, `busWidth()` (1 / 4 / 8), `wide()`, `highSpeed()`, `dma()`, `dmaBlocks()` / `cpuBlocks()` (blocks since `begin()`). Before `begin()`: `setDMA(false)` (all by the CPU), `setFlowControl(false)` (hardware flow control off) |

Differences from ESP32: ESP32 has 1 and 4 bits only, no 8-bit `setPins()`. ESP-IDF tries high speed above a
20 MHz ceiling and runs SD high speed at 40 MHz (its divider); here it is tried above 25 MHz and runs up to the
card's 50 MHz. Both are within the specification; the difference is the dividers'.

## The driver

- Identification in 1-bit mode at the slowest clock the divider gives: 400 kHz up to 103 MHz HCLK, HCLK / 257
  above (560 kHz at 144 MHz, which the specification's 400 kHz is under; cards answer in practice, and the EVT
  does the same).
- SD: 4 lines by ACMD6; above a 25 MHz ceiling, high speed by CMD6 SWITCH_FUNC (mode 0 asks whether function 1
  is there, mode 1 switches, and the status tells the function now selected).
- MMC: CMD1 (sector addressing asked). With CSD SPEC_VERS 4 or later, EXT_CSD (CMD8): a sector-addressed card's
  size from SEC_COUNT; 4 or 8 lines by BUS_WIDTH (CMD6), checked by reading EXT_CSD again on the new width (back
  to 1 line if that fails); high speed by HS_TIMING where CARD_TYPE allows.
- Blocks move by DMA (V30x / V4x7: DMA2 channel 4; H41x: the SDIO request on DMA2 channel 4 through DMAMUX)
  between the FIFO and memory a word at a time. `begin()` claims the channel with `ch32rv_dma_claim()`; when
  another user holds it, the CPU moves the FIFO. So it does for a buffer off a 4-byte boundary.
- Hardware flow control is on by default: the clock stops while the FIFO is full (reading) or empty (writing),
  so an interrupt or a DMA wait in between costs time and not data (WCH's RM asks for HWFC_EN with BYPASS).
- SDSC, SDHC, SDXC, MMC / eMMC.

Size (the CardInfo example, clang, 2026-11-17): DMA, high speed and the MMC widths took text on V307 from 35664
to 36928 bytes (+1264), on V407 35456 → 36776 (+1320), on H417 37072 → 38432 (+1360). A sketch without SD_MMC
is unchanged.

## Checked on

Built for V303, V307, V317, V407, H415 and H417. 4 bits at 20 MHz were read and written on H417 (nanoCH32H417's
SD slot), with the earlier polled driver. DMA, high speed, no flow control and 1 bit against 4 are compared by
tests/instrumented/sd (test_sd_sdio_transfer, test_sd_sdio_high_speed), MMC by a single, manual test
(tests/TEST_PLAN.md); not run yet.

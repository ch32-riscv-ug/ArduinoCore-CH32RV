/* SD cards on the SDIO host, with ESP32's SD_MMC API: a FatFS (the FS
 * library) on an SDIOBlockDevice. CH32V303 / V305 / V307 / V317 (the pin
 * counts that have SDIO), V407 / V467, H415 / H417; elsewhere begin() is
 * false.
 *
 *   #include <SD_MMC.h>
 *   if (SD_MMC.begin()) {                        // the series' default pins, 4 bits
 *       File f = SD_MMC.open("/log.txt", FILE_APPEND);
 *       f.println(millis());
 *   }
 */
#pragma once

#include <FatFS.h>
#include <SDCardInfo.h>

#include "SDIOBlockDevice.h"

/* ESP32's frequencies, in kHz. */
#define SDMMC_FREQ_DEFAULT 20000
#define SDMMC_FREQ_HIGHSPEED 40000
#define SDMMC_FREQ_PROBING 400
#define SDMMC_FREQ_52M 52000
#define SDMMC_FREQ_26M 26000
#ifndef BOARD_MAX_SDMMC_FREQ
#define BOARD_MAX_SDMMC_FREQ SDMMC_FREQ_DEFAULT
#endif

namespace fs {

class SDMMCFS : public FatFS {
public:
    /* The pads, before begin() (sdio_hw.h lists each series' choices); false
     * when the series cannot put the SDIO there. Three pins: 1-bit. */
    bool setPins(int clk, int cmd, int d0) { return _card.setPins(clk, cmd, d0); }
    bool setPins(int clk, int cmd, int d0, int d1, int d2, int d3) { return _card.setPins(clk, cmd, d0, d1, d2, d3); }

    /* ESP32's arguments. `mountpoint` and `maxOpenFiles` are taken and not
     * used (paths are the volume's own; FS_MAX_OPEN_FILES, FSConfig.h).
     * `sdmmc_frequency` is a ceiling in kHz: the clock is HCLK / n, and
     * above 25 MHz (default speed) it stays at 25 (high speed is not
     * switched to). format_if_mount_failed formats a card with no FAT volume
     * (where format() is built in). */
    bool begin(const char *mountpoint = "/sdcard", bool mode1bit = false, bool format_if_mount_failed = false,
               int sdmmc_frequency = BOARD_MAX_SDMMC_FREQ, uint8_t maxOpenFiles = 5)
    {
        /* inline, so that begin() links no format() */
        (void)mountpoint;
        (void)maxOpenFiles;
        end();
        _card.configure(mode1bit, sdmmc_frequency > 0 ? (uint32_t)sdmmc_frequency : SDMMC_FREQ_DEFAULT);
        return FatFS::begin(_card, format_if_mount_failed);
    }
    void end();
    sdcard_type_t cardType() const { return _card.cardType(); }
    uint64_t cardSize() const { return (uint64_t)numSectors() * 512u; }
    int numSectors() const { return (int)_card.blocks(); }
    int sectorSize() const { return 512; }
    bool readRAW(uint8_t *buffer, uint32_t sector) { return _card.readBlocks(sector, buffer, 1); }
    bool writeRAW(uint8_t *buffer, uint32_t sector) { return _card.writeBlocks(sector, buffer, 1); }
    SDIOBlockDevice &card() { return _card; }

private:
    SDIOBlockDevice _card;
};

} // namespace fs

using fs::SDMMCFS;
extern fs::SDMMCFS SD_MMC;

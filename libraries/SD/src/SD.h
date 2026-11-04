/* SD cards over SPI, with ESP32's SD API: a FatFS (the FS library) on an
 * SDSPIBlockDevice.
 *
 *   #include <SD.h>
 *   SPI.begin();                                 // the bus, as for any SPI device
 *   if (SD.begin(PA4)) {                         // chip select
 *       File f = SD.open("/log.txt", FILE_APPEND);
 *       f.println(millis());
 *   }
 */
#pragma once

#include <FatFS.h>
#include <SPI.h>

#include <SDCardInfo.h>
#include "SDSPIBlockDevice.h"

namespace fs {

class SDFS : public FatFS {
public:
    /* ESP32's arguments. The SPI bus is begun by the sketch (SPI.begin(), or
     * SPI.begin(sck, miso, mosi)); `frequency` is the transfer clock (SD
     * cards take up to 25 MHz in SPI mode, the bus and wiring set the real
     * limit). `mountpoint` and `max_files` are taken and not used: paths are
     * the volume's own ("/log.txt"), and the open files are FS_MAX_OPEN_FILES
     * (FSConfig.h). With format_if_empty a card with no FAT volume is
     * formatted (where format() is built in). */
    bool begin(uint8_t ssPin
#ifdef SS
               = SS
#endif
               , SPIClass &spi = SPI, uint32_t frequency = 4000000, const char *mountpoint = "/sd",
               uint8_t max_files = 5, bool format_if_empty = false)
    {
        /* inline, so that begin(cs) links no format() */
        (void)mountpoint;
        (void)max_files;
        end();
        _card.configure(ssPin, spi, frequency);
        return FatFS::begin(_card, format_if_empty);
    }
    void end();
    sdcard_type_t cardType() const { return _card.cardType(); }
    uint64_t cardSize() const { return (uint64_t)numSectors() * 512u; }
    size_t numSectors() const { return _card.blocks(); }
    size_t sectorSize() const { return 512; }
    /* Whole blocks of the card, bypassing the file system. */
    bool readRAW(uint8_t *buffer, uint32_t sector) { return _card.readBlocks(sector, buffer, 1); }
    bool writeRAW(uint8_t *buffer, uint32_t sector) { return _card.writeBlocks(sector, buffer, 1); }
    SDSPIBlockDevice &card() { return _card; }

private:
    SDSPIBlockDevice _card;
};

} // namespace fs

using fs::SDFS;
extern fs::SDFS SD;

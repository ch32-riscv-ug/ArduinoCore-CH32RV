/* An SD card (SDSC, SDHC, SDXC) or MMC in SPI mode as an fs::BlockDevice
 * (SD Physical Layer Simplified Specification 9.00, chapter 7). The bus is
 * any SPIClass - a hardware SPI or a SoftSPI - begun by the caller. */
#pragma once

#include <FSBlockDevice.h>
#include <SPI.h>

#include <SDCardInfo.h>

/* Check the CRC of commands and data (CMD59): a long or noisy line fails a
 * transfer instead of returning wrong data. */
#ifndef SD_SPI_CRC
#define SD_SPI_CRC 1
#endif

class SDSPIBlockDevice : public fs::BlockDevice {
public:
    /* `frequency` is the transfer clock after the 400 kHz identification. */
    void configure(int ssPin, SPIClass &spi, uint32_t frequency)
    {
        _cs = ssPin;
        _spi = &spi;
        _frequency = frequency;
    }
    /* Identify the card; false when none answers or it is not usable. */
    bool begin() override;
    void end() { _type = CARD_NONE; _blocks = 0; }
    bool readBlocks(uint32_t lba, uint8_t *buffer, uint32_t count) override;
    bool writeBlocks(uint32_t lba, const uint8_t *buffer, uint32_t count) override;
    bool sync() override;
    uint32_t blockCount() override { return _blocks; }
    bool writable() override { return _type != CARD_NONE; }
    /* The erase unit is not read from the card; 4 MB (8192 blocks) is the
     * allocation unit of the cards SDHC formats for, and aligns any smaller one. */
    uint32_t eraseBlocks() override { return 8192; }
    sdcard_type_t cardType() const { return _type; }
    uint32_t blocks() const { return _blocks; }

private:
    uint8_t command(uint8_t cmd, uint32_t arg);
    uint8_t appCommand(uint8_t cmd, uint32_t arg);
    bool waitReady(uint32_t timeoutMs);
    bool waitToken(uint8_t *token, uint32_t timeoutMs);
    bool readData(uint8_t *buffer, uint16_t length);
    bool writeData(uint8_t token, const uint8_t *buffer);
    bool stopTransmission();
    void select(bool fast);
    void deselect();
    uint8_t xfer(uint8_t b) { return _spi->transfer(b); }

    SPIClass *_spi = nullptr;
    int _cs = -1;
    uint32_t _frequency = 4000000;
    uint32_t _blocks = 0;
    sdcard_type_t _type = CARD_NONE;
    bool _byteAddress = true;    /* SDSC / MMC: byte addresses; SDHC / SDXC: block numbers */
};

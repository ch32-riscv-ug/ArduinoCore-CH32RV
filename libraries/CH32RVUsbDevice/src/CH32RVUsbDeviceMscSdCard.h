/* An SD card over SPI as the medium of CH32RVUsbDeviceMsc
 * (EspUsbDeviceMscSdCard). The host sees the card's raw blocks and mounts
 * whatever file system is on it; the sketch needs no file system library.
 *
 *     #include <SPI.h>
 *     #include <CH32RVUsbDeviceMscSdCard.h>
 *     CH32RVUsbDeviceMsc msc;
 *     CH32RVUsbDeviceMscSdCard card;
 *     SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN);
 *     card.begin(CS_PIN, SPI);  card.attach(msc);  msc.mediaPresent(true);
 *
 * Differences from EspUsbDevice: the SPI bus is passed explicitly (this header
 * does not pull in the SPI library), there is no mount point - nothing is
 * mounted on the device side - and SD/SDHC/SDXC cards in SPI mode are driven
 * directly. Not included by CH32RVUsbDevice.h. */
#pragma once

#include <CH32RVUsbDevice.h>
#include <api/HardwareSPI.h>

class CH32RVUsbDeviceMscSdCard : public CH32RVUsbDeviceMscStorage {
public:
    typedef void (*EjectCallback)(void);

    CH32RVUsbDeviceMscSdCard() {}

    /* Initialise the card on `spi` with chip select `ssPin` (the bus is begun
     * by the sketch). `frequency` is the transfer clock after the 400 kHz
     * identification phase. False when no card answers. */
    bool begin(int ssPin, arduino::HardwareSPI &spi, uint32_t frequency = 4000000);
    bool attach(CH32RVUsbDeviceMsc &msc);
    void onEject(EjectCallback cb) { _onEject = cb; }
    void readOnly(bool value) { _readOnly = value; }
    bool readOnly() const { return _readOnly; }
    uint32_t blockCount() const { return _blockCount; }
    uint16_t blockSize() const { return 512; }
    bool mounted() const { return _mounted; }

    /* Single blocks, for a sketch that wants the card itself. */
    bool readBlock(uint32_t lba, uint8_t *buffer);
    bool writeBlock(uint32_t lba, const uint8_t *buffer);

    int32_t read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) override;
    int32_t write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) override;
    bool startStop(uint8_t powerCondition, bool start, bool loadEject) override;

private:
    uint8_t command(uint8_t cmd, uint32_t arg);
    bool waitReady(uint32_t timeoutMs);
    void select();
    void deselect();
    uint8_t xfer(uint8_t b) { return _spi->transfer(b); }

    arduino::HardwareSPI *_spi = nullptr;
    int _cs = -1;
    uint32_t _frequency = 4000000;
    uint32_t _blockCount = 0;
    bool _blockAddressing = false;   /* SDHC/SDXC: LBA, not byte address */
    bool _mounted = false;
    bool _readOnly = false;
    EjectCallback _onEject = nullptr;
};

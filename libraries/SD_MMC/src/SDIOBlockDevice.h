/* An SD card (SDSC, SDHC, SDXC) or MMC on the SDIO host (CH32V30x, V4x7,
 * H41x) as an fs::BlockDevice, 1 or 4 data lines, the FIFO moved by the CPU
 * with hardware flow control (the clock stops while the FIFO is full or
 * empty, so an interrupt in between costs time, not data). Elsewhere it
 * exists and begin() is false. */
#pragma once

#include <FSBlockDevice.h>
#include <SDCardInfo.h>

class SDIOBlockDevice : public fs::BlockDevice {
public:
    /* The pads (sdio_hw.h lists each series' choices); false and nothing
     * changed when the series cannot route the SDIO there. d1-d3 -1: 1-bit. */
    bool setPins(int clk, int cmd, int d0, int d1 = -1, int d2 = -1, int d3 = -1);
    /* oneBit: D0 only. maxKHz: the transfer clock's ceiling (SD's default
     * speed is 25 MHz; the clock is HCLK / n, at most HCLK / 2). */
    void configure(bool oneBit, uint32_t maxKHz)
    {
        _oneBit = oneBit;
        _maxKHz = maxKHz;
    }
    bool begin() override;
    void end();
    bool readBlocks(uint32_t lba, uint8_t *buffer, uint32_t count) override;
    bool writeBlocks(uint32_t lba, const uint8_t *buffer, uint32_t count) override;
    uint32_t blockCount() override { return _blocks; }
    bool writable() override { return _type != CARD_NONE; }
    uint32_t eraseBlocks() override { return 8192; }
    sdcard_type_t cardType() const { return _type; }
    uint32_t blocks() const { return _blocks; }
    /* The SDIO_CK the card runs at, Hz (0 before begin()). */
    uint32_t clockHz() const { return _clockHz; }
    /* Four data lines in use. */
    bool wide() const { return _wide; }

private:
    bool command(uint8_t index, uint32_t arg, uint8_t response, bool checkCrc = true);
    bool commandR1(uint8_t index, uint32_t arg);
    bool waitTransferState(uint32_t timeoutMs);
    bool readData(uint8_t *buffer, uint32_t bytes);
    bool writeData(const uint8_t *buffer, uint32_t bytes);
    void setupPins();
    void startData(uint32_t bytes, bool read);

    int8_t _pin[6] = {-1, -1, -1, -1, -1, -1};   /* CK, CMD, D0-D3; -1: the series' default */
    bool _oneBit = false;
    uint32_t _maxKHz = 20000;
    uint32_t _blocks = 0;
    uint32_t _rca = 0;
    uint32_t _clockHz = 0;
    sdcard_type_t _type = CARD_NONE;
    bool _byteAddress = true;
    bool _wide = false;
};

/* An SD card (SDSC, SDHC, SDXC) or MMC / eMMC on the SDIO host (CH32V30x,
 * V4x7, H41x: internal/ch32rv_sdio.h) as an fs::BlockDevice, on 1, 4 or 8
 * data lines (8: MMC only). Blocks move by DMA where the variant names the
 * SDIO's channel and nobody else holds it (ch32rv_dma_claim()), and the
 * buffer is word-aligned; otherwise the CPU moves the FIFO. Hardware flow
 * control is on by default: the clock stops while the FIFO is full (read) or
 * empty (write), so an interrupt in between costs time, not data. SD cards
 * switch to high speed (CMD6, 50 MHz) and MMCs to HS_TIMING and their bus
 * width (EXT_CSD) when the ceiling asks for more than default speed.
 * Elsewhere it exists and begin() is false. */
#pragma once

#include <FSBlockDevice.h>
#include <SDCardInfo.h>

class SDIOBlockDevice : public fs::BlockDevice {
public:
    /* The pads (the variant's CH32RV_SDIO_PADS); false and nothing changed
     * when the SDIO cannot be there. d1-d3 -1: 1 bit; d4-d7 too: 4 bits;
     * all eight: 8 bits (MMC; an SD card runs 4 of them). */
    bool setPins(int clk, int cmd, int d0, int d1 = -1, int d2 = -1, int d3 = -1, int d4 = -1, int d5 = -1,
                 int d6 = -1, int d7 = -1);
    /* oneBit: D0 only. maxKHz: the transfer clock's ceiling. Above 25 MHz
     * (SD's default speed; MMC's 26 / 20) the card is asked for high speed
     * (SD 50 MHz, MMC 52 / 26 MHz). The clock is HCLK / n, at most HCLK / 2. */
    void configure(bool oneBit, uint32_t maxKHz)
    {
        _oneBit = oneBit;
        _maxKHz = maxKHz;
    }
    /* Before begin(): false moves every block by the CPU (default true). */
    void setDMA(bool on) { _wantDma = on; }
    /* Before begin(): the SDIO's hardware flow control (CLKCR HWFC_EN,
     * default true). Off, a FIFO the CPU or DMA does not keep up with is an
     * overrun / underrun and the transfer fails. */
    void setFlowControl(bool on) { _flowControl = on; }

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
    /* Data lines in use: 1, 4 or 8. */
    uint8_t busWidth() const { return _width; }
    /* Four data lines or more in use. */
    bool wide() const { return _width >= 4; }
    /* The card took high speed (SD: CMD6 function 1 of group 1 answered as
     * switched; MMC: HS_TIMING written without SWITCH_ERROR). */
    bool highSpeed() const { return _highSpeed; }
    /* Aligned blocks move by DMA (the channel was free at begin()). */
    bool dma() const { return _dma; }
    /* Blocks moved by DMA and by the CPU since begin(). */
    uint32_t dmaBlocks() const { return _dmaBlocks; }
    uint32_t cpuBlocks() const { return _cpuBlocks; }

private:
    bool command(uint8_t index, uint32_t arg, uint8_t response, bool checkCrc = true);
    bool commandR1(uint8_t index, uint32_t arg);
    bool waitTransferState(uint32_t timeoutMs);
    bool readData(uint8_t *buffer, uint32_t bytes);
    bool writeData(const uint8_t *buffer, uint32_t bytes);
    bool dmaWait(bool read, uint32_t timeoutMs);
    void dmaStart(const void *buffer, uint32_t bytes, bool read);
    void dmaStop();
    void startData(uint32_t bytes, bool read, uint8_t blockLog2, bool dma);
    bool readSmall(uint8_t index, uint32_t arg, uint32_t *buffer, uint32_t bytes, uint8_t blockLog2);
    bool sdHighSpeed();
    bool mmcSetup(uint8_t width, uint32_t &maxHz);
    bool mmcSwitch(uint8_t index, uint8_t value);
    void setClock(uint32_t hz);

    int16_t _pin[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   /* CK, CMD, D0-D7; -1: the default */
    uint8_t _chosen[10] = {};
    uint8_t _pinWidth = 4;     /* data pads setPins() named: 1, 4, 8 */
    uint8_t _used = 0;         /* pads given to the SDIO */
    bool _oneBit = false;
    bool _wantDma = true;
    bool _flowControl = true;
    uint32_t _maxKHz = 20000;
    uint32_t _blocks = 0;
    uint32_t _rca = 0;
    uint32_t _clockHz = 0;
    uint32_t _clkcr = 0;
    uint32_t _dmaBlocks = 0;
    uint32_t _cpuBlocks = 0;
    uint32_t _statusErrors = 0;   /* R1 error bits CMD13 reported in waitTransferState() */
    sdcard_type_t _type = CARD_NONE;
    bool _byteAddress = true;
    uint8_t _width = 1;
    bool _highSpeed = false;
    bool _dma = false;
};

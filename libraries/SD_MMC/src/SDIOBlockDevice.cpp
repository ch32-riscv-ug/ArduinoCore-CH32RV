/* SD / MMC on the SDIO host (SD Physical Layer Simplified Specification 9.00,
 * chapter 4; JEDEC JESD84-B51 for MMC): identification in 1-bit mode at the
 * slowest clock, then the bus width, the timing (default or high speed) and
 * the transfer clock. Blocks by DMA (aligned buffers, the channel claimed) or
 * through the FIFO by the CPU. The series' registers, pads and DMA line are
 * the core's (internal/ch32rv_sdio.h, from the variant). */
#include "SDIOBlockDevice.h"

#include <Arduino.h>
#include <internal/ch32rv_sdio.h>

#include <string.h>

#if !CH32RV_HAS_SDIO

bool SDIOBlockDevice::setPins(int, int, int, int, int, int, int, int, int, int) { return false; }
bool SDIOBlockDevice::begin() { return false; }
void SDIOBlockDevice::end() {}
bool SDIOBlockDevice::readBlocks(uint32_t, uint8_t *, uint32_t) { return false; }
bool SDIOBlockDevice::writeBlocks(uint32_t, const uint8_t *, uint32_t) { return false; }

#else

#define SDSTA CH32RV_SDIO_STA

enum : uint8_t { RESP_NONE, RESP_SHORT, RESP_LONG };

/* R1's error bits (card status 31-19, 16, 13, 7, 3: out of range, address,
 * block length, erase, write protect, lock, CRC, illegal command, ECC,
 * controller, general, CSD overwrite, erase skip, AKE; bit 7 is MMC's
 * SWITCH_ERROR and SD's reserved). */
static const uint32_t R1_ERRORS = 0xFDFFE088u;

static const ch32rv_sdio_pad_t pads[] = CH32RV_SDIO_PADS;
static const uint8_t PAD_COUNT = (uint8_t)(sizeof pads / sizeof pads[0]);

#ifdef CH32RV_SDIO_DMA
static const ch32rv_dma_line_t sdio_dma = CH32RV_SDIO_DMA;
#endif

/* ---------------------------------------------------------------- pins */

bool SDIOBlockDevice::setPins(int clk, int cmd, int d0, int d1, int d2, int d3, int d4, int d5, int d6, int d7)
{
    const int want[10] = {clk, cmd, d0, d1, d2, d3, d4, d5, d6, d7};
    uint8_t width = 1;
    if (d1 >= 0 || d2 >= 0 || d3 >= 0) {
        width = 4;
    }
    if (d4 >= 0 || d5 >= 0 || d6 >= 0 || d7 >= 0) {
        width = 8;
    }
    int16_t pin[10];
    for (uint8_t i = 0; i < 10; i++) {
        pin[i] = (int16_t)(i < 2 + width ? want[i] : -1);
        if (i < 2 + width && want[i] < 0) {
            return false;   /* a gap: d1 without d2, d4 without d7 */
        }
    }
    uint8_t chosen[10];
    if (!ch32rv_sdio_pick(pads, PAD_COUNT, pin, (uint8_t)(2 + width), chosen)) {
        return false;
    }
    memcpy(_pin, pin, sizeof _pin);
    _pinWidth = width;
    if (width == 1) {
        _oneBit = true;
    }
    return true;
}

/* ---------------------------------------------------------------- commands */

bool SDIOBlockDevice::command(uint8_t index, uint32_t arg, uint8_t response, bool checkCrc)
{
    CH32RV_SDIO_ICR = CH32RV_SDIO_ICR_ALL & ~(CH32RV_SDIO_STA_DATA_ERRORS | CH32RV_SDIO_STA_DATAEND |
                                             CH32RV_SDIO_STA_DBCKEND);
    CH32RV_SDIO_ARG = arg;
    CH32RV_SDIO_CMD = index |
                      (response == RESP_SHORT  ? CH32RV_SDIO_CMD_RESP_SHORT
                       : response == RESP_LONG ? CH32RV_SDIO_CMD_RESP_LONG
                                               : 0) |
                      CH32RV_SDIO_CMD_CPSMEN;
    const uint32_t t0 = millis();
    for (;;) {
        const uint32_t sta = SDSTA;
        if (response == RESP_NONE) {
            if (sta & CH32RV_SDIO_STA_CMDSENT) {
                return true;
            }
        } else {
            if (sta & CH32RV_SDIO_STA_CTIMEOUT) {
                return false;
            }
            if (sta & CH32RV_SDIO_STA_CCRCFAIL) {
                return !checkCrc;   /* R3 (OCR) carries no valid CRC */
            }
            if (sta & CH32RV_SDIO_STA_CMDREND) {
                /* R2 and R3 do not echo the index. */
                return response == RESP_LONG || !checkCrc || (CH32RV_SDIO_RESPCMD & 0x3Fu) == index;
            }
        }
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;
        }
    }
}

bool SDIOBlockDevice::commandR1(uint8_t index, uint32_t arg)
{
    return command(index, arg, RESP_SHORT) && !(CH32RV_SDIO_RESP1 & R1_ERRORS);
}

/* CMD13 until the card is back in the transfer state and ready for data (a
 * busy card - programming, or switching after CMD6 - reports prg). The
 * error bits it reports on the way (cleared as they are read) are kept in
 * _statusErrors. */
bool SDIOBlockDevice::waitTransferState(uint32_t timeoutMs)
{
    const uint32_t t0 = millis();
    for (;;) {
        if (command(13, _rca << 16, RESP_SHORT)) {
            const uint32_t status = CH32RV_SDIO_RESP1;
            _statusErrors |= status & R1_ERRORS;
            if (((status >> 9) & 0xFu) == 4u && (status & (1u << 8))) {
                return true;
            }
        }
        if ((uint32_t)(millis() - t0) > timeoutMs) {
            return false;
        }
        yield();
    }
}

/* ---------------------------------------------------------------- data */

void SDIOBlockDevice::startData(uint32_t bytes, bool read, uint8_t blockLog2, bool dma)
{
    CH32RV_SDIO_DCTRL = 0;
    CH32RV_SDIO_ICR = CH32RV_SDIO_ICR_ALL;
    /* 250 ms for a read, 500 ms for a write (SDXC's busy limit), in card
     * clocks; per block. */
    CH32RV_SDIO_DTIMER = (_clockHz / 1000u) * (read ? 250u : 500u);
    CH32RV_SDIO_DLEN = bytes;
    CH32RV_SDIO_DCTRL = CH32RV_SDIO_DCTRL_BLOCK(blockLog2) | (read ? CH32RV_SDIO_DCTRL_DTDIR_READ : 0) |
                        (dma ? CH32RV_SDIO_DCTRL_DMAEN : 0) | CH32RV_SDIO_DCTRL_DTEN;
}

bool SDIOBlockDevice::readData(uint8_t *buffer, uint32_t bytes)
{
    uint32_t words = bytes / 4u;
    const uint32_t t0 = millis();
    const uint32_t limit = 1000u + bytes / 512u;
    for (;;) {
        const uint32_t sta = SDSTA;
        if (sta & CH32RV_SDIO_STA_DATA_ERRORS) {
            return false;
        }
        if ((sta & CH32RV_SDIO_STA_RXFIFOHF) && words >= 8u) {
            for (uint8_t i = 0; i < 8; i++) {
                const uint32_t w = CH32RV_SDIO_FIFO;
                memcpy(buffer, &w, 4);
                buffer += 4;
            }
            words -= 8u;
        } else if ((sta & CH32RV_SDIO_STA_RXDAVL) && words) {
            const uint32_t w = CH32RV_SDIO_FIFO;
            memcpy(buffer, &w, 4);
            buffer += 4;
            words--;
        } else if (!words && (sta & CH32RV_SDIO_STA_DATAEND)) {
            return true;
        }
        if ((uint32_t)(millis() - t0) > limit) {
            return false;
        }
    }
}

bool SDIOBlockDevice::writeData(const uint8_t *buffer, uint32_t bytes)
{
    uint32_t words = bytes / 4u;
    const uint32_t t0 = millis();
    const uint32_t limit = 2000u + bytes / 512u;
    for (;;) {
        const uint32_t sta = SDSTA;
        if (sta & CH32RV_SDIO_STA_DATA_ERRORS) {
            return false;
        }
        if ((sta & CH32RV_SDIO_STA_TXFIFOHE) && words) {
            /* half empty: room for 8 words */
            for (uint8_t i = 0; i < 8 && words; i++, words--) {
                uint32_t w;
                memcpy(&w, buffer, 4);
                CH32RV_SDIO_FIFO = w;
                buffer += 4;
            }
        } else if (!words && (sta & CH32RV_SDIO_STA_DATAEND)) {
            return true;
        }
        if ((uint32_t)(millis() - t0) > limit) {
            return false;
        }
    }
}

#ifdef CH32RV_SDIO_DMA
/* The channel between the FIFO and memory, a word at a time (the FIFO is
 * 32 bits wide and the buffer word-aligned), the SDIO's requests pacing it. */
void SDIOBlockDevice::dmaStart(const void *buffer, uint32_t bytes, bool read)
{
    const ch32rv_dma_line_t *l = &sdio_dma;
    ch32rv_dma_open(l);
    CH32RV_DMA_PADDR(l) = CH32RV_SDIO_FIFO_ADDR;
    CH32RV_DMA_MADDR(l) = (uint32_t)(uintptr_t)buffer;
    CH32RV_DMA_CNTR(l) = bytes / 4u;
    __asm__ volatile("fence rw, rw" ::: "memory");   /* the caller's bytes before the DMA reads them */
    CH32RV_DMA_CFGR(l) = CH32RV_DMA_CFGR_MINC | CH32RV_DMA_CFGR_PSIZE_32 | CH32RV_DMA_CFGR_MSIZE_32 |
                         CH32RV_DMA_CFGR_PL(3) | (read ? 0u : CH32RV_DMA_CFGR_DIR) | CH32RV_DMA_CFGR_EN;
}

/* Until the SDIO has sent or received the last block (DATAEND) and, reading,
 * the DMA has taken the last word out of the FIFO. */
bool SDIOBlockDevice::dmaWait(bool read, uint32_t timeoutMs)
{
    const ch32rv_dma_line_t *l = &sdio_dma;
    const uint32_t t0 = millis();
    for (;;) {
        const uint32_t sta = SDSTA;
        if (sta & CH32RV_SDIO_STA_DATA_ERRORS) {
            return false;
        }
        if ((CH32RV_DMA_INTFR(l) >> l->shift) & CH32RV_DMA_TEIF) {
            return false;
        }
        if ((sta & CH32RV_SDIO_STA_DATAEND) && (!read || CH32RV_DMA_CNTR(l) == 0u)) {
            __asm__ volatile("fence rw, rw" ::: "memory");   /* the DMA's bytes before the caller reads them */
            return true;
        }
        if ((uint32_t)(millis() - t0) > timeoutMs) {
            return false;
        }
    }
}

void SDIOBlockDevice::dmaStop()
{
    ch32rv_dma_close(&sdio_dma);
}
#else
void SDIOBlockDevice::dmaStart(const void *, uint32_t, bool) {}
bool SDIOBlockDevice::dmaWait(bool, uint32_t) { return false; }
void SDIOBlockDevice::dmaStop() {}
#endif

bool SDIOBlockDevice::readBlocks(uint32_t lba, uint8_t *buffer, uint32_t count)
{
    if (_type == CARD_NONE || !count || lba >= _blocks || count > _blocks - lba) {
        return false;
    }
    const uint32_t address = _byteAddress ? lba * 512u : lba;
    const uint32_t bytes = count * 512u;
    const bool dma = _dma && !((uintptr_t)buffer & 3u);
    if (dma) {
        dmaStart(buffer, bytes, true);
    }
    startData(bytes, true, 9, dma);
    bool ok = commandR1(count > 1 ? 18 : 17, address);
    ok = ok && (dma ? dmaWait(true, 1000u + count) : readData(buffer, bytes));
    if (count > 1) {
        ok = commandR1(12, 0) && ok;
    }
    CH32RV_SDIO_DCTRL = 0;
    if (dma) {
        dmaStop();
        _dmaBlocks += count;
    } else {
        _cpuBlocks += count;
    }
    return waitTransferState(100) && ok;
}

bool SDIOBlockDevice::writeBlocks(uint32_t lba, const uint8_t *buffer, uint32_t count)
{
    if (_type == CARD_NONE || !count || lba >= _blocks || count > _blocks - lba) {
        return false;
    }
    const uint32_t address = _byteAddress ? lba * 512u : lba;
    const uint32_t bytes = count * 512u;
    const bool dma = _dma && !((uintptr_t)buffer & 3u);
    bool ok = commandR1(count > 1 ? 25 : 24, address);
    if (ok) {
        if (dma) {
            dmaStart(buffer, bytes, false);
        }
        startData(bytes, false, 9, dma);
        ok = dma ? dmaWait(false, 2000u + count) : writeData(buffer, bytes);
    }
    if (count > 1) {
        ok = commandR1(12, 0) && ok;
    }
    CH32RV_SDIO_DCTRL = 0;
    if (dma) {
        dmaStop();
        _dmaBlocks += count;
    } else {
        _cpuBlocks += count;
    }
    /* programming: the card leaves the transfer state until it is done */
    return waitTransferState(600) && ok;
}

/* A command whose answer is a short data block (SD's CMD6 status, MMC's
 * EXT_CSD), by the CPU. */
bool SDIOBlockDevice::readSmall(uint8_t index, uint32_t arg, uint32_t *buffer, uint32_t bytes, uint8_t blockLog2)
{
    startData(bytes, true, blockLog2, false);
    const bool ok = commandR1(index, arg) && readData((uint8_t *)buffer, bytes);
    CH32RV_SDIO_DCTRL = 0;
    return waitTransferState(100) && ok;
}

/* ---------------------------------------------------------------- bus setup */

static uint32_t clockDivider(uint32_t hz)
{
    /* SDIO_CK = HCLK / (CLKDIV + 2), CLKDIV 0..255 */
    uint32_t n = (CH32RV_SDIO_KERNEL_HZ + hz - 1u) / hz;
    if (n < 2u) {
        n = 2u;
    }
    if (n > 257u) {
        n = 257u;
    }
    return n - 2u;
}

void SDIOBlockDevice::setClock(uint32_t hz)
{
    const uint32_t div = clockDivider(hz);
    CH32RV_SDIO_CLKCR = div | _clkcr;
    _clockHz = CH32RV_SDIO_KERNEL_HZ / (div + 2u);
}

/* SD high speed: CMD6 asks (mode 0) whether function 1 of group 1 is there
 * (status bit 401), then switches to it (mode 1) and reads back the function
 * now selected (bits 379:376). The 512-bit status arrives most significant
 * byte first (SD Physical Layer 4.3.10). */
bool SDIOBlockDevice::sdHighSpeed()
{
    uint32_t status[16];
    const uint8_t *b = (const uint8_t *)status;
    if (!readSmall(6, 0x00FFFFF1u, status, 64, 6) || !(b[13] & 0x02u)) {
        return false;
    }
    return readSmall(6, 0x80FFFFF1u, status, 64, 6) && (b[16] & 0x0Fu) == 1u;
}

/* MMC's CMD6 SWITCH, write byte: EXT_CSD[index] = value. R1b: the card is
 * busy until the change is made; SWITCH_ERROR in the next status if it did
 * not take it. */
bool SDIOBlockDevice::mmcSwitch(uint8_t index, uint8_t value)
{
    _statusErrors = 0;
    return commandR1(6, (3u << 24) | ((uint32_t)index << 16) | ((uint32_t)value << 8)) && waitTransferState(500) &&
           !(_statusErrors & (1u << 7));   /* SWITCH_ERROR */
}

/* MMC 4.x and later (CSD SPEC_VERS >= 4): EXT_CSD for the size of a sector-
 * addressed card (SEC_COUNT, 215:212) and its speeds (CARD_TYPE, 196), then
 * the bus width (BUS_WIDTH, 183: 1 4 bits, 2 8 bits) - checked by reading
 * EXT_CSD again on the new width, put back to 1 bit if that fails - and high
 * speed (HS_TIMING, 185). maxHz comes back as the card's ceiling. */
bool SDIOBlockDevice::mmcSetup(uint8_t width, uint32_t &maxHz)
{
    uint32_t ext[128];   /* 512 bytes, on begin()'s stack */
    const uint8_t *b = (const uint8_t *)ext;
    if (!readSmall(8, 0, ext, 512, 9)) {
        return false;
    }
    if (!_byteAddress) {
        _blocks = (uint32_t)b[212] | ((uint32_t)b[213] << 8) | ((uint32_t)b[214] << 16) | ((uint32_t)b[215] << 24);
    }
    const uint8_t cardType = b[196];
    if (width >= 4) {
        const uint8_t code = width == 8 ? 2u : 1u;
        if (mmcSwitch(183, code)) {
            const uint32_t keep = _clkcr;
            _clkcr = (_clkcr & ~(3u << 11)) | (width == 8 ? CH32RV_SDIO_CLKCR_WIDBUS_8 : CH32RV_SDIO_CLKCR_WIDBUS_4);
            CH32RV_SDIO_CLKCR = (CH32RV_SDIO_CLKCR & 0xFFu) | _clkcr;
            if (readSmall(8, 0, ext, 512, 9) && b[183] == code) {
                _width = width;
            } else {
                _clkcr = keep;
                CH32RV_SDIO_CLKCR = (CH32RV_SDIO_CLKCR & 0xFFu) | _clkcr;
                mmcSwitch(183, 0);
            }
        }
    }
    const uint32_t legacy = 26000000u;
    if (maxHz > legacy && (cardType & 0x03u) && mmcSwitch(185, 1)) {
        _highSpeed = true;
        maxHz = (cardType & 0x02u) ? 52000000u : 26000000u;
    } else {
        maxHz = legacy;
    }
    return true;
}

/* ---------------------------------------------------------------- identification */

void SDIOBlockDevice::end()
{
    CH32RV_SDIO_CLKCR = 0;
    CH32RV_SDIO_POWER = 0;
#ifdef CH32RV_SDIO_DMA
    if (_dma) {
        dmaStop();
        ch32rv_dma_release(sdio_dma.dma, sdio_dma.channel);
    }
#endif
    _dma = false;
    _type = CARD_NONE;
    _blocks = 0;
    _clockHz = 0;
    _width = 1;
    _highSpeed = false;
}

/* MMC CSD (structure 1..3) capacity: C_SIZE, C_SIZE_MULT, READ_BL_LEN as
 * SD's version 1 (a sector-addressed card's comes from EXT_CSD). */
static uint32_t mmcCsdBlocks(const uint8_t csd[16])
{
    const uint32_t c_size = ((uint32_t)(csd[6] & 0x03) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
    const uint8_t mult = (uint8_t)(((csd[9] & 0x03) << 1) | (csd[10] >> 7));
    const uint8_t bl_len = csd[5] & 0x0F;
    if (bl_len < 9 || bl_len > 11) {
        return 0;
    }
    return (c_size + 1u) << (mult + 2u + bl_len - 9u);
}

bool SDIOBlockDevice::begin()
{
    end();
    const uint8_t width = _oneBit ? 1u : _pinWidth;
    _used = (uint8_t)(2u + width);
    if (!ch32rv_sdio_pick(pads, PAD_COUNT, _pin, _used, _chosen)) {
        return false;
    }
    ch32rv_sdio_pins_apply(pads, _chosen, _used);
    ch32rv_clock_enable(SDIO);
    _dmaBlocks = 0;
    _cpuBlocks = 0;

    /* Identification: 1 bit, the slowest the divider gives (400 kHz the
     * specification asks for; HCLK / 257 above 103 MHz, where cards in
     * practice still answer). */
    _clkcr = CH32RV_SDIO_CLKCR_CLKEN | (_flowControl ? CH32RV_SDIO_CLKCR_HWFC_EN : 0u);
    const uint32_t identDiv = clockDivider(400000u);
    _clockHz = CH32RV_SDIO_KERNEL_HZ / (identDiv + 2u);
    CH32RV_SDIO_CLKCR = identDiv;
    CH32RV_SDIO_POWER = 3u;
    delay(2);
    CH32RV_SDIO_CLKCR = identDiv | _clkcr;
    delay(2);   /* at least 74 clocks */

    command(0, 0, RESP_NONE);
    bool v2 = command(8, 0x1AAu, RESP_SHORT) && (CH32RV_SDIO_RESP1 & 0xFFFu) == 0x1AAu;
    sdcard_type_t type = CARD_SD;
    _byteAddress = true;
    const uint32_t t0 = millis();
    bool ready = false;
    if (command(55, 0, RESP_SHORT)) {
        while (!ready) {
            if (!command(41, 0x80100000u | (v2 ? 0x40000000u : 0), RESP_SHORT, false)) {
                break;
            }
            const uint32_t ocr = CH32RV_SDIO_RESP1;
            if (ocr & 0x80000000u) {
                ready = true;
                if (ocr & 0x40000000u) {
                    type = CARD_SDHC;
                    _byteAddress = false;
                }
                break;
            }
            if ((uint32_t)(millis() - t0) > 1500u || !command(55, 0, RESP_SHORT)) {
                break;
            }
            delay(5);
        }
    } else if (!v2) {
        /* No CMD55: an MMC (CMD1, sector mode asked, 2.7-3.6 V). */
        type = CARD_MMC;
        command(0, 0, RESP_NONE);
        while (command(1, 0x40FF8000u, RESP_SHORT, false)) {
            const uint32_t ocr = CH32RV_SDIO_RESP1;
            if (ocr & 0x80000000u) {
                ready = true;
                _byteAddress = (ocr & 0x60000000u) != 0x40000000u;
                break;
            }
            if ((uint32_t)(millis() - t0) > 1500u) {
                break;
            }
            delay(5);
        }
    }
    if (!ready) {
        end();
        return false;
    }

    /* CID, then the relative card address. */
    if (!command(2, 0, RESP_LONG)) {
        end();
        return false;
    }
    if (type == CARD_MMC) {
        _rca = 1;
        if (!commandR1(3, _rca << 16)) {
            end();
            return false;
        }
    } else {
        if (!command(3, 0, RESP_SHORT)) {
            end();
            return false;
        }
        _rca = CH32RV_SDIO_RESP1 >> 16;
    }

    /* CSD: the capacity. RESP1 holds bits 127-96. */
    if (!command(9, _rca << 16, RESP_LONG)) {
        end();
        return false;
    }
    const uint32_t r[4] = {CH32RV_SDIO_RESP1, CH32RV_SDIO_RESP2, CH32RV_SDIO_RESP3, CH32RV_SDIO_RESP4};
    uint8_t csd[16];
    for (uint8_t i = 0; i < 16; i++) {
        csd[i] = (uint8_t)(r[i / 4] >> (24 - 8 * (i % 4)));
    }
    _blocks = type == CARD_MMC ? mmcCsdBlocks(csd) : sd_csd_blocks(csd);
    if (!commandR1(7, _rca << 16) || !waitTransferState(500)) {
        end();
        return false;
    }
    if (_byteAddress && !commandR1(16, 512)) {
        end();
        return false;
    }

    /* The bus width, the timing, then the transfer clock: SD's default
     * speed is 25 MHz and high speed 50; MMC's legacy 20, 4.x's 26 and high
     * speed 26 / 52. High speed only when the ceiling is above default. */
    uint32_t maxHz = _maxKHz ? _maxKHz * 1000u : 25000000u;
    uint32_t cardHz;
    if (type == CARD_MMC) {
        cardHz = 20000000u;
        if ((csd[0] >> 2 & 0x0Fu) >= 4u) {
            setClock(cardHz < maxHz ? cardHz : maxHz);   /* EXT_CSD at legacy speed */
            cardHz = maxHz;
            if (!mmcSetup(width, cardHz)) {
                cardHz = 20000000u;
            }
        }
    } else {
        if (width >= 4 && commandR1(55, _rca << 16) && commandR1(6, 2)) {
            _clkcr |= CH32RV_SDIO_CLKCR_WIDBUS_4;
            CH32RV_SDIO_CLKCR = (CH32RV_SDIO_CLKCR & 0xFFu) | _clkcr;
            _width = 4;
        }
        cardHz = 25000000u;
        if (maxHz > cardHz && v2) {
            setClock(cardHz);
            if (sdHighSpeed()) {
                _highSpeed = true;
                cardHz = 50000000u;
            }
        }
    }
    if (!_blocks) {
        end();
        return false;
    }
    setClock(maxHz < cardHz ? maxHz : cardHz);

#ifdef CH32RV_SDIO_DMA
    _dma = _wantDma && ch32rv_dma_claim(sdio_dma.dma, sdio_dma.channel);
#endif
    _type = type;
    return true;
}

#endif

/* SD / MMC on the SDIO host (SD Physical Layer Simplified Specification 9.00,
 * chapter 4): identification in 1-bit mode at the slowest clock, 4-bit and
 * the transfer clock once selected; blocks through the FIFO by the CPU. */
#include "SDIOBlockDevice.h"

#include "sdio_hw.h"

#include <string.h>

#if !SDIO_HW

bool SDIOBlockDevice::setPins(int, int, int, int, int, int) { return false; }
bool SDIOBlockDevice::begin() { return false; }
void SDIOBlockDevice::end() {}
bool SDIOBlockDevice::readBlocks(uint32_t, uint8_t *, uint32_t) { return false; }
bool SDIOBlockDevice::writeBlocks(uint32_t, const uint8_t *, uint32_t) { return false; }

#else

enum : uint8_t { RESP_NONE, RESP_SHORT, RESP_LONG };

/* R1's error bits (card status 31-19, 16, 13, 3: out of range, address,
 * block length, erase, write protect, lock, CRC, illegal command, ECC,
 * controller, general, CSD overwrite, erase skip, AKE). */
static const uint32_t R1_ERRORS = 0xFDFFE008u;

/* ---------------------------------------------------------------- pins */

/* Pin index: CK, CMD, D0, D1, D2, D3. */
struct SdioPad {
    uint8_t index;
    uint8_t pin;
    uint8_t af;   /* H41x: the AF number; V4x7: 1 for the remapped D0 / D1 */
};

#if defined(SDIO_HW_V30X)
static const SdioPad pads[] = {
    {0, PC12, 0}, {1, PD2, 0}, {2, PC8, 0}, {3, PC9, 0}, {4, PC10, 0}, {5, PC11, 0},
};
#elif defined(SDIO_HW_V4X7)
static const SdioPad pads[] = {
    {0, PC12, 0}, {1, PD2, 0}, {2, PC8, 0}, {3, PC9, 0}, {4, PC10, 0}, {5, PC11, 0},
    {2, PB14, 1}, {3, PB15, 1},
};
#elif defined(CH32RV_SERIES_CH32H417) /* the EVT's set first (the default) */
static const SdioPad pads[] = {
    {0, PB11, 8}, {1, PB10, 8}, {2, PE8, 8}, {3, PE9, 8}, {4, PE10, 8}, {5, PE11, 8},
    {0, PC1, 11}, {1, PA0, 9}, {2, PB14, 9}, {3, PB15, 9}, {4, PB3, 9}, {5, PB4, 9},
};
#else /* CH32H415 (64 pins): no PE8-PE10 */
static const SdioPad pads[] = {
    {0, PB11, 8}, {1, PB10, 8}, {2, PB14, 9}, {3, PB15, 9}, {4, PB3, 9}, {5, PB4, 9},
    {0, PC1, 11}, {1, PA0, 9}, {5, PE11, 8},
};
#endif

static const SdioPad *findPad(uint8_t index, int pin)
{
    for (const SdioPad &p : pads) {
        if (p.index == index && (pin < 0 || p.pin == pin)) {
            return &p;
        }
    }
    return nullptr;
}

bool SDIOBlockDevice::setPins(int clk, int cmd, int d0, int d1, int d2, int d3)
{
    const int want[6] = {clk, cmd, d0, d1, d2, d3};
    const bool wideGiven = d1 >= 0 || d2 >= 0 || d3 >= 0;
    if (wideGiven && (d1 < 0 || d2 < 0 || d3 < 0)) {
        return false;
    }
    for (uint8_t i = 0; i < 6; i++) {
        if (want[i] >= 0 && !findPad(i, want[i])) {
            return false;
        }
    }
#if defined(SDIO_HW_V4X7)
    /* D0 and D1 move together (one remap bit). */
    if (d0 >= 0 && d1 >= 0 && findPad(2, d0)->af != findPad(3, d1)->af) {
        return false;
    }
#endif
    for (uint8_t i = 0; i < 6; i++) {
        _pin[i] = (int8_t)want[i];
    }
    if (!wideGiven) {
        _oneBit = true;
    }
    return true;
}

void SDIOBlockDevice::setupPins()
{
    for (uint8_t i = 0; i < 6; i++) {
        if (i >= 3 && _oneBit) {
            break;
        }
        const SdioPad *p = findPad(i, _pin[i]);
        const uint8_t port = (uint8_t)CH32RV_PIN_PORT(p->pin), bit = (uint8_t)CH32RV_PIN_BIT(p->pin);
        ch32rv_gpio_clock_enable(port);
#if defined(SDIO_HW_H41X)
        ch32rv_pin_set_af(p->pin, p->af);
#endif
        ch32rv_gpio_set_config(port, bit, SDIO_PIN_AF_PP);
    }
#if defined(SDIO_HW_V4X7)
    ch32rv_clock_enable(AFIO);
    const SdioPad *d0 = findPad(2, _pin[2]);
    if (d0->af) {
        CH32RV_REG32(CH32RV_AFIO_BASE + 0x1Cu) |= 1u << 12;
    } else {
        CH32RV_REG32(CH32RV_AFIO_BASE + 0x1Cu) &= ~(1u << 12);
    }
#endif
}

/* ---------------------------------------------------------------- commands */

bool SDIOBlockDevice::command(uint8_t index, uint32_t arg, uint8_t response, bool checkCrc)
{
    SDIO_ICR = SDIO_ICR_ALL;
    SDIO_ARG = arg;
    SDIO_CMD = index | (response == RESP_SHORT ? SDIO_CMD_RESP_SHORT : response == RESP_LONG ? SDIO_CMD_RESP_LONG : 0) |
               SDIO_CMD_CPSMEN;
    const uint32_t t0 = millis();
    for (;;) {
        const uint32_t sta = SDIO_STA;
        if (response == RESP_NONE) {
            if (sta & SDIO_STA_CMDSENT) {
                return true;
            }
        } else {
            if (sta & SDIO_STA_CTIMEOUT) {
                return false;
            }
            if (sta & SDIO_STA_CCRCFAIL) {
                return !checkCrc;   /* R3 (OCR) carries no valid CRC */
            }
            if (sta & SDIO_STA_CMDREND) {
                /* R2 and R3 do not echo the index. */
                return response == RESP_LONG || !checkCrc || (SDIO_RESPCMD & 0x3Fu) == index;
            }
        }
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;
        }
    }
}

bool SDIOBlockDevice::commandR1(uint8_t index, uint32_t arg)
{
    return command(index, arg, RESP_SHORT) && !(SDIO_RESP1 & R1_ERRORS);
}

/* CMD13 until the card is back in the transfer state and ready for data. */
bool SDIOBlockDevice::waitTransferState(uint32_t timeoutMs)
{
    const uint32_t t0 = millis();
    for (;;) {
        if (commandR1(13, _rca << 16)) {
            const uint32_t status = SDIO_RESP1;
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

void SDIOBlockDevice::startData(uint32_t bytes, bool read)
{
    SDIO_DCTRL = 0;
    SDIO_ICR = SDIO_ICR_ALL;
    /* 250 ms for a read, 500 ms for a write, in card clocks. */
    SDIO_DTIMER = (_clockHz / 1000u) * (read ? 250u : 500u);
    SDIO_DLEN = bytes;
    SDIO_DCTRL = SDIO_DCTRL_BLOCK_512 | (read ? SDIO_DCTRL_DTDIR_READ : 0) | SDIO_DCTRL_DTEN;
}

bool SDIOBlockDevice::readData(uint8_t *buffer, uint32_t bytes)
{
    uint32_t words = bytes / 4u;
    const uint32_t t0 = millis();
    for (;;) {
        const uint32_t sta = SDIO_STA;
        if (sta & SDIO_STA_DATA_ERRORS) {
            return false;
        }
        if ((sta & SDIO_STA_RXFIFOHF) && words >= 8u) {
            for (uint8_t i = 0; i < 8; i++) {
                const uint32_t w = SDIO_FIFO;
                memcpy(buffer, &w, 4);
                buffer += 4;
            }
            words -= 8u;
        } else if ((sta & SDIO_STA_RXDAVL) && words) {
            const uint32_t w = SDIO_FIFO;
            memcpy(buffer, &w, 4);
            buffer += 4;
            words--;
        } else if (!words && (sta & SDIO_STA_DATAEND)) {
            return true;
        }
        if ((uint32_t)(millis() - t0) > 1000u) {
            return false;
        }
    }
}

bool SDIOBlockDevice::writeData(const uint8_t *buffer, uint32_t bytes)
{
    uint32_t words = bytes / 4u;
    const uint32_t t0 = millis();
    for (;;) {
        const uint32_t sta = SDIO_STA;
        if (sta & SDIO_STA_DATA_ERRORS) {
            return false;
        }
        if ((sta & SDIO_STA_TXFIFOHE) && words) {
            /* half empty: room for 8 words */
            for (uint8_t i = 0; i < 8 && words; i++, words--) {
                uint32_t w;
                memcpy(&w, buffer, 4);
                SDIO_FIFO = w;
                buffer += 4;
            }
        } else if (!words && (sta & SDIO_STA_DATAEND)) {
            return true;
        }
        if ((uint32_t)(millis() - t0) > 2000u) {
            return false;
        }
    }
}

bool SDIOBlockDevice::readBlocks(uint32_t lba, uint8_t *buffer, uint32_t count)
{
    if (_type == CARD_NONE || !count || lba >= _blocks || count > _blocks - lba) {
        return false;
    }
    const uint32_t address = _byteAddress ? lba * 512u : lba;
    startData(count * 512u, true);
    bool ok = commandR1(count > 1 ? 18 : 17, address) && readData(buffer, count * 512u);
    if (count > 1) {
        ok = commandR1(12, 0) && ok;
    }
    SDIO_DCTRL = 0;
    return waitTransferState(100) && ok;
}

bool SDIOBlockDevice::writeBlocks(uint32_t lba, const uint8_t *buffer, uint32_t count)
{
    if (_type == CARD_NONE || !count || lba >= _blocks || count > _blocks - lba) {
        return false;
    }
    const uint32_t address = _byteAddress ? lba * 512u : lba;
    bool ok = commandR1(count > 1 ? 25 : 24, address);
    if (ok) {
        startData(count * 512u, false);
        ok = writeData(buffer, count * 512u);
    }
    if (count > 1) {
        ok = commandR1(12, 0) && ok;
    }
    SDIO_DCTRL = 0;
    /* programming: the card leaves the transfer state until it is done */
    return waitTransferState(600) && ok;
}

/* ---------------------------------------------------------------- identification */

static uint32_t clockDivider(uint32_t hz)
{
    /* SDIO_CK = HCLK / (CLKDIV + 2), CLKDIV 0..255 */
    uint32_t n = (SDIO_KERNEL_HZ + hz - 1u) / hz;
    if (n < 2u) {
        n = 2u;
    }
    if (n > 257u) {
        n = 257u;
    }
    return n - 2u;
}

void SDIOBlockDevice::end()
{
    SDIO_CLKCR = 0;
    SDIO_POWER = 0;
    _type = CARD_NONE;
    _blocks = 0;
    _clockHz = 0;
    _wide = false;
}

bool SDIOBlockDevice::begin()
{
    end();
    for (uint8_t i = 0; i < 6; i++) {
        if (!findPad(i, _pin[i])) {
            return false;
        }
        if (_pin[i] < 0) {
            _pin[i] = (int8_t)findPad(i, -1)->pin;
        }
    }
    setupPins();
    ch32rv_clock_enable(SDIO);

    /* Identification: 1 bit, the slowest the divider gives (400 kHz the
     * specification asks for; HCLK / 257 above 103 MHz, where cards in
     * practice still answer). */
    const uint32_t identDiv = clockDivider(400000u);
    _clockHz = SDIO_KERNEL_HZ / (identDiv + 2u);
    SDIO_CLKCR = identDiv;
    SDIO_POWER = 3u;
    delay(2);
    SDIO_CLKCR = identDiv | SDIO_CLKCR_CLKEN;
    delay(2);   /* at least 74 clocks */

    command(0, 0, RESP_NONE);
    bool v2 = command(8, 0x1AAu, RESP_SHORT) && (SDIO_RESP1 & 0xFFFu) == 0x1AAu;
    sdcard_type_t type = CARD_SD;
    _byteAddress = true;
    const uint32_t t0 = millis();
    bool ready = false;
    if (command(55, 0, RESP_SHORT)) {
        while (!ready) {
            if (!command(41, 0x80100000u | (v2 ? 0x40000000u : 0), RESP_SHORT, false)) {
                break;
            }
            const uint32_t ocr = SDIO_RESP1;
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
        /* No CMD55: an MMC (CMD1, sector mode asked). */
        type = CARD_MMC;
        command(0, 0, RESP_NONE);
        while (command(1, 0x40FF8000u, RESP_SHORT, false)) {
            const uint32_t ocr = SDIO_RESP1;
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
        _rca = SDIO_RESP1 >> 16;
    }

    /* CSD: the capacity. RESP1 holds bits 127-96. */
    if (!command(9, _rca << 16, RESP_LONG)) {
        end();
        return false;
    }
    const uint32_t r[4] = {SDIO_RESP1, SDIO_RESP2, SDIO_RESP3, SDIO_RESP4};
    uint8_t csd[16];
    for (uint8_t i = 0; i < 16; i++) {
        csd[i] = (uint8_t)(r[i / 4] >> (24 - 8 * (i % 4)));
    }
    _blocks = sd_csd_blocks(csd);
    if (!_blocks || !commandR1(7, _rca << 16) || !waitTransferState(500)) {
        end();
        return false;
    }
    if (_byteAddress && !commandR1(16, 512)) {
        end();
        return false;
    }

    /* Four lines (SD: ACMD6), then the transfer clock. */
    uint32_t clkcr = SDIO_CLKCR_CLKEN | SDIO_CLKCR_HWFC_EN;
    if (!_oneBit && type != CARD_MMC && commandR1(55, _rca << 16) && commandR1(6, 2)) {
        clkcr |= SDIO_CLKCR_WIDBUS_4;
        _wide = true;
    }
    uint32_t maxHz = _maxKHz * 1000u;
    if (maxHz > 25000000u || !maxHz) {
        maxHz = 25000000u;   /* default speed; high speed (CMD6) is not switched to */
    }
    const uint32_t div = clockDivider(maxHz);
    SDIO_CLKCR = div | clkcr;
    _clockHz = SDIO_KERNEL_HZ / (div + 2u);
    _type = type;
    return true;
}

#endif

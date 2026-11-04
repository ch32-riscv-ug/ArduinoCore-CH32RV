/* SD / MMC in SPI mode (SD Physical Layer Simplified Specification 9.00,
 * chapter 7): identification at 400 kHz, then multiple-block reads and
 * writes (CMD18 / CMD25) at the transfer clock, every command and data block
 * with its CRC when SD_SPI_CRC is on. */
#include "SDSPIBlockDevice.h"

#include <string.h>

#define CMD0 0     /* GO_IDLE_STATE */
#define CMD1 1     /* SEND_OP_COND (MMC) */
#define CMD8 8     /* SEND_IF_COND */
#define CMD9 9     /* SEND_CSD */
#define CMD12 12   /* STOP_TRANSMISSION */
#define CMD13 13   /* SEND_STATUS */
#define CMD16 16   /* SET_BLOCKLEN */
#define CMD17 17   /* READ_SINGLE_BLOCK */
#define CMD18 18   /* READ_MULTIPLE_BLOCK */
#define CMD24 24   /* WRITE_BLOCK */
#define CMD25 25   /* WRITE_MULTIPLE_BLOCK */
#define CMD55 55   /* APP_CMD */
#define CMD58 58   /* READ_OCR */
#define CMD59 59   /* CRC_ON_OFF */
#define ACMD41 41  /* SD_SEND_OP_COND */

#define R1_IDLE 0x01
#define R1_ILLEGAL 0x04
#define TOKEN_SINGLE 0xFE      /* read, single and multiple write data start */
#define TOKEN_MULTI_WRITE 0xFC
#define TOKEN_STOP 0xFD

/* Timeouts (the specification's, with margin): a read's data token in
 * 100 ms, a write's busy in 250 ms (500 ms on SDXC), initialisation in 1 s. */
static const uint32_t READ_MS = 200, WRITE_MS = 600, INIT_MS = 1500;

static uint8_t crc7(const uint8_t *p, uint8_t n)
{
    uint8_t crc = 0;
    while (n--) {
        uint8_t d = *p++;
        for (uint8_t i = 0; i < 8; i++) {
            crc <<= 1;
            if ((d ^ crc) & 0x80) {
                crc ^= 0x09;
            }
            d <<= 1;
        }
    }
    return (uint8_t)((crc << 1) | 1);
}

#if SD_SPI_CRC
/* CRC-16-CCITT, four bits at a time. */
static uint16_t crc16(const uint8_t *p, uint16_t n)
{
    static const uint16_t table[16] = {0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
                                       0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF};
    uint16_t crc = 0;
    while (n--) {
        crc = (uint16_t)((crc << 4) ^ table[((crc >> 12) ^ (*p >> 4)) & 0x0F]);
        crc = (uint16_t)((crc << 4) ^ table[((crc >> 12) ^ (*p & 0x0F)) & 0x0F]);
        p++;
    }
    return crc;
}
#endif

void SDSPIBlockDevice::select(bool fast)
{
    _spi->beginTransaction(SPISettings(fast ? _frequency : 400000, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
}

void SDSPIBlockDevice::deselect()
{
    digitalWrite(_cs, HIGH);
    xfer(0xFF);   /* the card lets go of MISO on the next clock */
    _spi->endTransaction();
}

bool SDSPIBlockDevice::waitReady(uint32_t timeoutMs)
{
    const uint32_t t0 = millis();
    while (xfer(0xFF) != 0xFF) {
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
        yield();
    }
    return true;
}

bool SDSPIBlockDevice::waitToken(uint8_t *token, uint32_t timeoutMs)
{
    const uint32_t t0 = millis();
    while ((*token = xfer(0xFF)) == 0xFF) {
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
    }
    return true;
}

/* A command with CS low; its R1 (0xFF: no answer). */
uint8_t SDSPIBlockDevice::command(uint8_t cmd, uint32_t arg)
{
    if (cmd != CMD0 && cmd != CMD12 && !waitReady(500)) {
        return 0xFF;
    }
    uint8_t frame[6] = {(uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8),
                        (uint8_t)arg, 0};
    frame[5] = crc7(frame, 5);
    for (uint8_t b : frame) {
        xfer(b);
    }
    if (cmd == CMD12) {
        xfer(0xFF);   /* a stuff byte before CMD12's response */
    }
    uint8_t r1 = 0xFF;
    for (uint8_t i = 0; i < 10 && (r1 & 0x80); i++) {
        r1 = xfer(0xFF);
    }
    return r1;
}

uint8_t SDSPIBlockDevice::appCommand(uint8_t cmd, uint32_t arg)
{
    command(CMD55, 0);
    return command(cmd, arg);
}

bool SDSPIBlockDevice::begin()
{
    if (!_spi || _cs < 0) {
        return false;
    }
    _type = CARD_NONE;
    _blocks = 0;
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    /* At least 74 clocks with CS high, then CMD0 puts the card in SPI mode. */
    _spi->beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (uint8_t i = 0; i < 10; i++) {
        xfer(0xFF);
    }
    _spi->endTransaction();
    select(false);
    uint8_t r1 = 0xFF;
    for (uint8_t tries = 0; tries < 10 && r1 != R1_IDLE; tries++) {
        r1 = command(CMD0, 0);
    }
    if (r1 != R1_IDLE) {
        deselect();
        return false;
    }
#if SD_SPI_CRC
    command(CMD59, 1);
#endif
    sdcard_type_t type = CARD_SD;
    bool v2 = false;
    if (!(command(CMD8, 0x1AA) & R1_ILLEGAL)) {
        uint8_t r7[4];
        for (uint8_t &b : r7) {
            b = xfer(0xFF);
        }
        if (r7[2] != 0x01 || r7[3] != 0xAA) {
            deselect();
            return false;   /* not 2.7-3.6 V, or the pattern came back wrong */
        }
        v2 = true;
    }
    const uint32_t t0 = millis();
    bool mmc = false;
    do {
        r1 = mmc ? command(CMD1, 0) : appCommand(ACMD41, v2 ? 0x40000000u : 0);
        if (!mmc && (r1 & R1_ILLEGAL) && !v2) {
            mmc = true;   /* no ACMD41: an MMC, which takes CMD1 */
            type = CARD_MMC;
            continue;
        }
        if ((uint32_t)(millis() - t0) > INIT_MS) {
            deselect();
            return false;
        }
    } while (r1 != 0);
    _byteAddress = true;
    if (v2 && command(CMD58, 0) == 0) {
        uint8_t ocr[4];
        for (uint8_t &b : ocr) {
            b = xfer(0xFF);
        }
        if (ocr[0] & 0x40) {   /* CCS: block addressed */
            _byteAddress = false;
            type = CARD_SDHC;
        }
    }
    if (_byteAddress && command(CMD16, 512) != 0) {
        deselect();
        return false;
    }
    uint8_t csd[16];
    const bool ok = command(CMD9, 0) == 0 && readData(csd, sizeof csd);
    deselect();
    if (!ok || !(_blocks = sd_csd_blocks(csd))) {
        return false;
    }
    _type = type;
    return true;
}

/* A data block after its start token, and its CRC. */
bool SDSPIBlockDevice::readData(uint8_t *buffer, uint16_t length)
{
    uint8_t token;
    if (!waitToken(&token, READ_MS) || token != TOKEN_SINGLE) {
        return false;
    }
    memset(buffer, 0xFF, length);
    _spi->transfer(buffer, length);
    const uint16_t crc = (uint16_t)(xfer(0xFF) << 8);
    const uint16_t got = (uint16_t)(crc | xfer(0xFF));
#if SD_SPI_CRC
    return got == crc16(buffer, length);
#else
    (void)got;
    return true;
#endif
}

bool SDSPIBlockDevice::writeData(uint8_t token, const uint8_t *buffer)
{
    xfer(token);
    _spi->writeBytes(buffer, 512);
#if SD_SPI_CRC
    const uint16_t crc = crc16(buffer, 512);
#else
    const uint16_t crc = 0xFFFF;
#endif
    xfer((uint8_t)(crc >> 8));
    xfer((uint8_t)crc);
    if ((xfer(0xFF) & 0x1F) != 0x05) {   /* data response: accepted */
        return false;
    }
    return waitReady(WRITE_MS);          /* programming */
}

bool SDSPIBlockDevice::stopTransmission()
{
    command(CMD12, 0);
    return waitReady(WRITE_MS);
}

bool SDSPIBlockDevice::readBlocks(uint32_t lba, uint8_t *buffer, uint32_t count)
{
    if (_type == CARD_NONE || lba >= _blocks || count > _blocks - lba || !count) {
        return false;
    }
    const uint32_t address = _byteAddress ? lba * 512u : lba;
    select(true);
    bool ok;
    if (count == 1) {
        ok = command(CMD17, address) == 0 && readData(buffer, 512);
    } else {
        ok = command(CMD18, address) == 0;
        for (uint32_t i = 0; ok && i < count; i++) {
            ok = readData(buffer + i * 512u, 512);
        }
        ok = stopTransmission() && ok;
    }
    deselect();
    return ok;
}

bool SDSPIBlockDevice::writeBlocks(uint32_t lba, const uint8_t *buffer, uint32_t count)
{
    if (_type == CARD_NONE || lba >= _blocks || count > _blocks - lba || !count) {
        return false;
    }
    const uint32_t address = _byteAddress ? lba * 512u : lba;
    select(true);
    bool ok;
    if (count == 1) {
        ok = command(CMD24, address) == 0 && writeData(TOKEN_SINGLE, buffer);
    } else {
        ok = command(CMD25, address) == 0;
        for (uint32_t i = 0; ok && i < count; i++) {
            ok = writeData(TOKEN_MULTI_WRITE, buffer + i * 512u);
        }
        xfer(TOKEN_STOP);
        xfer(0xFF);
        ok = waitReady(WRITE_MS) && ok;
    }
    /* A write the card took but failed to program shows only in its status. */
    if (ok) {
        ok = command(CMD13, 0) == 0 && xfer(0xFF) == 0;
    }
    deselect();
    return ok;
}

bool SDSPIBlockDevice::sync()
{
    /* Every write has waited out the card's busy before returning. */
    return _type != CARD_NONE;
}

/* SD card in SPI mode (SD Physical Layer Simplified Specification, ch. 7):
 * identification at 400 kHz, then single-block reads and writes. */
#include "CH32RVUsbDeviceMscSdCard.h"

#include <string.h>

#define CMD0   0     /* GO_IDLE_STATE */
#define CMD8   8     /* SEND_IF_COND */
#define CMD9   9     /* SEND_CSD */
#define CMD16  16    /* SET_BLOCKLEN */
#define CMD17  17    /* READ_SINGLE_BLOCK */
#define CMD24  24    /* WRITE_BLOCK */
#define CMD55  55    /* APP_CMD */
#define CMD58  58    /* READ_OCR */
#define ACMD41 41    /* SD_SEND_OP_COND */
#define TOKEN_START 0xFE

void CH32RVUsbDeviceMscSdCard::select()
{
    _spi->beginTransaction(arduino::SPISettings(_mounted ? _frequency : 400000, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
}

void CH32RVUsbDeviceMscSdCard::deselect()
{
    digitalWrite(_cs, HIGH);
    xfer(0xFF);                      /* the card releases MISO on the next clock */
    _spi->endTransaction();
}

bool CH32RVUsbDeviceMscSdCard::waitReady(uint32_t timeoutMs)
{
    const uint32_t t0 = millis();
    while (xfer(0xFF) != 0xFF) {
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
    }
    return true;
}

/* Sends a command (CS already low) and returns its R1 response. */
uint8_t CH32RVUsbDeviceMscSdCard::command(uint8_t cmd, uint32_t arg)
{
    if (cmd != CMD0) {
        waitReady(300);
    }
    xfer((uint8_t)(0x40 | cmd));
    xfer((uint8_t)(arg >> 24));
    xfer((uint8_t)(arg >> 16));
    xfer((uint8_t)(arg >> 8));
    xfer((uint8_t)arg);
    /* Only CMD0 and CMD8 are checked by the card in SPI mode. */
    xfer(cmd == CMD0 ? 0x95 : cmd == CMD8 ? 0x87 : 0x01);
    uint8_t r1 = 0xFF;
    for (uint8_t i = 0; i < 10 && (r1 & 0x80); i++) {
        r1 = xfer(0xFF);
    }
    return r1;
}

bool CH32RVUsbDeviceMscSdCard::begin(int ssPin, arduino::HardwareSPI &spi, uint32_t frequency)
{
    _spi = &spi;
    _cs = ssPin;
    _frequency = frequency;
    _mounted = false;
    _blockCount = 0;
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    /* At least 74 clocks with CS high, then CMD0 into SPI mode. */
    _spi->beginTransaction(arduino::SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (uint8_t i = 0; i < 10; i++) {
        xfer(0xFF);
    }
    _spi->endTransaction();
    select();
    uint8_t r1 = 0;
    for (uint8_t tries = 0; tries < 10 && (r1 = command(CMD0, 0)) != 0x01; tries++) {
    }
    if (r1 != 0x01) {
        deselect();
        return false;
    }
    bool v2 = false;
    if (command(CMD8, 0x1AA) == 0x01) {
        uint8_t r7[4];
        for (uint8_t i = 0; i < 4; i++) {
            r7[i] = xfer(0xFF);
        }
        if (r7[2] != 0x01 || r7[3] != 0xAA) {
            deselect();
            return false;            /* voltage range not accepted */
        }
        v2 = true;
    }
    const uint32_t t0 = millis();
    do {
        command(CMD55, 0);
        r1 = command(ACMD41, v2 ? 0x40000000u : 0);
        if ((uint32_t)(millis() - t0) > 1500) {
            deselect();
            return false;
        }
    } while (r1 != 0);
    _blockAddressing = false;
    if (v2 && command(CMD58, 0) == 0) {
        const uint8_t ocr0 = xfer(0xFF);
        xfer(0xFF);
        xfer(0xFF);
        xfer(0xFF);
        _blockAddressing = (ocr0 & 0x40) != 0;   /* CCS */
    }
    if (!_blockAddressing && command(CMD16, 512) != 0) {
        deselect();
        return false;
    }
    /* Capacity from the CSD. */
    uint8_t csd[16];
    bool ok = command(CMD9, 0) == 0;
    if (ok) {
        const uint32_t t1 = millis();
        uint8_t token;
        while ((token = xfer(0xFF)) == 0xFF && (uint32_t)(millis() - t1) < 300) {
        }
        ok = token == TOKEN_START;
        for (uint8_t i = 0; ok && i < 16; i++) {
            csd[i] = xfer(0xFF);
        }
        xfer(0xFF);
        xfer(0xFF);
    }
    deselect();
    if (!ok) {
        return false;
    }
    if ((csd[0] >> 6) == 1) {
        /* CSD 2.0: (C_SIZE + 1) * 512 KB. */
        const uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
        _blockCount = (c_size + 1) * 1024u;
    } else {
        /* CSD 1.0: (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) blocks of 2^READ_BL_LEN. */
        const uint32_t c_size = ((uint32_t)(csd[6] & 0x03) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        const uint8_t mult = (uint8_t)(((csd[9] & 0x03) << 1) | (csd[10] >> 7));
        const uint8_t bl_len = csd[5] & 0x0F;
        _blockCount = (c_size + 1) << (mult + 2 + bl_len - 9);
    }
    _mounted = _blockCount != 0;
    return _mounted;
}

bool CH32RVUsbDeviceMscSdCard::attach(CH32RVUsbDeviceMsc &msc)
{
    if (!_mounted) {
        return false;
    }
    msc.setStorage(this);
    msc.isWritable(!_readOnly);
    return msc.begin(_blockCount, 512);
}

bool CH32RVUsbDeviceMscSdCard::readBlock(uint32_t lba, uint8_t *buffer)
{
    if (!_mounted || lba >= _blockCount) {
        return false;
    }
    select();
    bool ok = command(CMD17, _blockAddressing ? lba : lba * 512u) == 0;
    if (ok) {
        const uint32_t t0 = millis();
        uint8_t token;
        while ((token = xfer(0xFF)) == 0xFF && (uint32_t)(millis() - t0) < 300) {
        }
        ok = token == TOKEN_START;
        if (ok) {
            memset(buffer, 0xFF, 512);
            _spi->transfer(buffer, 512);
            xfer(0xFF);              /* CRC, not checked in SPI mode */
            xfer(0xFF);
        }
    }
    deselect();
    return ok;
}

bool CH32RVUsbDeviceMscSdCard::writeBlock(uint32_t lba, const uint8_t *buffer)
{
    if (!_mounted || _readOnly || lba >= _blockCount) {
        return false;
    }
    select();
    bool ok = command(CMD24, _blockAddressing ? lba : lba * 512u) == 0;
    if (ok) {
        xfer(0xFF);
        xfer(TOKEN_START);
        for (uint16_t i = 0; i < 512; i++) {
            xfer(buffer[i]);
        }
        xfer(0xFF);
        xfer(0xFF);
        ok = (xfer(0xFF) & 0x1F) == 0x05;   /* data accepted */
        ok = waitReady(500) && ok;          /* programming */
    }
    deselect();
    return ok;
}

/* The MSC class hands over at most its transfer buffer at a time, block
 * aligned when that buffer is a multiple of 512 (the default). */
int32_t CH32RVUsbDeviceMscSdCard::read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    static uint8_t block[512];
    uint8_t *out = (uint8_t *)buffer;
    uint32_t done = 0;
    while (done < size) {
        const uint32_t at = offset + done;
        const uint32_t n = (512 - at % 512) < (size - done) ? 512 - at % 512 : size - done;
        if (at % 512 == 0 && n == 512) {
            if (!readBlock(lba + at / 512, out + done)) {
                return -1;
            }
        } else {
            if (!readBlock(lba + at / 512, block)) {
                return -1;
            }
            memcpy(out + done, block + at % 512, n);
        }
        done += n;
    }
    return (int32_t)size;
}

int32_t CH32RVUsbDeviceMscSdCard::write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    static uint8_t block[512];
    uint32_t done = 0;
    while (done < size) {
        const uint32_t at = offset + done;
        const uint32_t n = (512 - at % 512) < (size - done) ? 512 - at % 512 : size - done;
        if (at % 512 == 0 && n == 512) {
            if (!writeBlock(lba + at / 512, buffer + done)) {
                return -1;
            }
        } else {
            /* A partial block: read, patch, write back. */
            if (!readBlock(lba + at / 512, block)) {
                return -1;
            }
            memcpy(block + at % 512, buffer + done, n);
            if (!writeBlock(lba + at / 512, block)) {
                return -1;
            }
        }
        done += n;
    }
    return (int32_t)size;
}

bool CH32RVUsbDeviceMscSdCard::startStop(uint8_t powerCondition, bool start, bool loadEject)
{
    (void)powerCondition;
    (void)loadEject;
    if (!start && _onEject) {
        _onEject();
    }
    return true;
}

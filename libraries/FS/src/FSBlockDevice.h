/* What a FAT volume sits on: numbered 512-byte blocks. An SD card (SPI or
 * SDIO), eMMC, a USB stick through the host's MSC, a RAM disk. The same
 * object can also be what the USB MSC device shows a PC
 * (CH32RVUsbDeviceMscBlockStorage), as long as only one side has it at a
 * time. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace fs {

class BlockDevice {
public:
    /* Bring the medium up (an SD card's initialisation); false: absent. */
    virtual bool begin() { return true; }
    virtual bool readBlocks(uint32_t lba, uint8_t *buffer, uint32_t count) = 0;
    virtual bool writeBlocks(uint32_t lba, const uint8_t *buffer, uint32_t count) = 0;
    /* Everything written so far is on the medium. */
    virtual bool sync() { return true; }
    virtual uint32_t blockCount() = 0;
    virtual bool writable() { return true; }
    /* The erase unit in blocks (format aligns the data area to it); 1: none. */
    virtual uint32_t eraseBlocks() { return 1; }

    static const uint16_t BLOCK_SIZE = 512;

protected:
    ~BlockDevice() = default;
};

/* Blocks in RAM the sketch owns (at least 64 of them for a FAT volume). */
class RamBlockDevice : public BlockDevice {
public:
    RamBlockDevice(uint8_t *storage, uint32_t blockCount) : _storage(storage), _count(storage ? blockCount : 0) {}

    bool readBlocks(uint32_t lba, uint8_t *buffer, uint32_t count) override
    {
        if (!fits(lba, count)) {
            return false;
        }
        memcpy(buffer, _storage + (size_t)lba * BLOCK_SIZE, (size_t)count * BLOCK_SIZE);
        return true;
    }
    bool writeBlocks(uint32_t lba, const uint8_t *buffer, uint32_t count) override
    {
        if (!fits(lba, count)) {
            return false;
        }
        memcpy(_storage + (size_t)lba * BLOCK_SIZE, buffer, (size_t)count * BLOCK_SIZE);
        return true;
    }
    uint32_t blockCount() override { return _count; }
    uint8_t *data() { return _storage; }

private:
    bool fits(uint32_t lba, uint32_t count) const { return lba < _count && count <= _count - lba; }
    uint8_t *_storage;
    uint32_t _count;
};

} // namespace fs

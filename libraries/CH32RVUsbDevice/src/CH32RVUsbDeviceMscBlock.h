/* USB mass storage served from an fs::BlockDevice (the FS library): the same
 * RAM disk, SD card or eMMC a FatFS mounts can be shown to a PC.
 *
 * Only one side may have the volume at a time: both cache FAT sectors, and
 * neither sees the other's writes. Show it to the PC with the volume
 * unmounted (fat.end(), then msc.mediaPresent(true)); take it back when the
 * PC has ejected it (onEject(): msc.mediaPresent(false), then fat.begin()).
 *
 * Not included by CH32RVUsbDevice.h, and all inline here: a sketch without a
 * file system does not pull the FS library in, not even into the build. */
#pragma once

#include <CH32RVUsbDevice.h>
#include <FSBlockDevice.h>

class CH32RVUsbDeviceMscBlockStorage : public CH32RVUsbDeviceMscStorage {
public:
    typedef CH32RVCallback<void(void)> EjectCallback;

    explicit CH32RVUsbDeviceMscBlockStorage(fs::BlockDevice &device) : _device(device) {}

    /* Serve `msc` from the device (its begin() is called): its block count,
     * 512-byte blocks, and writable as the device is. */
    bool attach(CH32RVUsbDeviceMsc &msc)
    {
        if (!_device.begin() || !_device.blockCount()) {
            return false;
        }
        msc.setStorage(this);
        msc.isWritable(_device.writable());
        return msc.begin(_device.blockCount(), fs::BlockDevice::BLOCK_SIZE);
    }
    /* The PC ejected the medium: from deliverEvents(). */
    void onEject(EjectCallback cb) { _onEject = cb; }

    /* The MSC class asks for whole blocks from a block's start (its buffer is
     * a block, or a packet of them); anything else is refused. */
    int32_t read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) override
    {
        if (offset || !size || size % fs::BlockDevice::BLOCK_SIZE) {
            return -1;
        }
        return _device.readBlocks(lba, (uint8_t *)buffer, size / fs::BlockDevice::BLOCK_SIZE) ? (int32_t)size : -1;
    }
    int32_t write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) override
    {
        if (offset || !size || size % fs::BlockDevice::BLOCK_SIZE) {
            return -1;
        }
        return _device.writeBlocks(lba, buffer, size / fs::BlockDevice::BLOCK_SIZE) ? (int32_t)size : -1;
    }
    bool startStop(uint8_t powerCondition, bool start, bool loadEject) override
    {
        (void)powerCondition;
        (void)loadEject;
        if (!start) {
            _device.sync();
            _ejected = true;
        }
        return true;
    }
    size_t deliver() override
    {
        if (!_ejected) {
            return 0;
        }
        _ejected = false;
        if (!_onEject) {
            return 0;
        }
        _onEject();
        return 1;
    }

private:
    fs::BlockDevice &_device;
    EjectCallback _onEject = nullptr;
    volatile bool _ejected = false;
};

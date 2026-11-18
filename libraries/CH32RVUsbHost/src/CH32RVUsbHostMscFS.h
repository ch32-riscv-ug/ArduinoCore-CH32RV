/* A USB stick's FAT volume as an fs::FS: EspUsbHost's EspUsbHostMscFS, on
 * this core's FatFS (the FS library).
 *
 *   CH32RVUsbHost usb;
 *   CH32RVUsbHostMscFS stick;
 *   ...
 *   if (usb.mscReady() && !stick.mounted()) {
 *       stick.begin(usb);              // "/usb", the first mass storage device, LUN 0
 *   }
 *   File f = stick.open("/log.txt", FILE_APPEND);
 *
 * Header only, so a sketch that never includes it carries neither this nor
 * the FS library. The volume is FAT12/16/32 (exFAT where FatFS has it built
 * in) on 512-byte blocks; a drive with other blocks is refused. One LUN of one
 * device per object; FatFS mounts FF_VOLUMES at once (an SD card and a stick).
 * `basePath` and `maxFiles` are EspUsbHost's arguments, kept and not used:
 * paths are the volume's own ("/log.txt"), and the open files are
 * FS_MAX_OPEN_FILES (FSConfig.h). A stick pulled out makes every call false
 * until end() and begin() again. */
#pragma once

#include "CH32RVUsbHost.h"
#include <FatFS.h>
#include <string.h>

/* The blocks of one LUN through CH32RVUsbHost's msc calls (READ(10) /
 * WRITE(10)); sync() is SYNCHRONIZE CACHE unless skipped. */
class CH32RVUsbHostMscBlockDevice : public fs::BlockDevice {
public:
    void attach(CH32RVUsbHost &host, uint8_t address, uint8_t lun, uint32_t timeoutMs)
    {
        _host = &host;
        _address = address;
        _lun = lun;
        _timeoutMs = timeoutMs;
        _count = 0;
    }
    void setSkipSyncCache(bool skip) { _skipSync = skip; }
    bool skipSyncCache() const { return _skipSync; }
    /* The device's address once begin() found it (ANY resolved). */
    uint8_t address() const { return _address; }

    bool begin() override
    {
        if (!_host) {
            return false;
        }
        CH32RVUsbHostMscBlockDeviceInfo info;
        if (!_host->mscGetBlockDeviceInfo(info, _address)) {
            return false;
        }
        _address = info.address;          /* ANY: the device it found, from here on */
        if (!_host->mscSelectLun(_lun, _address) || !_host->mscWaitReady(_address, _timeoutMs)) {
            return false;
        }
        uint32_t count = 0, size = 0;
        if (!_host->mscCapacity(count, size, _address, _timeoutMs) || size != BLOCK_SIZE || count == 0) {
            return false;
        }
        _count = count;
        return true;
    }
    bool readBlocks(uint32_t lba, uint8_t *buffer, uint32_t count) override
    {
        return select() && _host->mscReadBlocks(lba, buffer, count, _address, _timeoutMs);
    }
    bool writeBlocks(uint32_t lba, const uint8_t *buffer, uint32_t count) override
    {
        return select() && _host->mscWriteBlocks(lba, buffer, count, _address, _timeoutMs);
    }
    bool sync() override
    {
        if (_skipSync) {
            return true;
        }
        /* A drive without a cache may reject the command: what it holds is on
         * the medium already, so that is not a failure. */
        if (!select()) {
            return false;
        }
        _host->mscSynchronizeCache(_address, _timeoutMs);
        return _host->mscReady(_address);
    }
    uint32_t blockCount() override { return _count; }

private:
    bool select() { return _host && _count && _host->mscSelectLun(_lun, _address); }

    CH32RVUsbHost *_host = nullptr;
    uint32_t _timeoutMs = 5000;
    uint32_t _count = 0;
    uint8_t _address = CH32RV_USB_HOST_ANY_ADDRESS;
    uint8_t _lun = 0;
    bool _skipSync = false;
};

class CH32RVUsbHostMscFS : public FatFS {
public:
    bool begin(CH32RVUsbHost &host, const char *basePath = "/usb", uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
               uint8_t lun = 0, uint8_t maxFiles = 4, uint32_t timeoutMs = 5000, bool skipSyncCache = false)
    {
        (void)maxFiles;
        end();
        strncpy(_basePath, basePath ? basePath : "", sizeof _basePath - 1);
        _basePath[sizeof _basePath - 1] = 0;
        _device.attach(host, address, lun, timeoutMs);
        _device.setSkipSyncCache(skipSyncCache);
        return FatFS::begin(_device);
    }
    using FatFS::begin;
    const char *basePath() const { return _basePath; }
    void setSkipSyncCache(bool skip) { _device.setSkipSyncCache(skip); }
    bool skipSyncCache() const { return _device.skipSyncCache(); }
    /* The blocks under the volume (address(), and format() through FatFS). */
    CH32RVUsbHostMscBlockDevice &device() { return _device; }

private:
    CH32RVUsbHostMscBlockDevice _device;
    char _basePath[16] = "/usb";
};

/* USB mass storage (EspUsbDeviceMsc): one logical unit whose blocks the
 * sketch supplies - from onRead()/onWrite(), or from a RAM disk below.
 * Included by CH32RVUsbDevice.h. */
#pragma once

typedef int32_t (*CH32RVUsbDeviceMscReadCallback)(uint32_t lba, uint32_t offset, void *buffer, uint32_t size);
typedef int32_t (*CH32RVUsbDeviceMscWriteCallback)(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size);
typedef bool (*CH32RVUsbDeviceMscStartStopCallback)(uint8_t powerCondition, bool start, bool loadEject);

/* What a disk object plugs into the MSC class. EspUsbDevice binds its disks
 * with capturing lambdas; callbacks here are plain function pointers, so the
 * disks implement this instead. */
class CH32RVUsbDeviceMscStorage {
public:
    virtual int32_t read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) = 0;
    virtual int32_t write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) = 0;
    virtual bool startStop(uint8_t powerCondition, bool start, bool loadEject)
    {
        (void)powerCondition;
        (void)start;
        (void)loadEject;
        return true;
    }
};

class CH32RVUsbDeviceMsc : public CH32RVUsbDeviceFunction {
public:
    explicit CH32RVUsbDeviceMsc(CH32RVUsbDevice &device = USBDevice);

    /* The medium's geometry; true once it and the read/write path are set. */
    bool begin(uint32_t blockCount, uint16_t blockSize);
    bool begin() { return _blockCount && _blockSize && (_storage || (_onRead && _onWrite)); }
    void end();

    /* SCSI INQUIRY strings: 8, 16 and 4 characters at most. */
    void vendorID(const char *value);
    void productID(const char *value);
    void productRevision(const char *value);
    /* No medium (false, the default) reads as "not ready" to the host. */
    void mediaPresent(bool value) { _mediaPresent = value; }
    void isWritable(bool value) { _writable = value; }
    void onRead(CH32RVUsbDeviceMscReadCallback cb) { _onRead = cb; }
    void onWrite(CH32RVUsbDeviceMscWriteCallback cb) { _onWrite = cb; }
    /* START STOP UNIT: start false + loadEject true is the host's eject. */
    void onStartStop(CH32RVUsbDeviceMscStartStopCallback cb) { _onStartStop = cb; }
    /* Serve the blocks from a disk object (the RAM disks do this in attach()). */
    void setStorage(CH32RVUsbDeviceMscStorage *storage) { _storage = storage; }

    /* For the USB stack's callbacks. */
    uint8_t maxLun() const { return 0; }
    void inquiry(uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) const;
    bool testUnitReady() const { return _mediaPresent; }
    void capacity(uint32_t *blockCount, uint16_t *blockSize) const;
    bool startStop(uint8_t powerCondition, bool start, bool loadEject);
    int32_t read10(uint32_t lba, uint32_t offset, void *buffer, uint32_t size);
    int32_t write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size);
    bool writable() const { return _writable; }

    /* The endpoint pair, set while the descriptors are laid out. */
    uint8_t _ep = 0;

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    void usbTransferred(uint8_t address, uint16_t length) override;
    void usbHaltCleared(uint8_t address) override;

private:
    /* Bulk-only transport: a command block, its data, a status. */
    enum class Stage : uint8_t { Command, DataIn, DataOut, Status, Stalled, Reset };
    void receiveCommand();
    void command();
    void sendData(uint16_t length);
    void readNext();
    void writeNext();
    void fail(uint8_t key, uint8_t asc, uint8_t ascq);
    void status();
    Stage _stage = Stage::Command;
    /* One block, or a command and its answer; a whole packet at any speed. */
    uint8_t _buffer[CH32RV_USB_MAX_PACKET > 512 ? CH32RV_USB_MAX_PACKET : 512] __attribute__((aligned(4)));
    uint8_t _csw[13];
    uint8_t _cmd[16];
    uint32_t _tag = 0, _expected = 0, _residue = 0, _lba = 0, _total = 0, _done = 0;
    uint16_t _packet = 64, _chunk = 0;
    bool _in = false, _failed = false;
    uint8_t _senseKey = 0, _asc = 0, _ascq = 0;
    char _vendor[9] = CH32RV_USB_MSC_VENDOR;
    char _product[17] = CH32RV_USB_MSC_PRODUCT;
    char _revision[5] = CH32RV_USB_MSC_REVISION;
    uint32_t _blockCount = 0;
    uint16_t _blockSize = 0;
    bool _mediaPresent = false;
    bool _writable = true;
    CH32RVUsbDeviceMscReadCallback _onRead = nullptr;
    CH32RVUsbDeviceMscWriteCallback _onWrite = nullptr;
    CH32RVUsbDeviceMscStartStopCallback _onStartStop = nullptr;
    CH32RVUsbDeviceMscStorage *_storage = nullptr;
};

/* Blocks in a RAM buffer the sketch owns (EspUsbDeviceMscRamDisk). */
class CH32RVUsbDeviceMscRamDisk : public CH32RVUsbDeviceMscStorage {
public:
    CH32RVUsbDeviceMscRamDisk(uint8_t *storage, uint32_t blockCount, uint16_t blockSize = 512);

    bool attach(CH32RVUsbDeviceMsc &msc);
    bool valid() const { return _storage && _blockCount && _blockSize; }
    uint8_t *data() { return _storage; }
    const uint8_t *data() const { return _storage; }
    uint32_t blockCount() const { return _blockCount; }
    uint16_t blockSize() const { return _blockSize; }
    size_t byteSize() const { return (size_t)_blockCount * _blockSize; }
    void clear(uint8_t value = 0);
    bool readBlock(uint32_t lba, void *buffer);
    bool writeBlock(uint32_t lba, const void *buffer);
    void writeByte(uint32_t lba, uint16_t offset, uint8_t value);

    int32_t read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) override;
    int32_t write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) override;

private:
    uint8_t *_storage;
    uint32_t _blockCount;
    uint16_t _blockSize;
};

/* A FAT12 volume in RAM that the sketch can put files into and read files
 * back from (EspUsbDeviceMscFatRamDisk). 8.3 names in the root directory
 * only; at least 16 blocks (8 KB). */
class CH32RVUsbDeviceMscFatRamDisk : public CH32RVUsbDeviceMscStorage {
public:
    typedef CH32RVCallback<void(void)> EjectCallback;

    CH32RVUsbDeviceMscFatRamDisk(uint8_t *storage, size_t size);

    bool format(const char *volumeLabel = CH32RV_USB_MSC_VOLUME_LABEL);
    bool attach(CH32RVUsbDeviceMsc &msc);
    void onEject(EjectCallback cb) { _onEject = cb; }
    bool addFile(const char *name, const uint8_t *data, size_t size);
    bool addTextFile(const char *name, const char *text);
    bool exists(const char *name) const;
    size_t fileSize(const char *name) const;
    size_t readFile(const char *name, uint8_t *buffer, size_t size) const;
    uint32_t blockCount() const { return _blockCount; }
    uint16_t blockSize() const { return 512; }
    size_t byteSize() const { return (size_t)_blockCount * 512; }

    int32_t read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) override;
    int32_t write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) override;
    bool startStop(uint8_t powerCondition, bool start, bool loadEject) override;

private:
    bool normalizeName(const char *name, char out[11]) const;
    bool findFile(const char name[11], uint32_t *firstCluster, uint32_t *size) const;
    bool allocateClusters(size_t size, uint16_t *firstCluster, size_t *clusterCount);
    void setFatEntry(uint16_t cluster, uint16_t value);
    uint16_t fatEntry(uint16_t cluster) const;
    uint8_t *clusterPtr(uint16_t cluster) const;
    uint8_t *rootEntry(uint16_t index) const;

    uint8_t *_storage;
    uint32_t _blockCount;
    uint16_t _sectorsPerFat = 0;
    uint16_t _rootEntryCount = 64;
    uint16_t _rootDirSectors = 0;
    uint16_t _dataStartSector = 0;
    uint16_t _clusterCount = 0;
    uint16_t _nextFreeCluster = 2;
    uint16_t _nextRootEntry = 0;
    CH32RVUsbDeviceMscRamDisk _blocks;
    EjectCallback _onEject = nullptr;
};

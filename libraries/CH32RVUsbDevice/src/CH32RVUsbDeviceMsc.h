/* USB mass storage (EspUsbDeviceMsc): logical units whose blocks the sketch
 * supplies - from onRead()/onWrite(), or from a RAM disk below. Each
 * CH32RVUsbDeviceMsc object is one logical unit, as each USBMSC is in
 * arduino-esp32: the first declared is LUN 0 and carries the interface, the
 * others (up to 8 in all) are LUN 1, 2 ... of the same interface.
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
    /* From deliverEvents(), through the MSC class: run the sketch's
     * notifications (onEject()); how many ran. */
    virtual size_t deliver() { return 0; }
};

class CH32RVUsbDeviceMsc : public CH32RVUsbDeviceFunction {
public:
    explicit CH32RVUsbDeviceMsc(CH32RVUsbDevice &device = USBDevice);

    /* The medium's geometry; true once it and the read path are set. Without
     * onWrite() (or a storage) the medium is read-only. */
    bool begin(uint32_t blockCount, uint16_t blockSize);
    bool begin() { return _blockCount && _blockSize && (_storage || _onRead); }
    void end();

    /* SCSI INQUIRY strings: 8, 16 and 4 characters at most. */
    void vendorID(const char *value);
    void productID(const char *value);
    void productRevision(const char *value);
    /* No medium (false, the default) reads as "not ready" to the host. A
     * medium appearing while the host has the device configured (false, then
     * true) is reported once, as UNIT ATTENTION "medium may have changed",
     * to the next command but INQUIRY and REQUEST SENSE - so the host reads
     * it afresh - even if the medium went again meanwhile. Setting the same
     * value again changes nothing; a bus reset forgets an untold change.
     * Taken out during a READ / WRITE, the rest of it fails (not ready,
     * medium not present) - the block being read or written goes on to its
     * end, nothing is undone, and the change still waits to be told. */
    void mediaPresent(bool value);
    void isWritable(bool value) { _writable = value; }
    /* Providers: called inside the USB service while the host waits. They
     * return the bytes done; anything but `size` is an I/O error to the host. */
    void onRead(CH32RVUsbDeviceMscReadCallback cb) { _onRead = cb; }
    void onWrite(CH32RVUsbDeviceMscWriteCallback cb) { _onWrite = cb; }
    /* START STOP UNIT: start false + loadEject true is the host's eject. */
    void onStartStop(CH32RVUsbDeviceMscStartStopCallback cb) { _onStartStop = cb; }
    /* Serve the blocks from a disk object (the RAM disks do this in attach()). */
    void setStorage(CH32RVUsbDeviceMscStorage *storage) { _storage = storage; deliverWith(deliverThis); }

    /* From inside onRead() / onWrite() (or a storage's read() / write())
     * that fails: the sense data the host gets for it (REQUEST SENSE) -
     * sense key, additional sense code and qualifier - instead of the
     * default, MEDIUM ERROR with unrecovered read error (03/11/00) or write
     * error (03/0C/00). Not ready, medium not present is 02/3A/00. Set when
     * the call succeeds, it is ignored. */
    void setSense(uint8_t senseKey, uint8_t asc, uint8_t ascq);
    /* This unit's logical unit number: the order the MSC objects were
     * declared in (0 for the first). */
    uint8_t lun() const { return _lun; }

    /* For the USB stack's callbacks. */
    uint8_t maxLun() const;          /* the units on the interface, less one */
    void inquiry(uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) const;
    bool testUnitReady() const { return _mediaPresent; }
    void capacity(uint32_t *blockCount, uint16_t *blockSize) const;
    bool startStop(uint8_t powerCondition, bool start, bool loadEject);
    int32_t read10(uint32_t lba, uint32_t offset, void *buffer, uint32_t size);
    int32_t write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size);
    bool writable() const { return _writable && (_storage || _onWrite); }

    /* The endpoint pair, set while the descriptors are laid out (LUN 0's). */
    uint8_t _ep = 0;

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    void usbTransferred(uint8_t address, uint16_t length) override;
    void usbHaltCleared(uint8_t address) override;
    /* The storage's notifications (eject); its own setters do not reach here. */
    static size_t deliverThis(CH32RVUsbDeviceFunction &f)
    {
        CH32RVUsbDeviceMsc &m = static_cast<CH32RVUsbDeviceMsc &>(f);
        return m._storage ? m._storage->deliver() : 0;
    }

private:
    /* The bulk-only transport (a command block, its data, a status) is the
     * interface's, kept once in CH32RVUsbDeviceMsc.cpp; these run it for
     * the unit a command addresses (`this`). */
    void receiveCommand();
    void command();
    void sendData(uint16_t length);
    void readNext();
    void writeNext();
    void fail(uint8_t key, uint8_t asc, uint8_t ascq);
    /* A read or write that failed: the sense setSense() gave, or `key/asc/ascq`. */
    void failIo(uint8_t key, uint8_t asc, uint8_t ascq);
    void status();
    uint8_t _senseKey = 0, _asc = 0, _ascq = 0;
    uint8_t _ioKey = 0, _ioAsc = 0, _ioAscq = 0;
    bool _ioSense = false;           /* setSense() during the provider's call */
    uint8_t _lun = 0;
    CH32RVUsbDeviceMsc *_nextLun = nullptr;
    char _vendor[9] = CH32RV_USB_MSC_VENDOR;
    char _product[17] = CH32RV_USB_MSC_PRODUCT;
    char _revision[5] = CH32RV_USB_MSC_REVISION;
    uint32_t _blockCount = 0;
    uint16_t _blockSize = 0;
    bool _mediaPresent = false;
    bool _attention = false;          /* a medium change the host has not been told of */
    /* Times the medium went: a READ / WRITE whose medium went since it began
     * fails the rest (02/3A/00), even if one is back - never onto another. */
    uint8_t _removals = 0, _commandRemovals = 0;
    bool mediumHeld() const { return _mediaPresent && _removals == _commandRemovals; }
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
    /* The host ejected the disk: from deliverEvents(). */
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
    size_t deliver() override;

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
    bool _ejected = false;
};

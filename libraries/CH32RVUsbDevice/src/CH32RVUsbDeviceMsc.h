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

/* A drive to copy a new firmware onto (EspUsbDeviceMscFirmwareDisk): a small
 * FAT12 volume whose data area past `storage` is the flash the core stages
 * an update in (ch32rv_update.h). A .uf2 file (any block order; its family
 * ID must be this series', uf2FamilyId()) or a raw .bin (in order) copied to
 * it is written block by block as it arrives, checked, and installed by a
 * restart. The image is never in RAM: `storage` holds the boot sector, the
 * FATs, the root directory and a scratch area for the host's own files
 * (8 KB is comfortable; at least 4 KB). The FAT is there too: where it
 * cannot describe twice the staging area (the UF2 file of a full image),
 * the drive is as large as it can (H417's 920 KB staging in 4 KB: 679 KB).
 *
 * When an update ends: a UF2 when every block number came; a .bin when the
 * bytes reach the length in its directory entry, or the drive is ejected.
 * A raw .bin must come in order (a jump back or a hole fails the update);
 * one whose first word is not this core's reset entry is ignored, as is
 * anything else that is neither (the host's metadata spilling over).
 * Failures leave the running sketch untouched and go to onError() with the
 * engine's error (CH32RV_UPDATE_*, or CH32RV_UPDATE_UF2_FAMILY / _UF2_LAYOUT
 * below). Callbacks run inside the USB service while the host waits. */
#define CH32RV_UPDATE_UF2_FAMILY 30u   /* a UF2 block for another family */
#define CH32RV_UPDATE_UF2_LAYOUT 31u   /* blocks that do not form one image at 0 */
#define CH32RV_UPDATE_RAW_ORDER  32u   /* a raw image not written in order */
#define CH32RV_UPDATE_NO_MEMORY  33u

class CH32RVUsbDeviceMscFirmwareDisk : public CH32RVUsbDeviceMscStorage {
public:
    typedef CH32RVCallback<void(void)> EjectCallback;
    typedef CH32RVCallback<void(size_t)> ProgressCallback;
    typedef CH32RVCallback<bool(void)> CompleteCallback;
    typedef CH32RVCallback<void(uint8_t)> ErrorCallback;

    CH32RVUsbDeviceMscFirmwareDisk(uint8_t *storage, size_t size);

    /* Lay the volume out over the staging area. False when there is none
     * (ch32rv_flash_staging_size() is 0), or `storage` is too small. */
    bool begin(const char *volumeLabel = CH32RV_USB_MSC_VOLUME_LABEL);
    /* Serve `msc` from it (media present, writable). Before USBDevice.begin(). */
    bool attach(CH32RVUsbDeviceMsc &msc);
    /* The family ID a .uf2 needs to be taken here (uf2conv.py --family). */
    static uint32_t uf2FamilyId();
    /* A file shown before anything is copied (in the scratch area). Between
     * begin() and attach(). */
    bool addTextFile(const char *name, const char *text);

    void onProgress(ProgressCallback cb) { _onProgress = cb; }
    /* The image passed its checks; false refuses it (nothing is installed). */
    void onComplete(CompleteCallback cb) { _onComplete = cb; }
    void onError(ErrorCallback cb) { _onError = cb; }
    /* The host ejected the drive: from deliverEvents(). */
    void onEject(EjectCallback cb) { _onEject = cb; }
    /* true (the default): restart into an accepted image, half a second
     * later (the host's last command answered first). */
    void restartWhenComplete(bool enable) { _restart = enable; }

    uint32_t blockCount() const { return _blockCount; }
    uint16_t blockSize() const { return 512; }
    /* Sectors in `storage`; from this one on the volume is the staging flash. */
    uint32_t ramSectorCount() const { return _ramSectors; }
    size_t written() const;
    bool updating() const { return _format != 0; }
    bool updatingUf2() const { return _format == 2; }
    /* The last failure (CH32RV_UPDATE_*, 0 none). */
    uint8_t lastError() const { return _lastError; }

    int32_t read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) override;
    int32_t write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) override;
    bool startStop(uint8_t powerCondition, bool start, bool loadEject) override;
    size_t deliver() override;

private:
    bool region(uint32_t at, const uint8_t *data, uint32_t size);
    bool raw(uint32_t at, const uint8_t *data, uint32_t size);
    bool uf2(const uint8_t *block);
    void fail(uint8_t error);
    void commit();
    void checkLength();
    uint8_t *rootEntry(uint16_t index) const;
    void setFatEntry(uint16_t cluster, uint16_t value);
    void release();

    uint8_t *_storage;
    size_t _size;
    CH32RVUsbDeviceMsc *_msc = nullptr;
    uint16_t _sectorsPerCluster = 1;
    uint16_t _sectorsPerFat = 0;
    uint16_t _rootEntryCount = 16;
    uint32_t _dataStart = 0;      /* LBA of cluster 2 */
    uint32_t _scratchSectors = 0;
    uint32_t _ramSectors = 0;
    uint32_t _blockCount = 0;
    uint16_t _nextCluster = 2;
    uint16_t _nextEntry = 0;
    uint32_t _imageStart = 0;     /* the raw image's first sector, relative to the region */
    uint32_t _uf2Total = 0, _uf2Seen = 0;
    uint8_t *_uf2Bitmap = nullptr;
    uint8_t _format = 0;          /* 0 none, 1 raw, 2 UF2 */
    uint8_t _lastError = 0;
    bool _formatted = false;
    bool _restart = true;
    volatile bool _ejected = false;
    ProgressCallback _onProgress = nullptr;
    CompleteCallback _onComplete = nullptr;
    ErrorCallback _onError = nullptr;
    EjectCallback _onEject = nullptr;
};

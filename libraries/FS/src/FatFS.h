/* A FAT volume (FAT12/16/32, exFAT) on a BlockDevice, through FatFs. One
 * object per mounted volume; FF_VOLUMES of them can be mounted at once
 * (1 on the small series, 2 otherwise: an SD card and a USB stick).
 *
 *   fs::RamBlockDevice disk(buffer, 128);
 *   FatFS fat;
 *   fat.begin(disk, true);           // format it when it holds no volume
 *   File f = fat.open("/hello.txt", FILE_WRITE);
 *   f.print("hello");
 *   f.close();
 */
#pragma once

#include "FS.h"
#include "FSBlockDevice.h"
#include "fatfs/ff.h"

namespace fs {

/* The volume behind a FatFS (the FS's FSImpl). */
class FatFSImpl : public FSImpl {
public:
    FileImpl *open(const char *path, const char *mode, bool create) override;
    bool exists(const char *path) override;
    bool remove(const char *path) override;
    bool rename(const char *from, const char *to) override;
    bool mkdir(const char *path) override;
    bool rmdir(const char *path) override;

    /* The FatFs path of `path` on this volume ("1:/a/b"); false if too long. */
    bool fullPath(char *out, size_t size, const char *path) const;
    void closeFiles();

    FATFS fatfs;
    BlockDevice *device = nullptr;
    int8_t drive = -1;
};

enum class FatType : uint8_t { None, Fat12, Fat16, Fat32, ExFat };

class FatFS : public FS {
public:
    FatFS();
    FatFS(const FatFS &) = delete;
    FatFS &operator=(const FatFS &) = delete;

    /* Mount the volume on `device` (its begin() is called first). With
     * formatIfEmpty, a medium that holds no FAT volume is formatted (where
     * format() is built in). false: no medium, no volume, or every volume
     * slot is taken. */
    bool begin(BlockDevice &device, bool formatIfEmpty = false)
    {
        /* inline, so that begin(device) links no format() */
        return formatIfEmpty ? beginOrFormat(device) : beginMount(device);
    }
    /* Unmount: open files are closed, what they wrote is flushed. */
    void end();
    /* Make a new empty volume on the device begin() was given (or `device`):
     * FAT12/16/32 by size, exFAT from 4 GB where exFAT is built in. Unmounts
     * first. false where format is not built in (the small series). */
    bool format();
    bool format(BlockDevice &device);

    bool mounted() const;
    FatType fatType() const;
    uint64_t totalBytes();
    uint64_t usedBytes();
    uint64_t freeBytes();

private:
    bool beginMount(BlockDevice &device);
    bool beginOrFormat(BlockDevice &device);
    /* Take a drive number for `device`; false when all FF_VOLUMES are in use. */
    bool claim(BlockDevice &device);
    void release();
    FatFSImpl _volume;
};

} // namespace fs

using fs::FatFS;
using fs::FatType;

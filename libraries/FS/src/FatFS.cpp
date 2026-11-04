/* FatFS: FatFs (fatfs/, vendored) under the FS API, and FatFs's disk glue.
 *
 * Open files come from one pool of FS_MAX_OPEN_FILES slots shared by the
 * volumes. "w" does not cut the old file at open: it writes over it and
 * cuts the tail at flush() or close(). A small file rewritten in place (a
 * configuration under a sector) is then the old one or the new one after a
 * power cut, never empty (docs/filesystem.ja.md: 40 of 404 cuts lost the
 * file the other way, none this way). What a File reports while the cut is
 * pending - size(), what read() reaches - is the new file's. */
#include "FatFS.h"

#include "fatfs/diskio.h"

#include <string.h>
#include <time.h>

namespace fs {

static FatFSImpl *volumes[FF_VOLUMES];

/* ------------------------------------------------------------ files */

class FatFile : public FileImpl {
public:
    size_t write(const uint8_t *buf, size_t size) override;
    size_t read(uint8_t *buf, size_t size) override;
    void flush() override;
    bool seek(uint32_t pos, SeekMode mode) override;
    size_t position() const override { return isDir ? 0 : (size_t)f_tell(&u.fil); }
    size_t size() const override;
    void close() override;
    time_t getLastWrite() override;
    const char *path() const override { return pathBuf; }
    const char *name() const override;
    bool isDirectory() const override { return isDir; }
    FileImpl *openNextFile(const char *mode) override;
    bool nextFileName(char *out, size_t size, bool *dir) override;
    bool seekDir(long position) override;
    void rewindDirectory() override;

    bool openFile(FatFSImpl *v, const char *path, const char *full, BYTE mode, bool truncateLater);
    bool openDir(FatFSImpl *v, const char *path, const char *full);
    void end() { invalidate(); }
    /* The next directory entry's name in `fno`; false at the end. */
    bool next(FILINFO *fno);
    bool childPath(char *out, size_t size, const char *child) const;

    union {
        FIL fil;
        DIR dir;
    } u;
    FatFSImpl *volume = nullptr;
    FSIZE_t written = 0;       /* the end of what "w" wrote, while the cut is pending */
    bool used = false, isDir = false, pendingCut = false;
    char pathBuf[FS_PATH_MAX];

private:
    bool cut();
};

static FatFile files[FS_MAX_OPEN_FILES];

static FatFile *takeSlot()
{
    for (FatFile &f : files) {
        if (!f.used) {
            f.used = true;
            f.isDir = false;
            f.pendingCut = false;
            f.written = 0;
            memset(&f.u, 0, sizeof f.u);
            return &f;
        }
    }
    return nullptr;
}

static bool copyPath(char *out, size_t size, const char *path)
{
    const size_t n = strlen(path);
    if (n + 1 > size) {
        return false;
    }
    memcpy(out, path, n + 1);
    return true;
}

bool FatFile::openFile(FatFSImpl *v, const char *path, const char *full, BYTE mode, bool truncateLater)
{
    if (!copyPath(pathBuf, sizeof pathBuf, path) || f_open(&u.fil, full, mode) != FR_OK) {
        return false;
    }
    volume = v;
    pendingCut = truncateLater && f_size(&u.fil) > 0;
    written = 0;
    return true;
}

bool FatFile::openDir(FatFSImpl *v, const char *path, const char *full)
{
    if (!copyPath(pathBuf, sizeof pathBuf, path) || f_opendir(&u.dir, full) != FR_OK) {
        return false;
    }
    volume = v;
    isDir = true;
    return true;
}

size_t FatFile::write(const uint8_t *buf, size_t size)
{
#if FF_FS_READONLY
    (void)buf;
    (void)size;
    return 0;
#else
    UINT done = 0;
    if (!used || isDir || f_write(&u.fil, buf, (UINT)size, &done) != FR_OK) {
        return 0;
    }
    if (pendingCut && f_tell(&u.fil) > written) {
        written = f_tell(&u.fil);
    }
    return done;
#endif
}

size_t FatFile::read(uint8_t *buf, size_t size)
{
    if (!used || isDir) {
        return 0;
    }
    if (pendingCut) {
        const FSIZE_t pos = f_tell(&u.fil);
        if (pos >= written) {
            return 0;
        }
        if (size > written - pos) {
            size = (size_t)(written - pos);
        }
    }
    UINT done = 0;
    return f_read(&u.fil, buf, (UINT)size, &done) == FR_OK ? done : 0;
}

/* Cut the old tail after what "w" wrote. */
bool FatFile::cut()
{
#if FF_FS_READONLY
    return true;
#else
    if (!pendingCut) {
        return true;
    }
    const FSIZE_t pos = f_tell(&u.fil);
    bool ok = f_lseek(&u.fil, written) == FR_OK && f_truncate(&u.fil) == FR_OK;
    ok = f_lseek(&u.fil, pos) == FR_OK && ok;
    pendingCut = false;
    return ok;
#endif
}

void FatFile::flush()
{
#if !FF_FS_READONLY
    if (used && !isDir) {
        cut();
        f_sync(&u.fil);
    }
#endif
}

size_t FatFile::size() const
{
    if (!used || isDir) {
        return 0;
    }
    return (size_t)(pendingCut ? written : f_size(&u.fil));
}

bool FatFile::seek(uint32_t pos, SeekMode mode)
{
    if (!used || isDir) {
        return false;
    }
    FSIZE_t base = 0;
    if (mode == SeekCur) {
        base = f_tell(&u.fil);
    } else if (mode == SeekEnd) {
        base = (FSIZE_t)size();
    }
    const FSIZE_t target = base + pos;
    /* FatFs grows a file seeked past its end when it is open for writing;
     * POSIX (and ESP32) leave the size alone until something is written. */
    if (target > (FSIZE_t)size()) {
        return false;
    }
    return f_lseek(&u.fil, target) == FR_OK;
}

void FatFile::close()
{
    if (!used) {
        return;
    }
    if (isDir) {
        f_closedir(&u.dir);
    } else {
#if !FF_FS_READONLY
        cut();
#endif
        f_close(&u.fil);
    }
    used = false;
    volume = nullptr;
}

const char *FatFile::name() const
{
    const char *slash = strrchr(pathBuf, '/');
    return slash && slash[1] ? slash + 1 : pathBuf;
}

/* FAT keeps local time in 2-second steps. */
static time_t fatTime(WORD date, WORD time)
{
#if FF_FS_NORTC
    (void)date;
    (void)time;
    return 0;
#else
    struct tm t = {};
    t.tm_year = (date >> 9) + 80;
    t.tm_mon = ((date >> 5) & 15) - 1;
    t.tm_mday = date & 31;
    t.tm_hour = time >> 11;
    t.tm_min = (time >> 5) & 63;
    t.tm_sec = (time & 31) * 2;
    t.tm_isdst = -1;
    return mktime(&t);
#endif
}

time_t FatFile::getLastWrite()
{
    if (!used || !volume) {
        return 0;
    }
    if (!isDir) {
        flush();
    }
    char full[FS_PATH_MAX + 2];
    FILINFO fno;
    if (!volume->fullPath(full, sizeof full, pathBuf) || f_stat(full, &fno) != FR_OK) {
        return 0;
    }
    return fatTime(fno.fdate, fno.ftime);
}

bool FatFile::next(FILINFO *fno)
{
    return used && isDir && f_readdir(&u.dir, fno) == FR_OK && fno->fname[0];
}

bool FatFile::childPath(char *out, size_t size, const char *child) const
{
    const size_t a = strlen(pathBuf), b = strlen(child);
    const bool root = a > 0 && pathBuf[a - 1] == '/';
    if (a + (root ? 0 : 1) + b + 1 > size) {
        return false;
    }
    memcpy(out, pathBuf, a);
    size_t n = a;
    if (!root) {
        out[n++] = '/';
    }
    memcpy(out + n, child, b + 1);
    return true;
}

FileImpl *FatFile::openNextFile(const char *mode)
{
    FILINFO fno;
    char child[FS_PATH_MAX];
    while (next(&fno)) {
        if (childPath(child, sizeof child, fno.fname)) {
            return volume->open(child, mode, false);
        }
        /* a name too long for FS_PATH_MAX is skipped */
    }
    return nullptr;
}

bool FatFile::nextFileName(char *out, size_t size, bool *dir)
{
    FILINFO fno;
    while (next(&fno)) {
        if (childPath(out, size, fno.fname)) {
            *dir = (fno.fattrib & AM_DIR) != 0;
            return true;
        }
    }
    return false;
}

void FatFile::rewindDirectory()
{
    if (used && isDir) {
        f_readdir(&u.dir, nullptr);
    }
}

bool FatFile::seekDir(long position)
{
    if (!used || !isDir || position < 0) {
        return false;
    }
    rewindDirectory();
    FILINFO fno;
    for (long i = 0; i < position; i++) {
        if (!next(&fno)) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------ volume */

bool FatFSImpl::fullPath(char *out, size_t size, const char *path) const
{
    if (drive < 0 || !path || path[0] != '/') {
        return false;
    }
#if FF_VOLUMES > 1
    if (size < 3) {
        return false;
    }
    out[0] = (char)('0' + drive);
    out[1] = ':';
    return copyPath(out + 2, size - 2, path);
#else
    return copyPath(out, size, path);
#endif
}

void FatFSImpl::closeFiles()
{
    for (FatFile &f : files) {
        if (f.used && f.volume == this) {
            f.close();
            f.end();
        }
    }
}

#if !FF_FS_READONLY
/* Make the directories above `full` ("0:/a/b/c.txt" makes /a and /a/b). */
static bool makeParents(char *full)
{
    char *p = strchr(full, '/');
    for (p = p ? strchr(p + 1, '/') : nullptr; p; p = strchr(p + 1, '/')) {
        *p = 0;
        const FRESULT r = f_mkdir(full);
        *p = '/';
        if (r != FR_OK && r != FR_EXIST) {
            return false;
        }
    }
    return true;
}
#endif

FileImpl *FatFSImpl::open(const char *path, const char *mode, bool create)
{
    char full[FS_PATH_MAX + 2];
    if (!fullPath(full, sizeof full, path)) {
        return nullptr;
    }
    BYTE m = 0;
    bool late = false;
    switch (mode[0]) {
    case 'r': m = FA_READ; break;
    case 'w': m = FA_WRITE | FA_OPEN_ALWAYS; late = true; break;
    case 'a': m = FA_WRITE | FA_OPEN_APPEND; break;
    default: return nullptr;
    }
    if (mode[1] == '+' || (mode[1] && mode[2] == '+')) {
        m |= FA_READ | FA_WRITE;
    }
#if FF_FS_READONLY
    if (m & FA_WRITE) {
        return nullptr;
    }
#else
    if (create && (m & FA_WRITE) && !makeParents(full)) {
        return nullptr;
    }
#endif
    FatFile *f = takeSlot();
    if (!f) {
        return nullptr;
    }
    if (f->openFile(this, path, full, m, late)) {
        return f;
    }
    if (m == FA_READ && f->openDir(this, path, full)) {
        return f;
    }
    f->used = false;
    return nullptr;
}

bool FatFSImpl::exists(const char *path)
{
    char full[FS_PATH_MAX + 2];
    if (!fullPath(full, sizeof full, path)) {
        return false;
    }
    if (path[1] == 0) {
        return true;   /* the root, which f_stat does not report */
    }
    FILINFO fno;
    return f_stat(full, &fno) == FR_OK;
}

#if FF_FS_READONLY
bool FatFSImpl::remove(const char *) { return false; }
bool FatFSImpl::rename(const char *, const char *) { return false; }
bool FatFSImpl::mkdir(const char *) { return false; }
bool FatFSImpl::rmdir(const char *) { return false; }
#else
/* remove() takes files, rmdir() empty directories (FatFs's f_unlink does both). */
static bool unlinkKind(FatFSImpl *v, const char *path, bool dir)
{
    char full[FS_PATH_MAX + 2];
    FILINFO fno;
    if (!v->fullPath(full, sizeof full, path) || f_stat(full, &fno) != FR_OK) {
        return false;
    }
    if (((fno.fattrib & AM_DIR) != 0) != dir) {
        return false;
    }
    return f_unlink(full) == FR_OK;
}

bool FatFSImpl::remove(const char *path) { return unlinkKind(this, path, false); }

bool FatFSImpl::rmdir(const char *path) { return unlinkKind(this, path, true); }

/* As FatFs: false when `to` exists. */
bool FatFSImpl::rename(const char *from, const char *to)
{
    char a[FS_PATH_MAX + 2], b[FS_PATH_MAX + 2];
    if (!fullPath(a, sizeof a, from) || !fullPath(b, sizeof b, to)) {
        return false;
    }
#if FF_VOLUMES > 1
    /* f_rename takes the new name without the drive. */
    return f_rename(a, b + 2) == FR_OK;
#else
    return f_rename(a, b) == FR_OK;
#endif
}

bool FatFSImpl::mkdir(const char *path)
{
    char full[FS_PATH_MAX + 2];
    return fullPath(full, sizeof full, path) && f_mkdir(full) == FR_OK;
}
#endif

/* ------------------------------------------------------------ FatFS */

FatFS::FatFS() : FS(_volume) {}

bool FatFS::mounted() const { return _volume.drive >= 0; }

bool FatFS::claim(BlockDevice &device)
{
    for (int8_t i = 0; i < FF_VOLUMES; i++) {
        if (!volumes[i]) {
            volumes[i] = &_volume;
            _volume.device = &device;
            _volume.drive = i;
            return true;
        }
    }
    return false;
}

void FatFS::release()
{
    if (_volume.drive >= 0) {
        volumes[_volume.drive] = nullptr;
    }
    _volume.drive = -1;
}

static FRESULT mountDrive(fs::FatFSImpl &v)
{
    char root[3] = {(char)('0' + v.drive), ':', 0};
    return f_mount(&v.fatfs, root, 1);
}

bool FatFS::beginMount(BlockDevice &device)
{
    end();
    if (!device.begin() || !claim(device)) {
        return false;
    }
    if (mountDrive(_volume) != FR_OK) {
        end();
        return false;
    }
    return true;
}

bool FatFS::beginOrFormat(BlockDevice &device)
{
    end();
    if (!device.begin() || !claim(device)) {
        return false;
    }
    FRESULT r = mountDrive(_volume);
    if (r == FR_NO_FILESYSTEM && format(device)) {
        r = claim(device) ? mountDrive(_volume) : FR_NOT_ENABLED;
    }
    if (r != FR_OK) {
        end();
        return false;
    }
    return true;
}

void FatFS::end()
{
    if (_volume.drive < 0) {
        return;
    }
    _volume.closeFiles();
    char root[3] = {(char)('0' + _volume.drive), ':', 0};
    f_unmount(root);
    if (_volume.device) {
        _volume.device->sync();
    }
    release();
}

bool FatFS::format() { return _volume.device && format(*_volume.device); }

bool FatFS::format(BlockDevice &device)
{
#if FF_USE_MKFS
    end();
    if (!device.begin() || !device.writable() || !claim(device)) {
        release();
        return false;
    }
    char root[3] = {(char)('0' + _volume.drive), ':', 0};
    /* Up to 32 MB a single volume from block 0 (a RAM disk, a small card);
     * above, an MBR partition as on an SD card or a USB stick. */
    MKFS_PARM opt = {FM_ANY, 0, 0, 0, 0};
    const uint32_t blocks = device.blockCount();
    if (blocks < 65536) {
        opt.fmt |= FM_SFD;
    }
    /* FAT12/16's fixed root directory: 512 entries (32 blocks) leave a small
     * RAM disk no room; a long name takes several entries. */
    if (blocks < 256) {
        opt.n_root = 64;
    } else if (blocks < 8192) {
        opt.n_root = 256;
    }
    static BYTE work[FF_MAX_SS];
    const bool ok = f_mkfs(root, &opt, work, sizeof work) == FR_OK;
    device.sync();
    release();
    return ok;
#else
    (void)device;
    return false;
#endif
}

FatType FatFS::fatType() const
{
    if (_volume.drive < 0) {
        return FatType::None;
    }
    switch (_volume.fatfs.fs_type) {
    case FS_FAT12: return FatType::Fat12;
    case FS_FAT16: return FatType::Fat16;
    case FS_FAT32: return FatType::Fat32;
#if FF_FS_EXFAT
    case FS_EXFAT: return FatType::ExFat;
#endif
    }
    return FatType::None;
}

uint64_t FatFS::totalBytes()
{
    if (_volume.drive < 0) {
        return 0;
    }
    return (uint64_t)(_volume.fatfs.n_fatent - 2) * _volume.fatfs.csize * BlockDevice::BLOCK_SIZE;
}

uint64_t FatFS::freeBytes()
{
#if FF_FS_READONLY
    return 0;
#else
    if (_volume.drive < 0) {
        return 0;
    }
    char root[3] = {(char)('0' + _volume.drive), ':', 0};
    DWORD clusters = 0;
    FATFS *fs = nullptr;
    if (f_getfree(root, &clusters, &fs) != FR_OK) {
        return 0;
    }
    return (uint64_t)clusters * _volume.fatfs.csize * BlockDevice::BLOCK_SIZE;
#endif
}

uint64_t FatFS::usedBytes()
{
    const uint64_t total = totalBytes(), free = freeBytes();
    return total > free ? total - free : 0;
}

} // namespace fs

/* ------------------------------------------------------------ FatFs's disk glue */

using fs::volumes;

extern "C" {

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv >= FF_VOLUMES || !volumes[pdrv] || !volumes[pdrv]->device) {
        return STA_NOINIT;
    }
    return volumes[pdrv]->device->writable() ? 0 : STA_PROTECT;
}

DSTATUS disk_initialize(BYTE pdrv) { return disk_status(pdrv); }

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (disk_status(pdrv) & STA_NOINIT) {
        return RES_NOTRDY;
    }
    return volumes[pdrv]->device->readBlocks((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}

#if !FF_FS_READONLY
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    const DSTATUS s = disk_status(pdrv);
    if (s & STA_NOINIT) {
        return RES_NOTRDY;
    }
    if (s & STA_PROTECT) {
        return RES_WRPRT;
    }
    return volumes[pdrv]->device->writeBlocks((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (disk_status(pdrv) & STA_NOINIT) {
        return RES_NOTRDY;
    }
    fs::BlockDevice *d = volumes[pdrv]->device;
    switch (cmd) {
    case CTRL_SYNC: return d->sync() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = d->blockCount(); return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = fs::BlockDevice::BLOCK_SIZE; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = d->eraseBlocks(); return RES_OK;
    }
    return RES_PARERR;
}

#if !FF_FS_READONLY && !FF_FS_NORTC
/* Now from time() as local time; a clock never set (before 2020) gives the
 * fixed date FatFs would use without a clock. */
DWORD get_fattime(void)
{
    const time_t now = time(nullptr);
    struct tm t;
    if (now < 1577836800 || !localtime_r(&now, &t)) {
        return ((DWORD)(FF_NORTC_YEAR - 1980) << 25) | ((DWORD)FF_NORTC_MON << 21) | ((DWORD)FF_NORTC_MDAY << 16);
    }
    return ((DWORD)(t.tm_year - 80) << 25) | ((DWORD)(t.tm_mon + 1) << 21) | ((DWORD)t.tm_mday << 16) |
           ((DWORD)t.tm_hour << 11) | ((DWORD)t.tm_min << 5) | ((DWORD)(t.tm_sec / 2));
}
#endif

} // extern "C"

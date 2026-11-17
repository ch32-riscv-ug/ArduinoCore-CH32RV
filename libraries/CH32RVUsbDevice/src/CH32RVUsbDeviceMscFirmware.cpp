/* The firmware drive (CH32RVUsbDeviceMsc.h, CH32RVUsbDeviceMscFirmwareDisk):
 * EspUsbDeviceMscFirmwareDisk's volume and rules over the core's update
 * engine (ch32rv_update.h).
 *
 * The volume: boot sector, two FATs, a root directory of 16 entries and a
 * scratch area in `storage`; after them, clusters that map onto the staging
 * flash. The cluster is the smallest of 1 to 128 sectors that lets FAT12
 * (4084 clusters at most) cover the scratch area and all of the staging
 * area: the staging writes go through the engine, which erases by itself, so
 * a cluster need not be an erase unit (ESP's starts at 4 KB for that). */
#include "internal/CH32RVUsbDeviceInternal.h"

#include <ch32rv_flash.h>
#include <ch32rv_update.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 512u
#define MAX_CLUSTERS 4084u
#define UF2_MAGIC0 0x0A324655u                /* "UF2\n" */
#define UF2_MAGIC1 0x9E5D5157u
#define UF2_MAGIC_END 0x0AB16F30u
#define UF2_NOT_MAIN_FLASH 0x00000001u
#define UF2_FAMILY_PRESENT 0x00002000u
#define UF2_MAX_PAYLOAD 476u
#define UF2_MAX_BLOCKS 65536u
#define FORMAT_RAW 1
#define FORMAT_UF2 2

static inline uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static bool is_uf2(const uint8_t *b)
{
    return le32(b) == UF2_MAGIC0 && le32(b + 4) == UF2_MAGIC1 && le32(b + SECTOR - 4) == UF2_MAGIC_END;
}

/* "README  TXT" from "README.TXT"; false for what 8.3 cannot hold. */
static bool name83(const char *name, char out[11])
{
    if (!name) {
        return false;
    }
    memset(out, ' ', 11);
    size_t i = 0;
    for (; name[i] && name[i] != '.'; i++) {
        if (i >= 8) {
            return false;
        }
        out[i] = (char)toupper((unsigned char)name[i]);
    }
    if (!i) {
        return false;
    }
    if (name[i] == '.') {
        const char *ext = name + i + 1;
        for (size_t j = 0; ext[j]; j++) {
            if (j >= 3) {
                return false;
            }
            out[8 + j] = (char)toupper((unsigned char)ext[j]);
        }
    }
    return true;
}

CH32RVUsbDeviceMscFirmwareDisk::CH32RVUsbDeviceMscFirmwareDisk(uint8_t *storage, size_t size)
    : _storage(storage), _size(size)
{
}

uint32_t CH32RVUsbDeviceMscFirmwareDisk::uf2FamilyId()
{
#ifdef CH32RV_UF2_FAMILY_ID
    return CH32RV_UF2_FAMILY_ID;
#else
    return 0;
#endif
}

bool CH32RVUsbDeviceMscFirmwareDisk::begin(const char *volumeLabel)
{
    _formatted = false;
    const uint32_t capacity = ch32rv_flash_staging_size();
    if (!_storage || _size < 8u * SECTOR || !capacity) {
        return false;
    }
    const uint32_t rootSectors = (_rootEntryCount * 32u + SECTOR - 1u) / SECTOR;
    const uint32_t ramAvailable = (uint32_t)(_size / SECTOR);
    bool laidOut = false;
    for (uint16_t spc = 1; spc <= 128 && !laidOut; spc = (uint16_t)(spc * 2)) {
        const uint32_t clusterBytes = spc * SECTOR;
        const uint32_t firmwareClusters = (capacity + clusterBytes - 1u) / clusterBytes;
        /* The FAT's size depends on the clusters, which depend on the RAM
         * the FAT leaves for the scratch area: a few passes settle it. */
        uint16_t fatSectors = 1;
        uint32_t scratchClusters = 0;
        bool settled = false;
        for (int pass = 0; pass < 8; pass++) {
            const uint32_t meta = 1u + 2u * fatSectors + rootSectors;
            if (ramAvailable <= meta) {
                break;
            }
            scratchClusters = (ramAvailable - meta) / spc;
            if (!scratchClusters) {
                break;
            }
            const uint32_t fatBytes = ((scratchClusters + firmwareClusters + 2u) * 3u + 1u) / 2u;
            const uint16_t need = (uint16_t)((fatBytes + SECTOR - 1u) / SECTOR);
            if (need == fatSectors) {
                settled = true;
                break;
            }
            fatSectors = need;
        }
        if (!settled || scratchClusters + firmwareClusters > MAX_CLUSTERS) {
            continue;
        }
        _sectorsPerCluster = spc;
        _sectorsPerFat = fatSectors;
        _dataStart = 1u + 2u * fatSectors + rootSectors;
        _scratchSectors = scratchClusters * spc;
        _ramSectors = _dataStart + _scratchSectors;
        _blockCount = _ramSectors + firmwareClusters * spc;
        laidOut = true;
    }
    if (!laidOut) {
        return false;
    }
    memset(_storage, 0, _ramSectors * SECTOR);
    _nextCluster = 2;
    _nextEntry = 0;
    release();

    uint8_t *boot = _storage;
    boot[0] = 0xEB;
    boot[1] = 0x3C;
    boot[2] = 0x90;
    memcpy(boot + 3, "MSDOS5.0", 8);
    put16(boot + 11, SECTOR);
    boot[13] = (uint8_t)_sectorsPerCluster;
    put16(boot + 14, 1);                             /* reserved sectors */
    boot[16] = 2;                                    /* FATs */
    put16(boot + 17, _rootEntryCount);
    if (_blockCount < 0x10000u) {
        put16(boot + 19, (uint16_t)_blockCount);
    } else {
        put32(boot + 32, _blockCount);
    }
    boot[21] = 0xF8;
    put16(boot + 22, _sectorsPerFat);
    put16(boot + 24, 1);
    put16(boot + 26, 1);
    boot[36] = 0x80;
    boot[38] = 0x29;
    put32(boot + 39, 0x43483332u);
    const bool labelled = volumeLabel && volumeLabel[0];
    memcpy(boot + 43, labelled ? "           " : "NO NAME    ", 11);
    for (uint8_t i = 0; labelled && i < 11 && volumeLabel[i]; i++) {
        boot[43 + i] = (uint8_t)toupper((unsigned char)volumeLabel[i]);
    }
    memcpy(boot + 54, "FAT12   ", 8);
    boot[510] = 0x55;
    boot[511] = 0xAA;
    uint8_t *fat = _storage + SECTOR;
    fat[0] = 0xF8;
    fat[1] = 0xFF;
    fat[2] = 0xFF;
    memcpy(_storage + (1u + _sectorsPerFat) * SECTOR, fat, _sectorsPerFat * SECTOR);
    _formatted = true;
    return true;
}

uint8_t *CH32RVUsbDeviceMscFirmwareDisk::rootEntry(uint16_t index) const
{
    return _storage + (1u + 2u * _sectorsPerFat) * SECTOR + index * 32u;
}

void CH32RVUsbDeviceMscFirmwareDisk::setFatEntry(uint16_t cluster, uint16_t value)
{
    const uint32_t i = cluster * 3u / 2u;
    uint8_t *fat = _storage + SECTOR;
    if (i + 1u >= (uint32_t)_sectorsPerFat * SECTOR) {
        return;
    }
    if (cluster & 1u) {
        fat[i] = (uint8_t)((fat[i] & 0x0F) | (value & 0x0F) << 4);
        fat[i + 1] = (uint8_t)(value >> 4);
    } else {
        fat[i] = (uint8_t)value;
        fat[i + 1] = (uint8_t)((fat[i + 1] & 0xF0) | ((value >> 8) & 0x0F));
    }
    memcpy(_storage + (1u + _sectorsPerFat) * SECTOR, fat, _sectorsPerFat * SECTOR);
}

bool CH32RVUsbDeviceMscFirmwareDisk::addTextFile(const char *name, const char *text)
{
    char n[11];
    if (!_formatted || !text || !name83(name, n)) {
        return false;
    }
    const size_t length = strlen(text);
    const uint32_t clusterBytes = _sectorsPerCluster * SECTOR;
    const uint32_t need = (uint32_t)((length + clusterBytes - 1u) / clusterBytes);
    if (!need || _nextEntry >= _rootEntryCount || _nextCluster - 2u + need > _scratchSectors / _sectorsPerCluster) {
        return false;
    }
    const uint16_t first = _nextCluster;
    for (uint32_t i = 0; i < need; i++) {
        const uint16_t c = (uint16_t)(first + i);
        setFatEntry(c, i + 1u == need ? 0xFFF : (uint16_t)(c + 1u));
    }
    _nextCluster = (uint16_t)(_nextCluster + need);
    uint8_t *e = rootEntry(_nextEntry++);
    memcpy(e, n, 11);
    e[11] = 0x20;
    put16(e + 26, first);
    put32(e + 28, (uint32_t)length);
    memcpy(_storage + (_dataStart + (first - 2u) * _sectorsPerCluster) * SECTOR, text, length);
    return true;
}

bool CH32RVUsbDeviceMscFirmwareDisk::attach(CH32RVUsbDeviceMsc &msc)
{
    if (!_formatted) {
        return false;
    }
    _msc = &msc;
    msc.setStorage(this);
    msc.isWritable(true);
    msc.mediaPresent(true);
    return msc.begin(_blockCount, SECTOR);
}

size_t CH32RVUsbDeviceMscFirmwareDisk::written() const
{
    return ch32rv_update_progress();
}

void CH32RVUsbDeviceMscFirmwareDisk::release()
{
    free(_uf2Bitmap);
    _uf2Bitmap = nullptr;
    _uf2Total = _uf2Seen = 0;
    _imageStart = 0;
    _format = 0;
}

void CH32RVUsbDeviceMscFirmwareDisk::fail(uint8_t error)
{
    ch32rv_update_abort();
    release();
    _lastError = error;
    if (_onError) {
        _onError(error);
    }
}

void CH32RVUsbDeviceMscFirmwareDisk::commit()
{
    if (!_format) {
        return;
    }
    release();
    if (!ch32rv_update_end(true)) {
        _lastError = ch32rv_update_error();
        if (_onError) {
            _onError(_lastError);
        }
        return;
    }
    if (_onComplete && !_onComplete()) {
        ch32rv_update_cancel_pending();
        return;
    }
    if (_restart && _msc) {
        /* The host's last command is answered first. */
        ch32rv_usb_install_after(_msc->device(), 500);
    }
}

/* Past the RAM sectors the volume reads as the staging flash: what was
 * copied reads back, the rest as erased flash. */
int32_t CH32RVUsbDeviceMscFirmwareDisk::read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    if (!_formatted || !buffer || lba >= _blockCount) {
        return -1;
    }
    const uint32_t at = lba * SECTOR + offset;
    const uint32_t ram = _ramSectors * SECTOR;
    uint8_t *out = (uint8_t *)buffer;
    uint32_t done = 0;
    if (at < ram) {
        done = ram - at < size ? ram - at : size;
        memcpy(out, _storage + at, done);
    }
    if (done < size) {
        const uint32_t from = at + done - ram;
        const uint32_t capacity = ch32rv_flash_staging_size();
        uint32_t n = size - done;
        if (from < capacity) {
            const uint32_t m = capacity - from < n ? capacity - from : n;
            memcpy(out + done, (const void *)(uintptr_t)(0x08000000u + ch32rv_flash_staging_offset() + from), m);
            done += m;
            n -= m;
        }
        memset(out + done, 0, n);
    }
    return (int32_t)size;
}

/* A raw image: in order from where it started. */
bool CH32RVUsbDeviceMscFirmwareDisk::raw(uint32_t at, const uint8_t *data, uint32_t size)
{
    if (!_format) {
        if (!ch32rv_update_begin(CH32RV_UPDATE_SIZE_UNKNOWN, false)) {
            fail(ch32rv_update_error());
            return false;
        }
        _format = FORMAT_RAW;
        _imageStart = at;
    }
    if (at < _imageStart || at - _imageStart != ch32rv_update_progress()) {
        fail(CH32RV_UPDATE_RAW_ORDER);
        return false;
    }
    if (!ch32rv_update_write(data, size)) {
        fail(ch32rv_update_error());
        return false;
    }
    if (_onProgress) {
        _onProgress(ch32rv_update_progress());
    }
    return true;
}

bool CH32RVUsbDeviceMscFirmwareDisk::uf2(const uint8_t *b)
{
    const uint32_t flags = le32(b + 8), target = le32(b + 12), payload = le32(b + 16);
    const uint32_t number = le32(b + 20), total = le32(b + 24), family = le32(b + 28);
    if (!payload || payload > UF2_MAX_PAYLOAD || !total || total > UF2_MAX_BLOCKS || number >= total) {
        fail(CH32RV_UPDATE_UF2_LAYOUT);
        return false;
    }
    if ((flags & UF2_FAMILY_PRESENT) && family != uf2FamilyId()) {
        /* An image for another chip: what a family ID is for. */
        fail(CH32RV_UPDATE_UF2_FAMILY);
        return false;
    }
    /* Addresses as the code flash's own (0x08000000) or its alias at 0. */
    const uint32_t offset = target >= 0x08000000u ? target - 0x08000000u : target;
    if (!_format) {
        /* Any block says where the image starts: it must be the flash's start. */
        if (offset != number * payload) {
            fail(CH32RV_UPDATE_UF2_LAYOUT);
            return false;
        }
        _uf2Bitmap = (uint8_t *)calloc((total + 7u) / 8u, 1);
        if (!_uf2Bitmap) {
            fail(CH32RV_UPDATE_NO_MEMORY);
            return false;
        }
        if (!ch32rv_update_begin(total * payload, true)) {
            fail(ch32rv_update_error());
            return false;
        }
        _format = FORMAT_UF2;
        _uf2Total = total;
        _uf2Seen = 0;
    } else if (total != _uf2Total) {
        fail(CH32RV_UPDATE_UF2_LAYOUT);                /* two files mixed in one region */
        return false;
    }
    const uint8_t bit = (uint8_t)(1u << (number & 7u));
    if (_uf2Bitmap[number / 8u] & bit) {
        return true;                                   /* had this one: counting it again would end early */
    }
    if (!(flags & UF2_NOT_MAIN_FLASH) && !ch32rv_update_write_at(offset, b + 32, payload)) {
        fail(ch32rv_update_error());
        return false;
    }
    _uf2Bitmap[number / 8u] |= bit;
    if (_onProgress) {
        _onProgress(ch32rv_update_progress());
    }
    if (++_uf2Seen >= _uf2Total) {
        commit();                                      /* exact: every block number came */
    }
    return true;
}

/* Bytes landing past the RAM sectors, `at` from the region's start. */
bool CH32RVUsbDeviceMscFirmwareDisk::region(uint32_t at, const uint8_t *data, uint32_t size)
{
    if (ch32rv_update_pending()) {
        /* An image is accepted and waits for the restart: what still comes
         * (the rest of a UF2 file's last packet, the host's metadata) must
         * not start another update over it. */
        return true;
    }
    if (!_format) {
        /* Decided once, from the first bytes to land here; anything that is
         * neither is a stray file or the host's metadata, left alone. */
        if (size >= SECTOR && !(at % SECTOR) && is_uf2(data)) {
            /* UF2: its first block starts the update below */
        } else if (size >= 4 && ch32rv_flash_image_plausible(le32(data), ch32rv_flash_staging_size())) {
            return raw(at, data, size);
        } else {
            return true;
        }
    }
    if (_format == FORMAT_RAW) {
        return raw(at, data, size);
    }
    if (at % SECTOR) {
        return true;
    }
    for (; size >= SECTOR; data += SECTOR, size -= SECTOR) {
        if (is_uf2(data) && !uf2(data)) {
            return false;
        }
    }
    return true;
}

/* The length of the file being copied: the root entry whose first cluster
 * lies in the firmware region (0 until the host writes it). */
void CH32RVUsbDeviceMscFirmwareDisk::checkLength()
{
    if (_format != FORMAT_RAW) {
        return;
    }
    const uint32_t scratchClusters = _scratchSectors / _sectorsPerCluster;
    for (uint16_t i = 0; i < _rootEntryCount; i++) {
        const uint8_t *e = rootEntry(i);
        if (e[0] == 0x00) {
            break;
        }
        if (e[0] == 0xE5 || (e[11] & 0x18)) {
            continue;                                  /* deleted, a label or a directory */
        }
        const uint16_t first = le16(e + 26);
        const uint32_t size = le32(e + 28);
        if (first < 2 || first - 2u < scratchClusters || !size) {
            continue;
        }
        if (ch32rv_update_progress() >= size) {
            commit();
        }
        return;
    }
}

int32_t CH32RVUsbDeviceMscFirmwareDisk::write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    if (!_formatted || !buffer || lba >= _blockCount) {
        return -1;
    }
    const uint32_t at = lba * SECTOR + offset;
    const uint32_t ram = _ramSectors * SECTOR;
    uint32_t done = 0;
    if (at < ram) {
        done = ram - at < size ? ram - at : size;
        memcpy(_storage + at, buffer, done);
    }
    if (done < size && !region(at + done - ram, buffer + done, size - done)) {
        return -1;
    }
    checkLength();                                     /* the entry may come before, during or after the data */
    return (int32_t)size;
}

bool CH32RVUsbDeviceMscFirmwareDisk::startStop(uint8_t powerCondition, bool start, bool loadEject)
{
    (void)powerCondition;
    (void)loadEject;
    if (!start) {
        if (_format == FORMAT_RAW) {
            commit();                                  /* the eject ends a raw image */
        }
        _ejected = true;
    }
    return true;
}

size_t CH32RVUsbDeviceMscFirmwareDisk::deliver()
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

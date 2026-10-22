/* RAM disks for CH32RVUsbDeviceMsc: raw blocks, and a FAT12 volume
 * (EspUsbDevice's, with the same layout). */
#include "CH32RVUsbDevice.h"

#include <ctype.h>
#include <string.h>

/* ---------------------------------------------------------- raw blocks */
CH32RVUsbDeviceMscRamDisk::CH32RVUsbDeviceMscRamDisk(uint8_t *storage, uint32_t blockCount, uint16_t blockSize)
    : _storage(storage), _blockCount(blockCount), _blockSize(blockSize)
{
}

bool CH32RVUsbDeviceMscRamDisk::attach(CH32RVUsbDeviceMsc &msc)
{
    if (!valid()) {
        return false;
    }
    msc.setStorage(this);
    return msc.begin(_blockCount, _blockSize);
}

void CH32RVUsbDeviceMscRamDisk::clear(uint8_t value)
{
    if (valid()) {
        memset(_storage, value, byteSize());
    }
}

bool CH32RVUsbDeviceMscRamDisk::readBlock(uint32_t lba, void *buffer)
{
    return read(lba, 0, buffer, _blockSize) == _blockSize;
}

bool CH32RVUsbDeviceMscRamDisk::writeBlock(uint32_t lba, const void *buffer)
{
    return buffer && write(lba, 0, (uint8_t *)buffer, _blockSize) == _blockSize;
}

void CH32RVUsbDeviceMscRamDisk::writeByte(uint32_t lba, uint16_t offset, uint8_t value)
{
    if (valid() && lba < _blockCount && offset < _blockSize) {
        _storage[(size_t)lba * _blockSize + offset] = value;
    }
}

int32_t CH32RVUsbDeviceMscRamDisk::read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    if (!valid() || !buffer || lba >= _blockCount || offset >= _blockSize) {
        return -1;
    }
    const size_t start = (size_t)lba * _blockSize + offset;
    if (start + size < start || start + size > byteSize()) {
        return -1;
    }
    memcpy(buffer, _storage + start, size);
    return (int32_t)size;
}

int32_t CH32RVUsbDeviceMscRamDisk::write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    if (!valid() || !buffer || lba >= _blockCount || offset >= _blockSize) {
        return -1;
    }
    const size_t start = (size_t)lba * _blockSize + offset;
    if (start + size < start || start + size > byteSize()) {
        return -1;
    }
    memcpy(_storage + start, buffer, size);
    return (int32_t)size;
}

/* ---------------------------------------------------------- FAT12 */
static void put16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32le(uint8_t *p, uint32_t v)
{
    put16le(p, (uint16_t)v);
    put16le(p + 2, (uint16_t)(v >> 16));
}

CH32RVUsbDeviceMscFatRamDisk::CH32RVUsbDeviceMscFatRamDisk(uint8_t *storage, size_t size)
    : _storage(storage), _blockCount((uint32_t)(size / 512)), _blocks(storage, (uint32_t)(size / 512), 512)
{
}

/* One boot sector, two FATs, a 64-entry root directory, then one sector per
 * cluster. */
bool CH32RVUsbDeviceMscFatRamDisk::format(const char *volumeLabel)
{
    if (!_storage || _blockCount < 16) {
        return false;
    }
    memset(_storage, 0, _blockCount * 512);
    _rootDirSectors = (uint16_t)((_rootEntryCount * 32 + 511) / 512);
    uint16_t spf = 1;
    for (uint8_t i = 0; i < 8; i++) {
        const uint32_t clusters = _blockCount - 1 - _rootDirSectors - 2u * spf;
        const uint16_t need = (uint16_t)((((clusters + 2) * 3 + 1) / 2 + 511) / 512);
        if (need == spf) {
            break;
        }
        spf = need;
    }
    _sectorsPerFat = spf;
    _dataStartSector = (uint16_t)(1 + 2 * _sectorsPerFat + _rootDirSectors);
    if (_dataStartSector >= _blockCount) {
        return false;
    }
    _clusterCount = (uint16_t)(_blockCount - _dataStartSector);
    _nextFreeCluster = 2;
    _nextRootEntry = 0;

    uint8_t *b = _storage;
    b[0] = 0xEB;
    b[1] = 0x3C;
    b[2] = 0x90;
    memcpy(b + 3, "MSDOS5.0", 8);
    put16le(b + 11, 512);
    b[13] = 1;
    put16le(b + 14, 1);
    b[16] = 2;
    put16le(b + 17, _rootEntryCount);
    put16le(b + 19, (uint16_t)_blockCount);
    b[21] = 0xF8;
    put16le(b + 22, _sectorsPerFat);
    put16le(b + 24, 1);
    put16le(b + 26, 1);
    b[36] = 0x80;
    b[38] = 0x29;
    put32le(b + 39, 0x43483252);   /* volume serial "CH2R" */
    memset(b + 43, ' ', 11);
    for (uint8_t i = 0; volumeLabel && i < 11 && volumeLabel[i]; i++) {
        b[43 + i] = (uint8_t)toupper((unsigned char)volumeLabel[i]);
    }
    memcpy(b + 54, "FAT12   ", 8);
    b[510] = 0x55;
    b[511] = 0xAA;
    _storage[512] = 0xF8;
    _storage[513] = 0xFF;
    _storage[514] = 0xFF;
    memcpy(_storage + (1 + _sectorsPerFat) * 512, _storage + 512, _sectorsPerFat * 512u);
    return true;
}

bool CH32RVUsbDeviceMscFatRamDisk::attach(CH32RVUsbDeviceMsc &msc)
{
    if (!_blocks.valid()) {
        return false;
    }
    msc.setStorage(this);
    return msc.begin(_blockCount, 512);
}

int32_t CH32RVUsbDeviceMscFatRamDisk::read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    return _blocks.read(lba, offset, buffer, size);
}

int32_t CH32RVUsbDeviceMscFatRamDisk::write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    return _blocks.write(lba, offset, buffer, size);
}

bool CH32RVUsbDeviceMscFatRamDisk::startStop(uint8_t powerCondition, bool start, bool loadEject)
{
    (void)powerCondition;
    (void)loadEject;
    if (!start) {
        _ejected = true;
    }
    return true;
}

size_t CH32RVUsbDeviceMscFatRamDisk::deliver()
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

bool CH32RVUsbDeviceMscFatRamDisk::addFile(const char *name, const uint8_t *data, size_t size)
{
    char fat[11];
    if (!normalizeName(name, fat) || !data || exists(name) || _nextRootEntry >= _rootEntryCount) {
        return false;
    }
    uint16_t first = 0;
    size_t clusters = 0;
    if (!allocateClusters(size, &first, &clusters)) {
        return false;
    }
    size_t left = size;
    uint16_t cluster = first;
    for (size_t i = 0; i < clusters; i++) {
        const size_t chunk = left > 512 ? 512 : left;
        memcpy(clusterPtr(cluster), data, chunk);
        data += chunk;
        left -= chunk;
        cluster = fatEntry(cluster);
    }
    uint8_t *e = rootEntry(_nextRootEntry++);
    memcpy(e, fat, 11);
    e[11] = 0x20;
    put16le(e + 26, first);
    put32le(e + 28, (uint32_t)size);
    return true;
}

bool CH32RVUsbDeviceMscFatRamDisk::addTextFile(const char *name, const char *text)
{
    return text && addFile(name, (const uint8_t *)text, strlen(text));
}

bool CH32RVUsbDeviceMscFatRamDisk::exists(const char *name) const
{
    char fat[11];
    return normalizeName(name, fat) && findFile(fat, nullptr, nullptr);
}

size_t CH32RVUsbDeviceMscFatRamDisk::fileSize(const char *name) const
{
    char fat[11];
    uint32_t size = 0;
    return normalizeName(name, fat) && findFile(fat, nullptr, &size) ? size : 0;
}

/* Follows the cluster chain, so a file the host wrote reads back too. */
size_t CH32RVUsbDeviceMscFatRamDisk::readFile(const char *name, uint8_t *buffer, size_t size) const
{
    char fat[11];
    uint32_t first = 0, stored = 0;
    if (!buffer || !normalizeName(name, fat) || !findFile(fat, &first, &stored)) {
        return 0;
    }
    size_t copied = 0, left = stored < size ? stored : size;
    uint16_t cluster = (uint16_t)first;
    while (left && cluster >= 2 && cluster < 0xFF8 && cluster < _clusterCount + 2u) {
        const size_t chunk = left > 512 ? 512 : left;
        memcpy(buffer + copied, clusterPtr(cluster), chunk);
        copied += chunk;
        left -= chunk;
        cluster = fatEntry(cluster);
    }
    return copied;
}

bool CH32RVUsbDeviceMscFatRamDisk::normalizeName(const char *name, char out[11]) const
{
    if (!name || !name[0]) {
        return false;
    }
    memset(out, ' ', 11);
    uint8_t base = 0, ext = 8;
    bool in_ext = false;
    for (const char *p = name; *p; p++) {
        if (*p == '.') {
            in_ext = true;
            continue;
        }
        const uint8_t at = in_ext ? ext++ : base++;
        if ((!in_ext && at >= 8) || at >= 11) {
            return false;
        }
        const unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '_' || c == '-' || c == '~')) {
            return false;
        }
        out[at] = (char)toupper(c);
    }
    return base > 0;
}

bool CH32RVUsbDeviceMscFatRamDisk::findFile(const char name[11], uint32_t *firstCluster, uint32_t *size) const
{
    if (!_sectorsPerFat) {
        return false;          /* not formatted */
    }
    for (uint16_t i = 0; i < _rootEntryCount; i++) {
        const uint8_t *e = rootEntry(i);
        if (e[0] == 0x00) {
            return false;
        }
        /* deleted, or a volume label / long-name part */
        if (e[0] == 0xE5 || (e[11] & 0x08)) {
            continue;
        }
        if (memcmp(e, name, 11) == 0) {
            if (firstCluster) {
                *firstCluster = e[26] | ((uint32_t)e[27] << 8);
            }
            if (size) {
                *size = e[28] | ((uint32_t)e[29] << 8) | ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
            }
            return true;
        }
    }
    return false;
}

bool CH32RVUsbDeviceMscFatRamDisk::allocateClusters(size_t size, uint16_t *firstCluster, size_t *clusterCount)
{
    const size_t need = size == 0 ? 1 : (size + 511) / 512;
    if (_nextFreeCluster + need > (size_t)_clusterCount + 2) {
        return false;
    }
    *firstCluster = _nextFreeCluster;
    *clusterCount = need;
    for (size_t i = 0; i < need; i++) {
        const uint16_t c = (uint16_t)(_nextFreeCluster + i);
        setFatEntry(c, i + 1 == need ? 0xFFF : (uint16_t)(c + 1));
    }
    _nextFreeCluster = (uint16_t)(_nextFreeCluster + need);
    return true;
}

void CH32RVUsbDeviceMscFatRamDisk::setFatEntry(uint16_t cluster, uint16_t value)
{
    value &= 0x0FFF;
    for (uint8_t fat = 0; fat < 2; fat++) {
        uint8_t *base = _storage + (1 + fat * _sectorsPerFat) * 512u;
        const uint32_t o = cluster + cluster / 2;
        if (cluster & 1) {
            base[o] = (uint8_t)((base[o] & 0x0F) | ((value << 4) & 0xF0));
            base[o + 1] = (uint8_t)(value >> 4);
        } else {
            base[o] = (uint8_t)value;
            base[o + 1] = (uint8_t)((base[o + 1] & 0xF0) | ((value >> 8) & 0x0F));
        }
    }
}

uint16_t CH32RVUsbDeviceMscFatRamDisk::fatEntry(uint16_t cluster) const
{
    const uint8_t *base = _storage + 512;
    const uint32_t o = cluster + cluster / 2;
    if (cluster & 1) {
        return (uint16_t)(((base[o] >> 4) | (base[o + 1] << 4)) & 0x0FFF);
    }
    return (uint16_t)((base[o] | ((base[o + 1] & 0x0F) << 8)) & 0x0FFF);
}

uint8_t *CH32RVUsbDeviceMscFatRamDisk::clusterPtr(uint16_t cluster) const
{
    return _storage + (size_t)(_dataStartSector + cluster - 2) * 512;
}

uint8_t *CH32RVUsbDeviceMscFatRamDisk::rootEntry(uint16_t index) const
{
    return _storage + (size_t)(1 + 2 * _sectorsPerFat) * 512 + (size_t)index * 32;
}

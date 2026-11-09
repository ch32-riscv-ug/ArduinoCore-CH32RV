/* A small FAT12 / FAT16 reader and writer for peer_pico.ino: the first line of
 * a file in the root directory, and a file written there (8.3 names only). An
 * implementation of its own, apart from the DUT's FatFS, so that what one
 * writes the other reads. SdFat (arduino-pico's) leaves FAT12 out unless built
 * with FAT12_SUPPORT, and the DUT's RAM disks are FAT12.
 *
 * The volume starts at sector 0 or at the first partition of an MBR. The FAT
 * is held in RAM while a file is written (up to 64 sectors), then written to
 * every copy. */
#pragma once
#include <stdint.h>
#include <string.h>

class PeerFat
{
public:
  typedef bool (*ReadFn)(uint32_t lba, uint8_t *buffer, uint16_t count);
  typedef bool (*WriteFn)(uint32_t lba, const uint8_t *buffer, uint16_t count);

  PeerFat(ReadFn read, WriteFn write) : _read(read), _write(write) {}

  bool mount()
  {
    _ok = false;
    uint32_t start = 0;
    if (!_read(0, _sector, 1) || _sector[510] != 0x55 || _sector[511] != 0xAA) return false;
    if (!bpb())
    {
      start = le32(_sector + 0x1C6);                   /* the first partition */
      if (!start || !_read(start, _sector, 1) || !bpb()) return false;
    }
    const uint16_t reserved = le16(_sector + 14);
    const uint8_t fats = _sector[16];
    const uint16_t rootEntries = le16(_sector + 17);
    const uint32_t total = le16(_sector + 19) ? le16(_sector + 19) : le32(_sector + 32);
    _spc = _sector[13];
    _fatSectors = le16(_sector + 22);
    if (!_fatSectors || !rootEntries) return false;   /* FAT32 is not handled */
    _fats = fats;
    _fatStart = start + reserved;
    _rootStart = _fatStart + (uint32_t)fats * _fatSectors;
    _rootSectors = (uint16_t)((rootEntries * 32u + 511u) / 512u);
    _dataStart = _rootStart + _rootSectors;
    _clusters = (total - (_dataStart - start)) / _spc;
    _fat12 = _clusters < 4085;
    if (_clusters >= 65525) return false;
    _ok = true;
    return true;
  }

  /* The first line of `name` (8.3, upper case) without its line end; false when absent. */
  bool firstLine(const char *name, char *out, size_t cap)
  {
    uint32_t sector;
    uint16_t index;
    out[0] = '\0';
    if (!_ok || !find(name, &sector, &index)) return false;
    const uint8_t *e = _sector + 32 * index;
    uint32_t size = le32(e + 28);
    uint16_t cluster = le16(e + 26);
    size_t n = 0;
    while (size && cluster >= 2 && n + 1 < cap)
    {
      for (uint8_t s = 0; s < _spc && size && n + 1 < cap; s++)
      {
        if (!_read(clusterSector(cluster) + s, _sector, 1)) return false;
        for (uint16_t i = 0; i < 512 && size && n + 1 < cap; i++, size--)
        {
          const char c = (char)_sector[i];
          if (c == '\r' || c == '\n')
          {
            out[n] = '\0';
            return true;
          }
          out[n++] = c;
        }
      }
      if (!loadFat()) return false;
      cluster = next(cluster);
      if (cluster >= (_fat12 ? 0xFF8 : 0xFFF8)) break;
    }
    out[n] = '\0';
    return true;
  }

  /* `name` (8.3, upper case) with `data`: replaced when it exists, else in a free root entry. */
  bool writeFile(const char *name, const uint8_t *data, uint32_t length)
  {
    uint32_t sector;
    uint16_t index;
    if (!_ok || !loadFat()) return false;
    if (find(name, &sector, &index))
    {
      for (uint16_t c = le16(_sector + 32 * index + 26), guard = 0; c >= 2 && c < (_fat12 ? 0xFF8 : 0xFFF8) &&
                                                                    guard < 4096; guard++)
      {
        const uint16_t n = next(c);
        set(c, 0);
        c = n;
      }
    }
    else if (!findFree(&sector, &index))
      return false;
    const uint32_t bytes = 512u * _spc;
    const uint32_t need = (length + bytes - 1) / bytes;
    uint16_t first = 0, last = 0;
    for (uint32_t k = 0, c = 2; k < need; k++)
    {
      while (c < _clusters + 2 && next((uint16_t)c)) c++;
      if (c >= _clusters + 2) return false;              /* full */
      set((uint16_t)c, _fat12 ? 0xFFF : 0xFFFF);
      if (last) set(last, (uint16_t)c);
      else first = (uint16_t)c;
      last = (uint16_t)c;
      for (uint8_t s = 0; s < _spc; s++)
      {
        const uint32_t at = k * bytes + 512u * s;
        memset(_data, 0, 512);
        if (at < length) memcpy(_data, data + at, length - at < 512 ? length - at : 512);
        if (!_write(clusterSector((uint16_t)c) + s, _data, 1)) return false;
      }
    }
    for (uint8_t f = 0; f < _fats; f++)
      if (!_write(_fatStart + (uint32_t)f * _fatSectors, _fat, _fatSectors)) return false;
    if (!_read(sector, _sector, 1)) return false;
    uint8_t *e = _sector + 32 * index;
    memset(e, 0, 32);
    shortName(name, e);
    e[11] = 0x20;                                        /* archive */
    e[24] = 0x6A;                                        /* 2026-11-10 */
    e[25] = 0x5D;
    e[18] = e[24];
    e[19] = e[25];
    e[26] = (uint8_t)first;
    e[27] = (uint8_t)(first >> 8);
    e[28] = (uint8_t)length;
    e[29] = (uint8_t)(length >> 8);
    e[30] = (uint8_t)(length >> 16);
    e[31] = (uint8_t)(length >> 24);
    return _write(sector, _sector, 1);
  }

private:
  ReadFn _read;
  WriteFn _write;
  bool _ok = false, _fat12 = false;
  uint8_t _spc = 1, _fats = 2;
  uint16_t _fatSectors = 0, _rootSectors = 0;
  uint32_t _fatStart = 0, _rootStart = 0, _dataStart = 0, _clusters = 0;
  uint8_t _sector[512], _data[512];
  uint8_t _fat[64 * 512];

  static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
  static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

  /* _sector holds a FAT boot sector: 512-byte sectors and a jump. */
  bool bpb() const
  {
    return (_sector[0] == 0xEB || _sector[0] == 0xE9) && le16(_sector + 11) == 512 && _sector[13] && _sector[16];
  }

  uint32_t clusterSector(uint16_t c) const { return _dataStart + (uint32_t)(c - 2) * _spc; }

  bool loadFat()
  {
    if (_fatSectors > sizeof _fat / 512) return false;
    return _read(_fatStart, _fat, _fatSectors);
  }

  uint16_t next(uint16_t c) const
  {
    if (!_fat12) return le16(_fat + 2u * c);
    const uint16_t v = le16(_fat + c + c / 2);
    return (c & 1) ? v >> 4 : v & 0xFFF;
  }

  void set(uint16_t c, uint16_t value)
  {
    if (!_fat12)
    {
      _fat[2u * c] = (uint8_t)value;
      _fat[2u * c + 1] = (uint8_t)(value >> 8);
      return;
    }
    uint8_t *p = _fat + c + c / 2;
    if (c & 1)
    {
      p[0] = (uint8_t)((p[0] & 0x0F) | (value << 4));
      p[1] = (uint8_t)(value >> 4);
    }
    else
    {
      p[0] = (uint8_t)value;
      p[1] = (uint8_t)((p[1] & 0xF0) | ((value >> 8) & 0x0F));
    }
  }

  static void shortName(const char *name, uint8_t *out)
  {
    memset(out, ' ', 11);
    const char *dot = strchr(name, '.');
    for (size_t i = 0; name + i != dot && name[i] && i < 8; i++) out[i] = (uint8_t)name[i];
    if (dot)
      for (size_t i = 0; dot[1 + i] && i < 3; i++) out[8 + i] = (uint8_t)dot[1 + i];
  }

  /* The root entry of `name`: its sector (left in _sector) and index there. */
  bool find(const char *name, uint32_t *sector, uint16_t *index)
  {
    uint8_t want[11];
    shortName(name, want);
    for (uint16_t s = 0; s < _rootSectors; s++)
    {
      if (!_read(_rootStart + s, _sector, 1)) return false;
      for (uint16_t i = 0; i < 16; i++)
      {
        const uint8_t *e = _sector + 32 * i;
        if (e[0] == 0x00) return false;
        if (e[0] != 0xE5 && (e[11] & 0x18) == 0 && e[11] != 0x0F && !memcmp(e, want, 11))
        {
          *sector = _rootStart + s;
          *index = i;
          return true;
        }
      }
    }
    return false;
  }

  bool findFree(uint32_t *sector, uint16_t *index)
  {
    for (uint16_t s = 0; s < _rootSectors; s++)
    {
      if (!_read(_rootStart + s, _sector, 1)) return false;
      for (uint16_t i = 0; i < 16; i++)
        if (_sector[32 * i] == 0x00 || _sector[32 * i] == 0xE5)
        {
          *sector = _rootStart + s;
          *index = i;
          return true;
        }
    }
    return false;
  }
};

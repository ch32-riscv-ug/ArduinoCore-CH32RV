/* The file API shared by the file systems (ESP32's FS / File, without the
 * shared_ptr and the VFS): a File is a handle on one open file or directory
 * of an FS. Copies share it, and the last copy to go closes it. Paths are
 * absolute ("/logs/day1.txt") within the FS they are opened on.
 *
 * What a file system implements is FSImpl and FileImpl; FatFS (FatFS.h) is
 * the one there is, on any BlockDevice (FSBlockDevice.h). */
#pragma once

#include <Arduino.h>
#include <internal/ch32rv_printf.h>
#include <time.h>

#include "FSConfig.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {

enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

class File;

/* One open file or directory. A file system keeps a fixed pool of them.
 * File counts its copies in _refs and calls close() when the last goes;
 * _gen changes at every close, so a copy left over from an explicit close()
 * never reaches the slot's next file. */
class FileImpl {
public:
    virtual size_t write(const uint8_t *buf, size_t size) = 0;
    virtual size_t read(uint8_t *buf, size_t size) = 0;
    virtual void flush() = 0;
    virtual bool seek(uint32_t pos, SeekMode mode) = 0;
    virtual size_t position() const = 0;
    virtual size_t size() const = 0;
    /* Ends the handle and gives the slot back; nothing is valid after it. */
    virtual void close() = 0;
    virtual time_t getLastWrite() = 0;
    virtual const char *path() const = 0;
    virtual const char *name() const = 0;
    virtual bool isDirectory() const = 0;
    /* The directory's next entry, opened with `mode`; nullptr at the end. */
    virtual FileImpl *openNextFile(const char *mode) = 0;
    /* The next entry's path into `out` (empty at the end), without opening it. */
    virtual bool nextFileName(char *out, size_t size, bool *isDir) = 0;
    virtual bool seekDir(long position) = 0;
    virtual void rewindDirectory() = 0;

protected:
    ~FileImpl() = default;
    /* The file system ends the handle itself (unmount): every File copy of
     * it is dead from here. */
    void invalidate()
    {
        _refs = 0;
        _gen++;
    }

private:
    friend class File;
    uint8_t _refs = 0;
    uint8_t _gen = 0;
};

class FSImpl {
public:
    /* nullptr when it cannot be opened. "r" also opens a directory. */
    virtual FileImpl *open(const char *path, const char *mode, bool create) = 0;
    virtual bool exists(const char *path) = 0;
    virtual bool remove(const char *path) = 0;
    virtual bool rename(const char *from, const char *to) = 0;
    virtual bool mkdir(const char *path) = 0;
    virtual bool rmdir(const char *path) = 0;

protected:
    ~FSImpl() = default;
};

class File : public Stream {
public:
    File(FileImpl *p = nullptr);
    File(const File &other);
    File &operator=(const File &other);
    ~File();

    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;
    /* arduino-esp32's Print::printf (the core's ch32rv_printf.h). */
    CH32RV_PRINTF_MEMBER
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;
    size_t read(uint8_t *buf, size_t size);
    size_t readBytes(char *buffer, size_t length) { return read((uint8_t *)buffer, length); }

    bool seek(uint32_t pos, SeekMode mode);
    bool seek(uint32_t pos) { return seek(pos, SeekSet); }
    /* (size_t)-1 when there is no file. */
    size_t position() const;
    size_t size() const;
    /* ESP32's sets a stdio buffer; FatFs has its own sector buffer. */
    bool setBufferSize(size_t size)
    {
        (void)size;
        return live() != nullptr;
    }
    /* Closes the file for every copy of this File. */
    void close();
    operator bool() const { return live() != nullptr; }
    time_t getLastWrite();
    const char *path() const;
    const char *name() const;

    bool isDirectory();
    bool seekDir(long position);
    File openNextFile(const char *mode = FILE_READ);
    String getNextFileName();
    String getNextFileName(bool *isDir);
    void rewindDirectory();

private:
    FileImpl *live() const { return _p && _p->_gen == _gen ? _p : nullptr; }
    void release();
    FileImpl *_p;
    uint8_t _gen;
};

class FS {
public:
    explicit FS(FSImpl &impl) : _impl(impl) {}

    /* "r", "r+", "w", "w+", "a", "a+". create: make the missing parent
     * directories of a file opened for writing. */
    File open(const char *path, const char *mode = FILE_READ, bool create = false);
    File open(const String &path, const char *mode = FILE_READ, bool create = false)
    {
        return open(path.c_str(), mode, create);
    }
    bool exists(const char *path);
    bool exists(const String &path) { return exists(path.c_str()); }
    bool remove(const char *path);
    bool remove(const String &path) { return remove(path.c_str()); }
    bool rename(const char *pathFrom, const char *pathTo);
    bool rename(const String &pathFrom, const String &pathTo) { return rename(pathFrom.c_str(), pathTo.c_str()); }
    bool mkdir(const char *path);
    bool mkdir(const String &path) { return mkdir(path.c_str()); }
    bool rmdir(const char *path);
    bool rmdir(const String &path) { return rmdir(path.c_str()); }

protected:
    FSImpl &_impl;
};

} // namespace fs

using fs::File;
using fs::FS;
using fs::SeekCur;
using fs::SeekEnd;
using fs::SeekMode;
using fs::SeekSet;

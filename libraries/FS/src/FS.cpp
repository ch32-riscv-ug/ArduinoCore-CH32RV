#include "FS.h"

#include <string.h>

namespace fs {

File::File(FileImpl *p) : _p(p), _gen(p ? p->_gen : 0)
{
    _timeout = 0;
    if (_p) {
        _p->_refs++;
    }
}

File::File(const File &other) : Stream(), _p(other.live()), _gen(other._gen)
{
    _timeout = 0;
    if (_p) {
        _p->_refs++;
    }
}

File &File::operator=(const File &other)
{
    FileImpl *p = other.live();
    if (p) {
        p->_refs++;
    }
    release();
    _p = p;
    _gen = other._gen;
    return *this;
}

File::~File() { release(); }

void File::release()
{
    FileImpl *p = live();
    if (p && --p->_refs == 0) {
        p->_gen++;
        p->close();
    }
    _p = nullptr;
}

void File::close()
{
    FileImpl *p = live();
    if (p) {
        p->_refs = 0;
        p->_gen++;
        p->close();
    }
    _p = nullptr;
}

size_t File::write(const uint8_t *buf, size_t size)
{
    FileImpl *p = live();
    return p && buf ? p->write(buf, size) : 0;
}

size_t File::read(uint8_t *buf, size_t size)
{
    FileImpl *p = live();
    return p && buf ? p->read(buf, size) : 0;
}

int File::available()
{
    FileImpl *p = live();
    if (!p || p->isDirectory()) {
        return 0;
    }
    const size_t s = p->size(), pos = p->position();
    return pos < s ? (int)(s - pos) : 0;
}

int File::read()
{
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

int File::peek()
{
    FileImpl *p = live();
    if (!p) {
        return -1;
    }
    const size_t pos = p->position();
    uint8_t c;
    if (p->read(&c, 1) != 1) {
        return -1;
    }
    p->seek((uint32_t)pos, SeekSet);
    return c;
}

void File::flush()
{
    if (FileImpl *p = live()) {
        p->flush();
    }
}

bool File::seek(uint32_t pos, SeekMode mode)
{
    FileImpl *p = live();
    return p && p->seek(pos, mode);
}

size_t File::position() const
{
    FileImpl *p = live();
    return p ? p->position() : (size_t)-1;
}

size_t File::size() const
{
    FileImpl *p = live();
    return p ? p->size() : 0;
}

time_t File::getLastWrite()
{
    FileImpl *p = live();
    return p ? p->getLastWrite() : 0;
}

const char *File::path() const
{
    FileImpl *p = live();
    return p ? p->path() : nullptr;
}

const char *File::name() const
{
    FileImpl *p = live();
    return p ? p->name() : nullptr;
}

bool File::isDirectory()
{
    FileImpl *p = live();
    return p && p->isDirectory();
}

bool File::seekDir(long position)
{
    FileImpl *p = live();
    return p && p->seekDir(position);
}

File File::openNextFile(const char *mode)
{
    FileImpl *p = live();
    return File(p && mode ? p->openNextFile(mode) : nullptr);
}

String File::getNextFileName() { return getNextFileName(nullptr); }

String File::getNextFileName(bool *isDir)
{
    char path[FS_PATH_MAX];
    bool dir = false;
    FileImpl *p = live();
    if (!p || !p->nextFileName(path, sizeof path, &dir)) {
        path[0] = 0;
        dir = false;
    }
    if (isDir) {
        *isDir = dir;
    }
    return String(path);
}

void File::rewindDirectory()
{
    if (FileImpl *p = live()) {
        p->rewindDirectory();
    }
}

File FS::open(const char *path, const char *mode, bool create)
{
    if (!path || !mode) {
        return File();
    }
    return File(_impl.open(path, mode, create));
}

bool FS::exists(const char *path) { return path && _impl.exists(path); }

bool FS::remove(const char *path) { return path && _impl.remove(path); }

bool FS::rename(const char *pathFrom, const char *pathTo) { return pathFrom && pathTo && _impl.rename(pathFrom, pathTo); }

bool FS::mkdir(const char *path) { return path && _impl.mkdir(path); }

bool FS::rmdir(const char *path) { return path && _impl.rmdir(path); }

} // namespace fs

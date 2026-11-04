/* The FS library on the host, on a disk image (mapped, so what it writes is
 * in the file for fsck afterwards).
 *
 *   fs-host <image> api                 format, then check the API's behaviour
 *   fs-host <image> populate            write the sample tree on the volume there
 *   fs-host <image> verify              read the sample tree back
 *   fs-host <image> format              FatFS::format(), then populate
 *   fs-host <image> twovol <image2>     two volumes mounted at once
 *
 * Prints "ok <check>" or "FAIL <check>: <why>" per check; exits 1 on a FAIL. */
#include <FatFS.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(name, cond)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("ok %s\n", name);                                                     \
        } else {                                                                         \
            printf("FAIL %s: %s (line %d)\n", name, #cond, __LINE__);                    \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

/* A disk image as a block device; counts writes for the checks. */
class ImageDevice : public fs::BlockDevice {
public:
    bool load(const char *path)
    {
        int fd = ::open(path, O_RDWR);
        struct stat st;
        if (fd < 0 || fstat(fd, &st) != 0) {
            return false;
        }
        _size = (size_t)st.st_size;
        _mem = (uint8_t *)mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        return _mem != MAP_FAILED;
    }
    void unload()
    {
        if (_mem) {
            msync(_mem, _size, MS_SYNC);
            munmap(_mem, _size);
            _mem = nullptr;
        }
    }
    bool readBlocks(uint32_t lba, uint8_t *b, uint32_t n) override
    {
        if (!fits(lba, n)) {
            return false;
        }
        memcpy(b, _mem + (size_t)lba * BLOCK_SIZE, (size_t)n * BLOCK_SIZE);
        return true;
    }
    bool writeBlocks(uint32_t lba, const uint8_t *b, uint32_t n) override
    {
        if (!fits(lba, n) || readOnly) {
            return false;
        }
        memcpy(_mem + (size_t)lba * BLOCK_SIZE, b, (size_t)n * BLOCK_SIZE);
        writes++;
        return true;
    }
    uint32_t blockCount() override { return (uint32_t)(_size / BLOCK_SIZE); }
    bool writable() override { return !readOnly; }
    bool readOnly = false;
    uint32_t writes = 0;

private:
    bool fits(uint32_t lba, uint32_t n) const { return (size_t)(lba + n) * BLOCK_SIZE <= _size; }
    uint8_t *_mem = nullptr;
    size_t _size = 0;
};

static String readAll(FS &fs, const char *path)
{
    File f = fs.open(path);
    String s;
    if (!f) {
        return String("<missing>");
    }
    while (f.available()) {
        s += (char)f.read();
    }
    return s;
}

static bool writeText(FS &fs, const char *path, const char *text, const char *mode = FILE_WRITE)
{
    File f = fs.open(path, mode, true);
    if (!f) {
        return false;
    }
    const size_t n = strlen(text);
    const bool ok = f.write((const uint8_t *)text, n) == n;
    f.close();
    return ok;
}

static uint8_t pattern(size_t i) { return (uint8_t)((i * 7) ^ (i >> 8)); }

/* --------------------------------------------------------------- api */
static void api(ImageDevice &dev)
{
    FatFS fat;
#if FF_USE_MKFS
    CHECK("format", fat.format(dev));
#else
    CHECK("format not built in", !fat.format(dev));
#endif
    CHECK("mount", fat.begin(dev));
    CHECK("type", fat.fatType() != FatType::None);
    CHECK("total", fat.totalBytes() > 0 && fat.freeBytes() <= fat.totalBytes());

    CHECK("write", writeText(fat, "/hello.txt", "hello world"));
    CHECK("read back", readAll(fat, "/hello.txt") == "hello world");
    CHECK("exists", fat.exists("/hello.txt") && !fat.exists("/nope.txt") && fat.exists("/"));

    /* "w" replaces the file; while the cut is pending the File is the new one */
    {
        File f = fat.open("/hello.txt", "w+");
        CHECK("w+ open", (bool)f);
        CHECK("w+ empty at open", f.size() == 0 && f.available() == 0);
        f.print("hi");
        CHECK("w+ size", f.size() == 2);
        CHECK("w+ seek", f.seek(0));
        CHECK("w+ read stops at new end", f.readString() == "hi");
        CHECK("seek past end refused", !f.seek(10));
        f.close();
        CHECK("w cut at close", readAll(fat, "/hello.txt") == "hi");
    }
    /* a cut flushed: what is on the volume is the new file */
    {
        File f = fat.open("/hello.txt", "w");
        f.print("x");
        f.flush();
        FatFS other;   /* not mounted: a second look needs the same volume */
        (void)other;
        CHECK("flush cuts", f.size() == 1);
        f.close();
        CHECK("after flush", readAll(fat, "/hello.txt") == "x");
    }
    CHECK("append", writeText(fat, "/hello.txt", "yz", FILE_APPEND) && readAll(fat, "/hello.txt") == "xyz");
    {
        File f = fat.open("/hello.txt", "r+");
        f.seek(1);
        f.write('Y');
        f.close();
        CHECK("r+ overwrite", readAll(fat, "/hello.txt") == "xYz");
    }
    {
        File f = fat.open("/hello.txt", "a+");
        f.print("!");
        f.seek(0);
        CHECK("a+ read", f.readString() == "xYz!");
    }
    {
        File f = fat.open("/hello.txt");
        CHECK("peek", f.peek() == 'x' && f.read() == 'x' && f.position() == 1);
        CHECK("seek end", f.seek(0, SeekEnd) && f.position() == 4 && f.read() == -1);
        CHECK("seek cur", f.seek(0) && f.seek(2, SeekCur) && f.read() == 'z');
        CHECK("read-only refuses write", f.write('q') == 0);
        CHECK("name/path", strcmp(f.name(), "hello.txt") == 0 && strcmp(f.path(), "/hello.txt") == 0);
    }
    CHECK("missing", !fat.open("/missing.txt"));
    CHECK("bad mode", !fat.open("/hello.txt", "x"));
    CHECK("relative refused", !fat.open("hello.txt"));

    /* directories */
    CHECK("mkdir", fat.mkdir("/dir"));
    CHECK("mkdir again fails", !fat.mkdir("/dir"));
    CHECK("create parents", writeText(fat, "/a/b/c/deep.txt", "deep") && readAll(fat, "/a/b/c/deep.txt") == "deep");
    CHECK("no parents without create", !fat.open("/q/r.txt", FILE_WRITE));
    writeText(fat, "/dir/one.txt", "1");
    writeText(fat, "/dir/two.txt", "22");
    fat.mkdir("/dir/sub");
    {
        File d = fat.open("/dir");
        CHECK("open dir", d && d.isDirectory() && d.size() == 0 && d.available() == 0);
        int files = 0, dirs = 0;
        size_t bytes = 0;
        for (File e = d.openNextFile(); e; e = d.openNextFile()) {
            if (e.isDirectory()) {
                dirs++;
                CHECK("subdir path", strcmp(e.path(), "/dir/sub") == 0);
            } else {
                files++;
                bytes += e.size();
            }
        }
        CHECK("list", files == 2 && dirs == 1 && bytes == 3);
        d.rewindDirectory();
        bool isDir = true;
        String first = d.getNextFileName(&isDir);
        CHECK("getNextFileName", first.startsWith("/dir/") && first.length() > 5);
        CHECK("seekDir", d.seekDir(2) && d.getNextFileName().length() > 0 && d.getNextFileName().length() == 0);
    }
    {
        File root = fat.open("/");
        CHECK("root", root && root.isDirectory());
        File e = root.openNextFile();
        CHECK("root child path", e && e.path()[0] == '/' && e.path()[1] != '/');
    }
    CHECK("remove dir via remove fails", !fat.remove("/dir/sub"));
    CHECK("rmdir non-empty fails", !fat.rmdir("/dir"));
    CHECK("rmdir", fat.rmdir("/dir/sub") && !fat.exists("/dir/sub"));
    CHECK("rmdir file fails", !fat.rmdir("/dir/one.txt"));
    CHECK("remove", fat.remove("/dir/one.txt") && !fat.exists("/dir/one.txt"));
    CHECK("rename", fat.rename("/dir/two.txt", "/dir/2.txt") && readAll(fat, "/dir/2.txt") == "22");
    CHECK("rename over existing fails", writeText(fat, "/dir/3.txt", "3") && !fat.rename("/dir/3.txt", "/dir/2.txt"));
    CHECK("rename across dirs", fat.rename("/dir/3.txt", "/a/3.txt") && readAll(fat, "/a/3.txt") == "3");

    /* names */
    CHECK("long name", writeText(fat, "/A long file name with spaces.text", "L") &&
                           readAll(fat, "/A long file name with spaces.text") == "L");
    CHECK("utf-8 name", writeText(fat, "/日本語のファイル名.txt", "ja") && readAll(fat, "/日本語のファイル名.txt") == "ja");
    CHECK("case-insensitive", readAll(fat, "/HELLO.TXT") == "xYz!");
    {
        char p[FS_PATH_MAX + 8];
        memset(p, 'a', sizeof p);
        p[0] = '/';
        p[sizeof p - 1] = 0;
        CHECK("path too long", !fat.open(p, FILE_WRITE));
    }

    /* handles: copies share, the last one closes; close() ends every copy */
    {
        File a = fat.open("/hello.txt");
        File b = a;
        a = File();
        CHECK("copy keeps open", b && b.read() == 'x');
        File c = b;
        b.close();
        CHECK("close ends copies", !c && c.read() == -1 && c.position() == (size_t)-1);
        File n = fat.open("/a/3.txt");
        CHECK("stale copy does not reach the slot's next file", !c && n.read() == '3');
    }
    {
        File f[FS_MAX_OPEN_FILES + 1];
        int opened = 0;
        for (auto &x : f) {
            x = fat.open("/hello.txt");
            opened += x ? 1 : 0;
        }
        CHECK("open limit", opened == FS_MAX_OPEN_FILES);
    }
    CHECK("slots back", (bool)fat.open("/hello.txt"));
#if FF_FS_LOCK
    {
        File w = fat.open("/hello.txt", FILE_APPEND);
        CHECK("second writer refused", w && !fat.open("/hello.txt", FILE_APPEND));
        CHECK("remove of an open file refused", !fat.remove("/hello.txt"));
    }
#endif

    /* a large file through the sector buffer, at odd sizes */
    {
        File f = fat.open("/big.bin", FILE_WRITE);
        uint8_t buf[777];
        size_t n = 0;
        while (n < 300000) {
            for (size_t i = 0; i < sizeof buf; i++) {
                buf[i] = pattern(n + i);
            }
            n += f.write(buf, sizeof buf);
        }
        f.close();
        f = fat.open("/big.bin");
        bool same = f.size() == n;
        size_t m = 0;
        while (same && m < n) {
            const size_t got = f.read(buf, 333);
            for (size_t i = 0; i < got; i++) {
                same = same && buf[i] == pattern(m + i);
            }
            m += got;
            if (!got) {
                break;
            }
        }
        CHECK("big file", same && m == n);
        CHECK("used grows", fat.usedBytes() >= n);
    }

    /* end() closes what is open and what it wrote is on the volume */
    {
        File f = fat.open("/open-at-end.txt", FILE_WRITE);
        f.print("kept");
        fat.end();
        CHECK("file dead after end", !f && f.write('x') == 0);
        CHECK("unmounted", !fat.mounted() && fat.totalBytes() == 0 && !fat.open("/hello.txt"));
        CHECK("remount", fat.begin(dev) && readAll(fat, "/open-at-end.txt") == "kept");
    }
    /* a write-protected device mounts read-only */
    fat.end();
    dev.readOnly = true;
    CHECK("read-only mount", fat.begin(dev) && readAll(fat, "/hello.txt") == "xYz!");
    CHECK("read-only refuses", !fat.open("/new.txt", FILE_WRITE) && !fat.remove("/hello.txt"));
    fat.end();
    dev.readOnly = false;
}

/* --------------------------------------------------------------- the sample tree */
static const char *const sampleNames[] = {
    "/README.TXT", "/lower.txt", "/MixedCase.Txt", "/A directory with a long name/inner file.txt",
    "/日本語/ファイル.txt", "/many/f00.dat", "/a.b.c.d", "/no_extension",
};

/* full: the 60 files and 150 KB too (not on a volume under 1 MB). */
static void populate(FS &fs, bool full)
{
    for (const char *n : sampleNames) {
        CHECK(n, writeText(fs, n, n));
    }
    if (!full) {
        return;
    }
    for (int i = 0; i < 60; i++) {
        char p[48];
        snprintf(p, sizeof p, "/many/same_prefix_long_name_%02d.dat", i);
        if (!writeText(fs, p, p)) {
            CHECK("many", false);
            return;
        }
    }
    File f = fs.open("/many/big.bin", FILE_WRITE);
    for (size_t i = 0; i < 150000; i++) {
        f.write(pattern(i));
    }
    f.close();
    CHECK("populate", true);
}

static void verify(FS &fs, bool full)
{
    for (const char *n : sampleNames) {
        CHECK(n, readAll(fs, n) == n);
    }
    if (!full) {
        return;
    }
    int count = 0;
    File d = fs.open("/many");
    for (File e = d.openNextFile(); e; e = d.openNextFile()) {
        count++;
    }
    CHECK("many listed", count == 62);
    File f = fs.open("/many/big.bin");
    bool same = f.size() == 150000;
    for (size_t i = 0; same && i < 150000; i++) {
        same = f.read() == pattern(i);
    }
    CHECK("big", same);
}

/* api/Common.h declares main() without arguments (the core's); the host's
 * real main() below hands argc / argv over. */
static int run(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <image> api|populate|verify|format|twovol <image2>\n", argv[0]);
        return 2;
    }
    ImageDevice dev;
    if (!dev.load(argv[1])) {
        printf("FAIL load %s\n", argv[1]);
        return 1;
    }
    const char *cmd = argv[2];
    const bool full = dev.blockCount() >= 2048;
    FatFS fat;
    if (!strcmp(cmd, "api")) {
        api(dev);
    } else if (!strcmp(cmd, "populate") || !strcmp(cmd, "verify")) {
        CHECK("mount", fat.begin(dev));
        if (fat.mounted()) {
            if (cmd[0] == 'p') {
                populate(fat, full);
            } else {
                verify(fat, full);
            }
            fat.end();
        }
    } else if (!strcmp(cmd, "format")) {
        CHECK("format", fat.format(dev) && fat.begin(dev));
        if (fat.mounted()) {
            populate(fat, full);
            fat.end();
        }
    } else if (!strcmp(cmd, "twovol") && argc > 3) {
        ImageDevice dev2;
        FatFS second, third;
        CHECK("load second", dev2.load(argv[3]));
        CHECK("mount both", fat.begin(dev, true) && second.begin(dev2, true));
#if FF_VOLUMES == 2
        CHECK("no third volume", !third.begin(dev2));
#endif
        CHECK("write one", writeText(fat, "/from-first.txt", "first volume"));
        {
            File in = fat.open("/from-first.txt");
            File out = second.open("/copied.txt", FILE_WRITE);
            uint8_t b[16];
            size_t n;
            while ((n = in.read(b, sizeof b)) > 0) {
                out.write(b, n);
            }
        }
        CHECK("copied across", readAll(second, "/copied.txt") == "first volume" && !fat.exists("/copied.txt"));
        CHECK("rename on second", second.rename("/copied.txt", "/renamed.txt") && second.exists("/renamed.txt") &&
                                      !fat.exists("/renamed.txt"));
        fat.end();
        CHECK("second stays", readAll(second, "/renamed.txt") == "first volume");
        second.end();
        dev2.unload();
    } else {
        fprintf(stderr, "unknown command %s\n", cmd);
        return 2;
    }
    dev.unload();
    printf("%s: %d failure(s)\n", cmd, failures);
    return failures ? 1 : 0;
}

static int g_argc;
static char **g_argv;
static void __attribute__((constructor)) keepArgs(int argc, char **argv, char **)
{
    g_argc = argc;
    g_argv = argv;
}
int main() { return run(g_argc, g_argv); }

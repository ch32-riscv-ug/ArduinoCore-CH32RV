/* The FS library against another FAT implementation (tests/peer/usb, case
 * usb-peer-fs): a volume the DUT formats and writes with FatFS is shown over
 * USB mass storage (CH32RVUsbDeviceMscBlockStorage) to the ESP32-S3, which
 * mounts it with ESP-IDF's FAT and reads and writes files; the DUT takes it
 * back and reads them.
 *
 * X035 has 20 KB of RAM and FatFs makes nothing under 64 blocks, so the
 * 128-block (64 KB) medium keeps only the blocks that are not all zero, in
 * SLOTS slots: FAT12 of a few small files fits.
 *
 *   ready <nonce>   READY <nonce> <build id> <USB started 0/1> <last error>
 *   prep            PREP <ok 0/1> <slots used>: format, write README.TXT (its
 *                   first line names the build), a long-named directory and
 *                   file and CFG.TXT, unmount, show the medium
 *   take <token>    TAKE <mount 0/1> <own files intact 0/1> <files in /> <first line
 *                   of HOST.TXT or -> : take the medium back, mount and check;
 *                   then README.TXT's first line becomes "CH32RV usb-fs <token>"
 *                   (an in-place rewrite, cut at close), unmount, show it again
 *   slots           SLOTS <used> <failed writes> */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <CH32RVUsbDeviceMscBlock.h>
#include <FatFS.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

#define BLOCKS 128
#define SLOTS 22

/* A medium larger than the RAM: blocks never written non-zero read as zeros. */
class SparseDisk : public fs::BlockDevice {
public:
    bool readBlocks(uint32_t lba, uint8_t *b, uint32_t n) override
    {
        if (lba + n > BLOCKS) {
            return false;
        }
        for (uint32_t i = 0; i < n; i++) {
            const int s = find(lba + i);
            if (s < 0) {
                memset(b + i * BLOCK_SIZE, 0, BLOCK_SIZE);
            } else {
                memcpy(b + i * BLOCK_SIZE, data[s], BLOCK_SIZE);
            }
        }
        return true;
    }
    bool writeBlocks(uint32_t lba, const uint8_t *b, uint32_t n) override
    {
        if (lba + n > BLOCKS) {
            return false;
        }
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t *src = b + i * BLOCK_SIZE;
            bool zero = true;
            for (uint32_t k = 0; k < BLOCK_SIZE && zero; k++) {
                zero = src[k] == 0;
            }
            int s = find(lba + i);
            if (zero) {
                if (s >= 0) {
                    used[s] = false;
                }
                continue;
            }
            if (s < 0) {
                for (s = 0; s < SLOTS && used[s]; s++) {
                }
                if (s == SLOTS) {
                    failed++;
                    return false;
                }
                used[s] = true;
                at[s] = (uint16_t)(lba + i);
            }
            memcpy(data[s], src, BLOCK_SIZE);
        }
        return true;
    }
    uint32_t blockCount() override { return BLOCKS; }
    unsigned inUse() const
    {
        unsigned n = 0;
        for (bool u : used) {
            n += u;
        }
        return n;
    }
    uint32_t failed = 0;

private:
    int find(uint32_t lba) const
    {
        for (int s = 0; s < SLOTS; s++) {
            if (used[s] && at[s] == lba) {
                return s;
            }
        }
        return -1;
    }
    uint8_t data[SLOTS][BLOCK_SIZE];
    uint16_t at[SLOTS];
    bool used[SLOTS];
};

static SparseDisk disk;
CH32RVUsbDeviceMsc msc;
CH32RVUsbDeviceMscBlockStorage shared(disk);
FatFS fat;
static CH32RVUsbDeviceConfig config;
static char line[80];
static uint8_t usedChars;

static const char *const longDir = "/A long directory name";
static const char *const longFile = "/A long directory name/inner long file.txt";

static bool writeText(const char *path, const char *text)
{
    File f = fat.open(path, FILE_WRITE);
    const size_t n = strlen(text);
    return f && f.write((const uint8_t *)text, n) == n;
}

static bool firstLine(const char *path, char *out, size_t size)
{
    File f = fat.open(path);
    if (!f) {
        return false;
    }
    size_t n = 0;
    while (f.available() && n + 1 < size) {
        const int c = f.read();
        if (c == '\r' || c == '\n') {
            break;
        }
        out[n++] = (char)c;
    }
    out[n] = 0;
    return true;
}

static void prep()
{
    msc.mediaPresent(false);
    bool ok = fat.begin(disk, true);
    ok = ok && writeText("/README.TXT", "CH32RV usb-fs " BENCH_BUILD_ID "\r\nwritten by FatFS on the DUT\r\n");
    ok = ok && fat.mkdir(longDir) && writeText(longFile, "inner");
    ok = ok && writeText("/CFG.TXT", "v1");
    fat.end();
    msc.mediaPresent(true);
    SerialDMSeq.printf("PREP %d %u\r\n", ok ? 1 : 0, disk.inUse());
}

static void take(const char *token)
{
    msc.mediaPresent(false);
    const bool mounted = fat.begin(disk);
    char buf[64];
    bool mine = mounted && firstLine(longFile, buf, sizeof buf) && strcmp(buf, "inner") == 0 &&
                firstLine("/CFG.TXT", buf, sizeof buf) && strcmp(buf, "v1") == 0;
    unsigned files = 0;
    File root = fat.open("/");
    for (File e = root.openNextFile(); e; e = root.openNextFile()) {
        files++;
    }
    root.close();
    char host[64];
    if (!mounted || !firstLine("/HOST.TXT", host, sizeof host)) {
        strcpy(host, "-");
    }
    if (mounted) {
        char text[64];
        snprintf(text, sizeof text, "CH32RV usb-fs %s\r\n", token);
        mine = writeText("/README.TXT", text) && mine;
    }
    fat.end();
    msc.mediaPresent(true);
    SerialDMSeq.printf("TAKE %d %d %u %s\r\n", mounted ? 1 : 0, mine ? 1 : 0, files, host);
}

static void reply(const char *command)
{
    if (strncmp(command, "ready ", 6) == 0) {
        SerialDMSeq.printf("READY %s %s %d %s\r\n", command + 6, BENCH_BUILD_ID, USBDevice.started() ? 1 : 0,
                           USBDevice.lastErrorName());
    } else if (strcmp(command, "prep") == 0) {
        prep();
    } else if (strncmp(command, "take ", 5) == 0) {
        take(command + 5);
    } else if (strcmp(command, "slots") == 0) {
        SerialDMSeq.printf("SLOTS %u %lu\r\n", disk.inUse(), (unsigned long)disk.failed);
    } else {
        SerialDMSeq.printf("UNKNOWN %s\r\n", command);
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    msc.vendorID("CH32RV");
    msc.productID("usb-fs");
    shared.attach(msc);
    msc.mediaPresent(false);
    config.product = "CH32RV usb-fs " BENCH_BUILD_ID;
    config.selfPowered = true;
    USBDevice.begin(config);
}

void loop()
{
    deliverEvents();
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
        if (c == '\n') {
            line[usedChars] = 0;
            if (usedChars) {
                reply(line);
            }
            usedChars = 0;
        } else if (c != '\r' && usedChars < sizeof line - 1) {
            line[usedChars++] = (char)c;
        }
    }
}

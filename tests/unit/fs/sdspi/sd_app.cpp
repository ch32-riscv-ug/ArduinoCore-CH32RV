/* The SD library (SPI) on the host against a simulated card (sdcard_sim.h)
 * over a disk image, which fsck reads afterwards.
 *
 *   sd-host <image> sdhc|sdscv2|sdscv1      identify, format, files, faults
 *   sd-host - absent                        no card on the bus */
#include <SD.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sdcard_sim.h"

static int failures;
#define CHECK(name, cond)                                                           \
    do {                                                                            \
        if (cond) {                                                                 \
            printf("ok %s\n", name);                                                \
        } else {                                                                    \
            printf("FAIL %s: %s (line %d)\n", name, #cond, __LINE__);               \
            failures++;                                                             \
        }                                                                           \
    } while (0)

static const int CS = 10;
static SdCardSim *card;
static uint8_t silentMem[512];
static SdCardSim silent(silentMem, 1, SimKind::Sdhc);
SPIClass &SPI = silent;

void pinMode(pin_size_t, PinMode) {}
void digitalWrite(pin_size_t pin, PinStatus status)
{
    if ((int)pin == CS && card) {
        card->select(status == LOW);
    }
}

/* A bus with nothing on it reads 0xFF. */
class EmptyBus : public SPIClass {
public:
    uint8_t transfer(uint8_t) override { return 0xFF; }
    void beginTransaction(arduino::SPISettings) override {}
    void endTransaction() override {}
};

static uint8_t pattern(size_t i) { return (uint8_t)((i * 13) ^ (i >> 7)); }

static int runCard(const char *imagePath, SimKind kind)
{
    int fd = open(imagePath, O_RDWR);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) {
        printf("FAIL open %s\n", imagePath);
        return 1;
    }
    const size_t size = (size_t)st.st_size;
    uint8_t *mem = (uint8_t *)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    SdCardSim sim(mem, (uint32_t)(size / 512), kind);
    card = &sim;

    CHECK("begin (formats the blank card)", SD.begin(CS, sim, 8000000, "/sd", 5, true));
    const sdcard_type_t want = kind == SimKind::Sdhc ? CARD_SDHC : CARD_SD;
    CHECK("card type", SD.cardType() == want);
    CHECK("size from the CSD", SD.cardSize() == size && SD.numSectors() == size / 512 && SD.sectorSize() == 512);
    CHECK("CRC on", sim.crcOn);
    CHECK("transfer clock", sim.lastClock == 8000000);
    CHECK("FAT volume", SD.fatType() != FatType::None && SD.totalBytes() > 0);

    File f = SD.open("/dir/data.bin", FILE_WRITE, true);
    const size_t n = 200000;
    uint8_t buf[4096];
    for (size_t done = 0; done < n;) {
        const size_t k = n - done < sizeof buf ? n - done : sizeof buf;
        for (size_t i = 0; i < k; i++) {
            buf[i] = pattern(done + i);
        }
        done += f.write(buf, k);
    }
    f.close();
    CHECK("multiple-block writes used", sim.multiWrites > 0);
    f = SD.open("/dir/data.bin");
    bool same = f.size() == n;
    size_t got = 0;
    while (same) {
        const size_t k = f.read(buf, sizeof buf);
        if (!k) {
            break;
        }
        for (size_t i = 0; i < k; i++) {
            same = same && buf[i] == pattern(got + i);
        }
        got += k;
    }
    f.close();
    CHECK("read back", same && got == n);
    CHECK("multiple-block reads used", sim.multiReads > 0);
    File t = SD.open("/hello.txt", FILE_WRITE);
    t.print("hello from SD");
    t.close();
    CHECK("small file", SD.open("/hello.txt").readString() == "hello from SD");

    /* faults */
    uint8_t block[512];
    sim.breakReadCrcOnce = 1;
    CHECK("bad data CRC refused", !SD.readRAW(block, 0));
    CHECK("next read good", SD.readRAW(block, 0) && block[510] == 0x55 && block[511] == 0xAA);
    sim.refuseWrites = true;
    CHECK("refused write reported", !SD.writeRAW(block, 1));
    sim.refuseWrites = false;
    CHECK("raw write", SD.writeRAW(block, 0));
    CHECK("out of range", !SD.readRAW(block, (uint32_t)(size / 512)));

    SD.end();
    CHECK("end", SD.cardType() == CARD_NONE && !SD.mounted());
    CHECK("again", SD.begin(CS, sim, 8000000) && SD.open("/hello.txt").readString() == "hello from SD");
    SD.end();
    card = nullptr;
    msync(mem, size, MS_SYNC);
    munmap(mem, size);
    return 0;
}

static int run(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: sd-host <image> sdhc|sdscv2|sdscv1|absent\n");
        return 2;
    }
    const char *kind = argv[2];
    if (!strcmp(kind, "absent")) {
        EmptyBus bus;
        CHECK("no card", !SD.begin(CS, bus) && SD.cardType() == CARD_NONE);
    } else if (!strcmp(kind, "sdhc")) {
        runCard(argv[1], SimKind::Sdhc);
    } else if (!strcmp(kind, "sdscv2")) {
        runCard(argv[1], SimKind::SdscV2);
    } else if (!strcmp(kind, "sdscv1")) {
        runCard(argv[1], SimKind::SdscV1);
    } else {
        return 2;
    }
    printf("%s: %d failure(s)\n", kind, failures);
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

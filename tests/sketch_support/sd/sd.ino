/* SD cards (tests/instrumented/sd): the SD library on SPI and SD_MMC on the
 * SDIO host, on the pads the bench labels (bench_lines.py: spisd, sdio). The
 * card must hold a FAT volume; the test writes, reads back and removes one
 * file (/CH32TEST.BIN).
 *
 *   ready <nonce>                      READY <nonce> <build id>
 *   spi <sck> <miso> <mosi> <cs> <hz>  SPI <ok> <card type> <sectors> <FAT type> <hw|soft>
 *                                      (SoftSPI when no SPI instance reaches those pads, as on an SDIO
 *                                      slot: nanoCH32H417)
 *   sdio <ck> <cmd> <d0> [<d1> <d2> <d3>] <khz>
 *                                      SDIO <ok> <card type> <sectors> <FAT type> <clock kHz> <bits>
 *   files <kb>                         FILES <ok> <write ms> <read ms> <entries in />
 *   end                                END
 * Pins are the sketch's pin numbers. */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <SD.h>
#include <SD_MMC.h>
#include <SoftSPI.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static FatFS *mounted;
static SoftSPI *soft;
static char line[96];
static uint8_t used;
static uint8_t block[2048];

static void files(uint32_t kb)
{
    bool ok = mounted != nullptr;
    uint32_t wms = 0, rms = 0, entries = 0;
    if (ok) {
        uint32_t t0 = millis();
        File f = mounted->open("/CH32TEST.BIN", FILE_WRITE);
        ok = f;
        for (uint32_t i = 0; ok && i < kb * 1024u / sizeof block; i++) {
            for (size_t k = 0; k < sizeof block; k++) {
                block[k] = (uint8_t)(i * 7 + k);
            }
            ok = f.write(block, sizeof block) == sizeof block;
        }
        f.close();
        wms = millis() - t0;
        t0 = millis();
        f = mounted->open("/CH32TEST.BIN");
        ok = ok && f && f.size() == kb * 1024u;
        for (uint32_t i = 0; ok && i < kb * 1024u / sizeof block; i++) {
            ok = f.read(block, sizeof block) == sizeof block;
            for (size_t k = 0; ok && k < sizeof block; k++) {
                ok = block[k] == (uint8_t)(i * 7 + k);
            }
        }
        f.close();
        rms = millis() - t0;
        File root = mounted->open("/");
        for (File e = root.openNextFile(); e; e = root.openNextFile()) {
            entries++;
        }
        root.close();
        ok = mounted->remove("/CH32TEST.BIN") && ok;
    }
    SerialDMSeq.printf("FILES %d %lu %lu %lu\r\n", ok ? 1 : 0, (unsigned long)wms, (unsigned long)rms,
                       (unsigned long)entries);
}

static void reply(char *command)
{
    char *argv[8];
    int argc = 0;
    for (char *t = strtok(command, " "); t && argc < 8; t = strtok(nullptr, " ")) {
        argv[argc++] = t;
    }
    if (!argc) {
        return;
    }
    if (!strcmp(argv[0], "ready") && argc == 2) {
        SerialDMSeq.printf("READY %s %s\r\n", argv[1], BENCH_BUILD_ID);
    } else if (!strcmp(argv[0], "spi") && argc == 6) {
        const int sck = atoi(argv[1]), miso = atoi(argv[2]), mosi = atoi(argv[3]);
        const bool hw = SPI.begin(sck, miso, mosi);
        if (!hw && !soft) {
            soft = new SoftSPI((uint8_t)sck, (uint8_t)miso, (uint8_t)mosi);
        }
        if (!hw) {
            soft->begin(sck, miso, mosi);
        }
        SPIClass &bus = hw ? (SPIClass &)SPI : (SPIClass &)*soft;
        const bool ok = SD.begin((uint8_t)atoi(argv[4]), bus, (uint32_t)atol(argv[5]));
        mounted = ok ? &SD : nullptr;
        SerialDMSeq.printf("SPI %d %d %lu %d %s\r\n", ok ? 1 : 0, (int)SD.cardType(), (unsigned long)SD.numSectors(),
                           (int)SD.fatType(), hw ? "hw" : "soft");
    } else if (!strcmp(argv[0], "sdio") && (argc == 5 || argc == 8)) {
        const bool pins = argc == 8 ? SD_MMC.setPins(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]),
                                                     atoi(argv[5]), atoi(argv[6]))
                                    : SD_MMC.setPins(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
        const bool ok = pins && SD_MMC.begin("/sdcard", argc == 5, false, atoi(argv[argc - 1]));
        mounted = ok ? &SD_MMC : nullptr;
        SerialDMSeq.printf("SDIO %d %d %lu %d %lu %d\r\n", ok ? 1 : 0, (int)SD_MMC.cardType(),
                           (unsigned long)SD_MMC.numSectors(), (int)SD_MMC.fatType(),
                           (unsigned long)(SD_MMC.card().clockHz() / 1000u), SD_MMC.card().wide() ? 4 : 1);
    } else if (!strcmp(argv[0], "files") && argc == 2) {
        files((uint32_t)atol(argv[1]));
    } else if (!strcmp(argv[0], "end")) {
        SD.end();
        SD_MMC.end();
        mounted = nullptr;
        SerialDMSeq.println("END");
    } else {
        SerialDMSeq.printf("UNKNOWN %s\r\n", argv[0]);
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    SerialDMSeq.println(CH32RV.resetReasonName());
}

void loop()
{
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
        if (c == '\n') {
            line[used] = 0;
            if (used) {
                reply(line);
            }
            used = 0;
        } else if (c != '\r' && used < sizeof line - 1) {
            line[used++] = (char)c;
        }
    }
}

/* SPI contracts (tests/instrumented/spi): the hardware SPI on its default
 * pins, or a SoftSPI on the bench's pads, as the controller of an OEP probe's
 * spi-target, CS an ordinary GPIO. Console on CONSOLE (bench_console.h):
 *
 *   ready <nonce>                      READY <nonce> <build id>
 *   pins <sck> <miso> <mosi> <cs>      SoftSPI on these: PINS <1 opened / 0 refused>
 *   spipins                            the SPI's default pins, SPI not begun:
 *                                      SPIPINS <sck> <miso> <mosi> (SPIPINS NONE)
 *   hw <cs>                            SPI on its default pins: HW <sck> <miso> <mosi>
 *                                      (HW NONE on a part without SPI)
 *   xfer <mode> <lsb> <hz> <word> <bytes>
 *                                      at SPISettings(hz, ...): CS low; transfer() each byte (hex,
 *                                      two digits each), then transfer16(word,
 *                                      hex); CS high:
 *                                      XFER <bytes read, hex> <word read, hex>
 *
 *   shift <lsb> <out hex> <n>         after `pins` (SoftSPI's pins): CS low; shiftOut() each out byte
 *                                      (MOSI as data, SCK as clock), then shiftIn() n bytes (MISO as
 *                                      data); CS high: SHIFT <bytes read, hex> (SHIFT NONE before `pins`)
 *
 *   hwcs <0|1>                         SPI.setHwCs(): HWCS <1 taken / 0> <the NSS pad, 0 when none>;
 *                                      while on, xfer and blk leave CS to the SPI (the NSS pad)
 *   crc <poly> <bytes>                 after `hw`: setCRC(poly, hex), then CS low; transferCRC() of the
 *                                      bytes (hex, up to 16) at 1 MHz, mode 0 MSB first; CS high:
 *                                      CRC <1 matched / 0 / 2 no CRC here> <crcSent()> <crcReceived()>
 *                                      <bytes read, hex>
 *   tw <hz> <bytes> <n>                after `hw`: setThreeWire(true), mode 0 MSB first; CS low; transfer() of
 *                                      the bytes (hex) on the one line, then receive() of n bytes; CS high;
 *                                      setThreeWire(false): TW <1 / 0; 2 no hardware SPI> <bytes received, hex>
 *   blk <how> <hz> <bytes>             the buffer transfers, mode 0 MSB first, CS low around one
 *                                      call with the bytes (hex, up to 80): how w writeBytes, t
 *                                      transferBytes, b transfer(buf, n), a transferAsync out only,
 *                                      A transferAsync both ways (finishedAsync() polled):
 *                                      BLK <1 / 0 started> <bytes read, hex; - for w and a>
 *
 * A slave (the probe's spi-controller is the master): CH32RVSPISlave on the SPI's
 * default pins, or - once `pins` named SoftSPI's pins - SoftSPISlave on those:
 *
 *   slave <cs> <mode> <lsb> <fill>     the bus given back (SPI.end() / soft.end()), the slave
 *                                      begun with CS on <cs> and <fill> (hex) once tx runs
 *                                      out: SLAVE <1 / 0>
 *   sq <size> <tx bytes, hex>          queue(tx, rx, size): SQ <1 / 0>
 *   sres                               the oldest result, and the counters:
 *                                      SRES <length> <rx bytes kept, hex> <missed> <overruns>
 *                                      (SRES NONE - <missed> <overruns> when none)
 *   send                               slave.end(): SEND
 *
 * I2S (the I2S library, I2S2 / I2S3 of V30x, V4x7, H41x; I2S NONE elsewhere):
 *
 *   i2sfind <pin>...                   three of these pins that are one I2S's CK, WS and SD (the
 *                                      variant's CH32RV_I2Sn_PADS; on the AFIO families on one
 *                                      route), nothing driven: I2SFIND <ck> <ws> <sd> (I2SFIND NONE)
 *   i2s <ck> <ws> <sd> <bits> <rate> <format> <mono> <frames hex>
 *                                      setFormat(format: 0 Philips, 1 MSB, 2 LSB), begin() as an
 *                                      output, then the frames (the samples as write() takes them,
 *                                      up to 64 bytes) played over and over from loop():
 *                                      I2S <1 / 0> <sampleRate()> <lastError()>
 *   i2srx <ck> <ws> <sd> <bits> <rate> <frames>
 *                                      begin() as a stereo input (Philips), readBytes() that many
 *                                      frames (the input left running until i2send); each slot counted as all ones, all zeros or
 *                                      mixed, and the left slot's rises from zeros to ones:
 *                                      I2SRX <1 / 0> <sampleRate()> <frames read> <ones> <zeros>
 *                                      <mixed> <rises> <overruns>
 *   i2stat                             I2STAT <underruns>
 *   i2send                             end(): I2SEND
 *
 * The bus itself as a peripheral (SPI_HAS_PERIPHERAL_MODE): SPI after `hw`, SoftSPI after
 * `pins`, CS left to the master (the DUT's CS pin an input):
 *
 *   periph <mode> <lsb> <n> <tx hex>   beginTransaction(SPISettings(1000000, order, mode,
 *                                      SPI_PERIPHERAL)), PERIPH READY, then transfer() n bytes
 *                                      as the master clocks them, endTransaction():
 *                                      PERIPH <bytes read, hex> */
#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
#include <SPI.h>
#if defined(SPI_INTERFACES_COUNT)
#include <CH32RVSPISlave.h>
#endif
#include <I2S.h>
/* a row of the variant's CH32RV_I2Sn_PADS (here: the prototypes the IDE adds come first) */
struct I2SPad {
    uint8_t role, pad;
    uint32_t value, value2;
};
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static SoftSPI soft(CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN);
static SPIClass *bus = &soft;
static int cs = -1;
/* SoftSPI's pins, once `pins` named them: the slave is then a SoftSPISlave on them. */
static int softPins[3] = {-1, -1, -1};

static void hex(uint32_t v, uint8_t digits)
{
    while (digits--) {
        CONSOLE.print((char)"0123456789abcdef"[(v >> (digits * 4)) & 0xFu]);
    }
}

static size_t unhex(const char *text, uint8_t *out, size_t room)
{
    size_t n = 0;
    while (text[0] && text[1] && n < room) {
        const char pair[3] = {text[0], text[1], 0};
        out[n++] = (uint8_t)strtoul(pair, nullptr, 16);
        text += 2;
    }
    return n;
}

static bool hwcs;      /* SPI.setHwCs(true): the SPI drives CS (its NSS pad) */

static void xfer(uint8_t mode, bool lsb, uint32_t hz, uint16_t word, const char *bytes)
{
    uint8_t out[16];
    const size_t n = unhex(bytes, out, sizeof out);
    if (bus == &soft) {
        /* SoftSPI does not read the clock from SPISettings (SoftSPI.h): without
         * a half period it runs as fast as the CPU toggles - 3.5 MHz on H417,
         * where the probe's 10 MHz samples cannot tell the edges apart. */
        soft.setHalfPeriodUs((uint16_t)(hz ? (500000u + hz - 1u) / hz : 0u));
    }
    bus->beginTransaction(SPISettings(hz, lsb ? LSBFIRST : MSBFIRST, mode));
    if (!hwcs) {
        digitalWrite(cs, LOW);
    }
    delayMicroseconds(50);             /* the probe's CS set-up */
    for (size_t i = 0; i < n; i++) {
        out[i] = bus->transfer(out[i]);
    }
    const uint16_t in = bus->transfer16(word);
    delayMicroseconds(10);
    if (!hwcs) {
        digitalWrite(cs, HIGH);
    }
    bus->endTransaction();
    CONSOLE.print("XFER ");
    for (size_t i = 0; i < n; i++) {
        hex(out[i], 2);
    }
    CONSOLE.print(' ');
    hex(in, 4);
    CONSOLE.println();
}

/* The buffer transfers (`blk`). The asynchronous ones are the bus object's
 * own (SPI's DMA, or SoftSPI's done-at-once), not SPIClass's. A member of a
 * struct, so the .ino's generated prototypes leave the template alone. */
struct BlockCommands {
template <typename Bus> static bool block(Bus &b, char how, uint8_t *out, uint8_t *in, size_t n)
{
    switch (how) {
    case 'w': b.writeBytes(out, n); return true;
    case 't': b.transferBytes(out, in, n); return true;
    case 'b': memcpy(in, out, n); b.transfer(in, n); return true;
    case 'a':
    case 'A':
        if (!b.transferAsync(out, how == 'A' ? in : nullptr, n)) {
            return false;
        }
        while (!b.finishedAsync()) {
        }
        return true;
    }
    return false;
}
};

static void blk(char how, uint32_t hz, const char *bytes)
{
    static uint8_t out[80], in[80];
    const size_t n = unhex(bytes, out, sizeof out);
    memset(in, 0, sizeof in);
    if (bus == &soft) {
        soft.setHalfPeriodUs((uint16_t)(hz ? (500000u + hz - 1u) / hz : 0u));
    }
    bus->beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
    if (!hwcs) {
        digitalWrite(cs, LOW);
    }
    delayMicroseconds(50);
    bool ok;
#if defined(SPI_INTERFACES_COUNT)
    ok = bus == &soft ? BlockCommands::block(soft, how, out, in, n)
                      : BlockCommands::block(SPI, how, out, in, n);
#else
    ok = BlockCommands::block(soft, how, out, in, n);
#endif
    delayMicroseconds(10);
    if (!hwcs) {
        digitalWrite(cs, HIGH);
    }
    bus->endTransaction();
    CONSOLE.print("BLK ");
    CONSOLE.print(ok ? 1 : 0);
    CONSOLE.print(' ');
    if (how == 'w' || how == 'a') {
        CONSOLE.print('-');
    } else {
        for (size_t i = 0; i < n; i++) {
            hex(in[i], 2);
        }
    }
    CONSOLE.println();
}

#if defined(SPI_INTERFACES_COUNT)
static CH32RVSPISlave slave;
#endif
static SoftSPISlave softSlave;
static_assert(SoftSPISlave::QUEUE_DEPTH == 4, "one ring for both slaves");
/* One tx and one rx buffer per queue slot, used round the ring as queue() takes them. */
static uint8_t slave_tx[SoftSPISlave::QUEUE_DEPTH][16], slave_rx[SoftSPISlave::QUEUE_DEPTH][16];
static uint8_t slave_next;

/* The same commands on either slave: both have CH32RVSPISlave's API. Members
 * of a struct, so the .ino's generated prototypes leave the templates alone. */
struct SlaveCommands {
template <typename Slave> static void queue(Slave &s, char *p)
{
    size_t size = strtoul(p, &p, 10);
    while (*p == ' ') {
        p++;
    }
    if (size > sizeof slave_rx[0]) {
        size = sizeof slave_rx[0];
    }
    uint8_t *tx = slave_tx[slave_next], *rx = slave_rx[slave_next];
    memset(tx, 0, sizeof slave_tx[0]);
    memset(rx, 0, sizeof slave_rx[0]);
    unhex(p, tx, size);
    const bool ok = s.queue(tx, rx, size);
    if (ok) {
        slave_next = (uint8_t)((slave_next + 1) % SoftSPISlave::QUEUE_DEPTH);
    }
    CONSOLE.print("SQ ");
    CONSOLE.println(ok ? 1 : 0);
}

template <typename Slave> static void result(Slave &s)
{
    typename Slave::Result r;
    CONSOLE.print("SRES ");
    if (s.poll(r)) {
        CONSOLE.print((unsigned long)r.length);
        CONSOLE.print(' ');
        for (size_t i = 0; i < r.length && i < r.size; i++) {
            hex(r.rx[i], 2);
        }
        if (!r.length) {
            CONSOLE.print('-');
        }
    } else {
        CONSOLE.print("NONE -");
    }
    CONSOLE.print(' ');
    CONSOLE.print((unsigned long)s.missed());
    CONSOLE.print(' ');
    CONSOLE.println((unsigned long)s.overruns());
}
};

static void peripheral(uint8_t mode, bool lsb, size_t n, const char *bytes)
{
    uint8_t data[16];
    if (n > sizeof data) {
        n = sizeof data;
    }
    memset(data, 0, sizeof data);
    unhex(bytes, data, n);
    if (cs >= 0) {
        pinMode(cs, INPUT);            /* the master's CS: not driven here */
    }
    bus->beginTransaction(SPISettings(1000000, lsb ? LSBFIRST : MSBFIRST, mode, SPI_PERIPHERAL));
    CONSOLE.println("PERIPH READY");
    CONSOLE.flush();
    for (size_t i = 0; i < n; i++) {
        data[i] = bus->transfer(data[i]);
    }
    bus->endTransaction();
    CONSOLE.print("PERIPH ");
    for (size_t i = 0; i < n; i++) {
        hex(data[i], 2);
    }
    CONSOLE.println(n ? "" : "-");
}

static char line[176];
static uint8_t used;

void setup()
{
    CONSOLE_BEGIN(115200);
    CONSOLE.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    CONSOLE.println(CH32RV.resetReasonName());
}

#if CH32RV_HAS_I2S
static I2SClass i2s;
static uint8_t i2sFrames[64];
static size_t i2sLen;
static bool i2sOn;

/* The first CK, WS, SD of one I2S among pins[], as the I2S library pairs
 * them: any on the per-pin families, one route's on the AFIO ones. */
static bool i2sFindIn(const I2SPad *t, size_t n, const long *pins, size_t count, long out[3])
{
    auto listed = [&](long pin) {
        for (size_t i = 0; i < count; i++) {
            if (pins[i] == pin) {
                return true;
            }
        }
        return false;
    };
    for (size_t a = 0; a < n; a++) {
        if (t[a].role != 0 || !listed(t[a].pad)) {
            continue;
        }
        for (size_t b = 0; b < n; b++) {
            for (size_t c = 0; c < n; c++) {
                if (t[b].role != 1 || t[c].role != 2 || !listed(t[b].pad) || !listed(t[c].pad)) {
                    continue;
                }
#if !CH32RV_GPIO_PER_PIN_AF
                if (t[b].value != t[a].value || t[c].value != t[a].value || t[b].value2 != t[a].value2 ||
                    t[c].value2 != t[a].value2) {
                    continue;
                }
#endif
                out[0] = t[a].pad;
                out[1] = t[b].pad;
                out[2] = t[c].pad;
                return true;
            }
        }
    }
    return false;
}

static void i2sFind(char *p)
{
    long pins[32];
    size_t count = 0;
    while (count < 32) {
        char *end;
        const long v = strtol(p, &end, 10);
        if (end == p) {
            break;
        }
        pins[count++] = v;
        p = end;
    }
    long got[3];
    bool ok = false;
#ifdef CH32RV_I2S2_PADS
    static const I2SPad t2[] = CH32RV_I2S2_PADS;
    ok = i2sFindIn(t2, CH32RV_I2S2_PAD_COUNT, pins, count, got);
#endif
#ifdef CH32RV_I2S3_PADS
    static const I2SPad t3[] = CH32RV_I2S3_PADS;
    ok = ok || i2sFindIn(t3, CH32RV_I2S3_PAD_COUNT, pins, count, got);
#endif
    if (!ok) {
        CONSOLE.println("I2SFIND NONE");
        return;
    }
    CONSOLE.print("I2SFIND ");
    CONSOLE.print(got[0]);
    CONSOLE.print(' ');
    CONSOLE.print(got[1]);
    CONSOLE.print(' ');
    CONSOLE.println(got[2]);
}

static void i2sCommand(char *p)
{
    i2s.end();
    i2sOn = false;
    const long ck = strtol(p, &p, 10);
    const long ws = strtol(p, &p, 10);
    const long sd = strtol(p, &p, 10);
    const long bits = strtol(p, &p, 10);
    const unsigned long rate = strtoul(p, &p, 10);
    const long format = strtol(p, &p, 10);
    const long mono = strtol(p, &p, 10);
    while (*p == ' ') {
        p++;
    }
    i2sLen = unhex(p, i2sFrames, sizeof i2sFrames);
    i2s.setPins((int)ck, (int)ws, (int)sd);
    i2s.setFormat((CH32RVI2SFormat)format);
    const bool ok = i2sLen && i2s.begin(I2S_MODE_STD, (uint32_t)rate, (i2s_data_bit_width_t)bits,
                                        mono ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO);
    i2sOn = ok;
    CONSOLE.print("I2S ");
    CONSOLE.print(ok ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.print((unsigned long)i2s.sampleRate());
    CONSOLE.print(' ');
    CONSOLE.println(i2s.lastError());
}

static void i2sReceive(char *p)
{
    i2s.end();
    i2sOn = false;
    const long ck = strtol(p, &p, 10);
    const long ws = strtol(p, &p, 10);
    const long sd = strtol(p, &p, 10);
    const long bits = strtol(p, &p, 10);
    const unsigned long rate = strtoul(p, &p, 10);
    const unsigned long want = strtoul(p, &p, 10);
    i2s.setPins((int)ck, (int)ws, -1, (int)sd);
    i2s.setFormat(I2S_FORMAT_PHILIPS);
    const bool ok = i2s.begin(I2S_MODE_STD, (uint32_t)rate, (i2s_data_bit_width_t)bits, I2S_SLOT_MODE_STEREO);
    unsigned long got = 0, ones = 0, zeros = 0, mixed = 0, rises = 0;
    const size_t sb = (size_t)bits / 8u;
    int last = -1;
    while (ok && got < want) {
        uint8_t frame[8];
        if (i2s.readBytes(frame, 2 * sb) != 2 * sb) {
            break;
        }
        got++;
        for (uint8_t s = 0; s < 2; s++) {
            bool all1 = true, all0 = true;
            for (size_t k = 0; k < sb; k++) {
                all1 = all1 && frame[s * sb + k] == 0xFF;
                all0 = all0 && frame[s * sb + k] == 0x00;
            }
            ones += all1;
            zeros += all0;
            mixed += !all1 && !all0;
            if (s == 0 && (all1 || all0)) {
                if (last == 0 && all1) {
                    rises++;
                }
                last = all1;
            }
        }
    }
    const unsigned long overruns = i2s.overruns();
    const unsigned long actual = i2s.sampleRate();
    /* left running (the lines driven) until the next i2s command or i2send:
     * undriven, WS and BCLK pick up the lines beside them */
    CONSOLE.print("I2SRX ");
    CONSOLE.print(ok ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.print(actual);
    CONSOLE.print(' ');
    CONSOLE.print(got);
    CONSOLE.print(' ');
    CONSOLE.print(ones);
    CONSOLE.print(' ');
    CONSOLE.print(zeros);
    CONSOLE.print(' ');
    CONSOLE.print(mixed);
    CONSOLE.print(' ');
    CONSOLE.print(rises);
    CONSOLE.print(' ');
    CONSOLE.println(overruns);
}

/* The frames again whenever a whole copy fits: write() never waits here. */
static void i2sService()
{
    while (i2sOn && i2s.availableForWrite() >= (int)i2sLen) {
        i2s.write(i2sFrames, i2sLen);
    }
}
#endif

void loop()
{
#if CH32RV_HAS_I2S
    i2sService();
#endif
    while (CONSOLE.available()) {
        const int ch = CONSOLE.read();
        if (ch != '\n') {
            if (ch != '\r' && used < sizeof line - 1) {
                line[used++] = (char)ch;
            }
            continue;
        }
        line[used] = 0;
        used = 0;
        char *p = line;
        if (strncmp(line, "ready ", 6) == 0) {
            CONSOLE.print("READY ");
            CONSOLE.print(line + 6);
            CONSOLE.print(' ');
            CONSOLE.println(BENCH_BUILD_ID);
        } else if (strncmp(line, "pins ", 5) == 0) {
            p += 5;
            const long sck = strtol(p, &p, 10);
            const long miso = strtol(p, &p, 10);
            const long mosi = strtol(p, &p, 10);
            cs = (int)strtol(p, &p, 10);
            pinMode(cs, OUTPUT);
            digitalWrite(cs, HIGH);
            softPins[0] = (int)sck;
            softPins[1] = (int)miso;
            softPins[2] = (int)mosi;
            CONSOLE.print("PINS ");
            CONSOLE.println(soft.begin((int)sck, (int)miso, (int)mosi) ? 1 : 0);
        } else if (strncmp(line, "shift ", 6) == 0) {
            p += 6;
            const BitOrder order = strtol(p, &p, 10) ? LSBFIRST : MSBFIRST;
            while (*p == ' ') {
                p++;
            }
            static uint8_t out[40], in[40];
            char *count = strchr(p, ' ');
            if (count) {
                *count++ = '\0';                  /* unhex() would read on into the count */
            }
            const size_t n = unhex(p, out, sizeof out);
            size_t k = count ? strtoul(count, nullptr, 10) : 0;
            k = k > sizeof in ? sizeof in : k;
            if (softPins[0] < 0) {
                CONSOLE.println("SHIFT NONE");
            } else {
                soft.end();
                pinMode(softPins[0], OUTPUT);
                digitalWrite(softPins[0], LOW);
                pinMode(softPins[2], OUTPUT);
                pinMode(softPins[1], INPUT);
                digitalWrite(cs, LOW);
                delayMicroseconds(50);
                for (size_t i = 0; i < n; i++) {
                    shiftOut(softPins[2], softPins[0], order, out[i]);
                }
                for (size_t i = 0; i < k; i++) {
                    in[i] = shiftIn(softPins[1], softPins[0], order);
                }
                delayMicroseconds(50);
                digitalWrite(cs, HIGH);
                soft.begin(softPins[0], softPins[1], softPins[2]);
                CONSOLE.print("SHIFT ");
                for (size_t i = 0; i < k; i++) {
                    hex(in[i], 2);
                }
                CONSOLE.println();
            }
        } else if (strncmp(line, "hwcs ", 5) == 0) {
#if defined(SPI_INTERFACES_COUNT) && !CH32RV_SPI_SOFT_N(CH32RV_SPI_ROW0_N)
            const bool want = line[5] == '1';
            if (want) {
                SPI.begin(-1, -1, -1, -1);
            }
            SPI.setHwCs(want);
            hwcs = SPI.hwCs();
            CONSOLE.print("HWCS ");
            CONSOLE.print(hwcs ? 1 : 0);
            CONSOLE.print(' ');
#ifdef PIN_SPI_SS
            CONSOLE.println(want && hwcs ? (unsigned)PIN_SPI_SS : 0u);
#else
            CONSOLE.println(0u);
#endif
#else
            CONSOLE.println("HWCS 0 0");
#endif
        } else if (strncmp(line, "blk ", 4) == 0) {
            const char how = line[4];
            p += 6;
            const uint32_t hz = strtoul(p, &p, 10);
            while (*p == ' ') {
                p++;
            }
            blk(how, hz, p);
        } else if (strcmp(line, "spipins") == 0) {
#if defined(SPI_INTERFACES_COUNT)
            CONSOLE.print("SPIPINS ");
            CONSOLE.print(PIN_SPI_SCK);
            CONSOLE.print(' ');
            CONSOLE.print(PIN_SPI_MISO);
            CONSOLE.print(' ');
            CONSOLE.println(PIN_SPI_MOSI);
#else
            CONSOLE.println("SPIPINS NONE");
#endif
        } else if (strncmp(line, "tw ", 3) == 0) {
            /* not on V003: its 16 KB took the CRC, not this as well (400 B over) */
#if defined(SPI_INTERFACES_COUNT) && !CH32RV_SPI_SOFT_N(CH32RV_SPI_ROW0_N) && !defined(CH32RV_VARIANT_CH32V003)
            p += 3;
            const uint32_t hz = strtoul(p, &p, 10);
            while (*p == ' ') {
                p++;
            }
            char *count = strchr(p, ' ');
            if (count) {
                *count++ = '\0';
            }
            uint8_t out[16], in[16];
            const size_t n = unhex(p, out, sizeof out);
            size_t k = count ? strtoul(count, nullptr, 10) : 0;
            k = k > sizeof in ? sizeof in : k;
            bool ok = SPI.setThreeWire(true);
            SPI.beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
            digitalWrite(cs, LOW);
            delayMicroseconds(50);
            SPI.transfer(out, n);
            ok = ok && SPI.receive(in, k);
            delayMicroseconds(10);
            digitalWrite(cs, HIGH);
            SPI.endTransaction();
            SPI.setThreeWire(false);
            CONSOLE.print("TW ");
            CONSOLE.print(ok ? 1 : 0);
            CONSOLE.print(' ');
            for (size_t i = 0; i < k; i++) {
                hex(in[i], 2);
            }
            CONSOLE.println(k ? "" : "-");
#else
            CONSOLE.println("TW 2 -");
#endif
        } else if (strncmp(line, "crc ", 4) == 0) {
#if defined(SPI_INTERFACES_COUNT) && !CH32RV_SPI_SOFT_N(CH32RV_SPI_ROW0_N)
            p += 4;
            const uint8_t poly = (uint8_t)strtoul(p, &p, 16);
            while (*p == ' ') {
                p++;
            }
            uint8_t data[16];
            const size_t n = unhex(p, data, sizeof data);
            SPI.setCRC(poly);
            SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
            digitalWrite(cs, LOW);
            delayMicroseconds(50);
            const bool ok = SPI.transferCRC(data, n);
            delayMicroseconds(10);
            digitalWrite(cs, HIGH);
            SPI.endTransaction();
            CONSOLE.print("CRC ");
            CONSOLE.print(ok ? 1 : 0);
            CONSOLE.print(' ');
            hex(SPI.crcSent(), 2);
            CONSOLE.print(' ');
            hex(SPI.crcReceived(), 2);
            CONSOLE.print(' ');
            for (size_t i = 0; i < n; i++) {
                hex(data[i], 2);
            }
            CONSOLE.println();
#else
            CONSOLE.println("CRC 2 00 00 -");
#endif
        } else if (strncmp(line, "hw ", 3) == 0) {
#if defined(SPI_INTERFACES_COUNT)
            p += 3;
            cs = (int)strtol(p, &p, 10);
            pinMode(cs, OUTPUT);
            digitalWrite(cs, HIGH);
            SPI.begin();
            bus = &SPI;
            CONSOLE.print("HW ");
            CONSOLE.print(PIN_SPI_SCK);
            CONSOLE.print(' ');
            CONSOLE.print(PIN_SPI_MISO);
            CONSOLE.print(' ');
            CONSOLE.println(PIN_SPI_MOSI);
#else
            CONSOLE.println("HW NONE");   /* the part has no SPI (CH32RV_SPI1_ABSENT); SoftSPI still runs */
#endif
        } else if (strncmp(line, "slave ", 6) == 0) {
            p += 6;
            const int scs = (int)strtol(p, &p, 10);
            const unsigned long mode = strtoul(p, &p, 10);
            const unsigned long lsb = strtoul(p, &p, 10);
            const unsigned long fill = strtoul(p, &p, 16);
            const BitOrder order = lsb ? LSBFIRST : MSBFIRST;
            bool ok = false;
            slave_next = 0;
            if (softPins[0] >= 0) {
                soft.end();
                softSlave.end();
                ok = softSlave.begin(softPins[0], softPins[1], softPins[2], scs, (uint8_t)mode, order);
                softSlave.setFill((uint8_t)fill);
            } else {
#if defined(SPI_INTERFACES_COUNT)
                SPI.end();
                slave.end();
                ok = slave.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, scs, (uint8_t)mode, order);
                slave.setFill((uint8_t)fill);
#endif
            }
            bus = &soft;
            CONSOLE.print("SLAVE ");
            CONSOLE.println(ok ? 1 : 0);
        } else if (strncmp(line, "sq ", 3) == 0) {
            if (softPins[0] >= 0) {
                SlaveCommands::queue(softSlave, p + 3);
            } else {
#if defined(SPI_INTERFACES_COUNT)
                SlaveCommands::queue(slave, p + 3);
#endif
            }
        } else if (strcmp(line, "sres") == 0) {
            if (softPins[0] >= 0) {
                SlaveCommands::result(softSlave);
            } else {
#if defined(SPI_INTERFACES_COUNT)
                SlaveCommands::result(slave);
#endif
            }
        } else if (strcmp(line, "send") == 0) {
            softSlave.end();
#if defined(SPI_INTERFACES_COUNT)
            slave.end();
#endif
            CONSOLE.println("SEND");
        } else if (strncmp(line, "i2sfind ", 8) == 0) {
#if CH32RV_HAS_I2S
            i2sFind(p + 8);
#else
            CONSOLE.println("I2SFIND NONE");
#endif
        } else if (strncmp(line, "i2s ", 4) == 0) {
#if CH32RV_HAS_I2S
            i2sCommand(p + 4);
#else
            CONSOLE.println("I2S NONE");
#endif
        } else if (strncmp(line, "i2srx ", 6) == 0) {
#if CH32RV_HAS_I2S
            i2sReceive(p + 6);
#else
            CONSOLE.println("I2SRX 0 0 0 0 0 0 0 0");
#endif
        } else if (strcmp(line, "i2stat") == 0) {
            CONSOLE.print("I2STAT ");
#if CH32RV_HAS_I2S
            CONSOLE.println((unsigned long)i2s.underruns());
#else
            CONSOLE.println(0);
#endif
        } else if (strcmp(line, "i2send") == 0) {
#if CH32RV_HAS_I2S
            i2s.end();
            i2sOn = false;
#endif
            CONSOLE.println("I2SEND");
        } else if (strncmp(line, "periph ", 7) == 0) {
            p += 7;
            const unsigned long mode = strtoul(p, &p, 10);
            const unsigned long lsb = strtoul(p, &p, 10);
            const unsigned long n = strtoul(p, &p, 10);
            while (*p == ' ') {
                p++;
            }
            peripheral((uint8_t)mode, lsb != 0, n, p);
        } else if (strncmp(line, "xfer ", 5) == 0) {
            p += 5;
            const unsigned long mode = strtoul(p, &p, 10);
            const unsigned long lsb = strtoul(p, &p, 10);
            const unsigned long hz = strtoul(p, &p, 10);
            const unsigned long word = strtoul(p, &p, 16);
            while (*p == ' ') {
                p++;
            }
            xfer((uint8_t)mode, lsb != 0, (uint32_t)hz, (uint16_t)word, p);
        }
    }
}

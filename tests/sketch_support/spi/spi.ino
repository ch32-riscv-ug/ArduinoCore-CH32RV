/* SPI contracts (tests/instrumented/spi): the hardware SPI on its default
 * pins, or a SoftSPI on the bench's pads, as the controller of an OEP probe's
 * spi-target, CS an ordinary GPIO. Console on SerialDMSeq:
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
 * The bus itself as a peripheral (SPI_HAS_PERIPHERAL_MODE): SPI after `hw`, SoftSPI after
 * `pins`, CS left to the master (the DUT's CS pin an input):
 *
 *   periph <mode> <lsb> <n> <tx hex>   beginTransaction(SPISettings(1000000, order, mode,
 *                                      SPI_PERIPHERAL)), PERIPH READY, then transfer() n bytes
 *                                      as the master clocks them, endTransaction():
 *                                      PERIPH <bytes read, hex> */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <SPI.h>
#if defined(SPI_INTERFACES_COUNT)
#include <CH32RVSPISlave.h>
#endif
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
        SerialDMSeq.print((char)"0123456789abcdef"[(v >> (digits * 4)) & 0xFu]);
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
    SerialDMSeq.print("XFER ");
    for (size_t i = 0; i < n; i++) {
        hex(out[i], 2);
    }
    SerialDMSeq.print(' ');
    hex(in, 4);
    SerialDMSeq.println();
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
    SerialDMSeq.print("BLK ");
    SerialDMSeq.print(ok ? 1 : 0);
    SerialDMSeq.print(' ');
    if (how == 'w' || how == 'a') {
        SerialDMSeq.print('-');
    } else {
        for (size_t i = 0; i < n; i++) {
            hex(in[i], 2);
        }
    }
    SerialDMSeq.println();
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
    SerialDMSeq.print("SQ ");
    SerialDMSeq.println(ok ? 1 : 0);
}

template <typename Slave> static void result(Slave &s)
{
    typename Slave::Result r;
    SerialDMSeq.print("SRES ");
    if (s.poll(r)) {
        SerialDMSeq.print((unsigned long)r.length);
        SerialDMSeq.print(' ');
        for (size_t i = 0; i < r.length && i < r.size; i++) {
            hex(r.rx[i], 2);
        }
        if (!r.length) {
            SerialDMSeq.print('-');
        }
    } else {
        SerialDMSeq.print("NONE -");
    }
    SerialDMSeq.print(' ');
    SerialDMSeq.print((unsigned long)s.missed());
    SerialDMSeq.print(' ');
    SerialDMSeq.println((unsigned long)s.overruns());
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
    SerialDMSeq.println("PERIPH READY");
    SerialDMSeq.flush();
    for (size_t i = 0; i < n; i++) {
        data[i] = bus->transfer(data[i]);
    }
    bus->endTransaction();
    SerialDMSeq.print("PERIPH ");
    for (size_t i = 0; i < n; i++) {
        hex(data[i], 2);
    }
    SerialDMSeq.println(n ? "" : "-");
}

static char line[176];
static uint8_t used;

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    SerialDMSeq.println(CH32RV.resetReasonName());
}

void loop()
{
    while (SerialDMSeq.available()) {
        const int ch = SerialDMSeq.read();
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
            SerialDMSeq.print("READY ");
            SerialDMSeq.print(line + 6);
            SerialDMSeq.print(' ');
            SerialDMSeq.println(BENCH_BUILD_ID);
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
            SerialDMSeq.print("PINS ");
            SerialDMSeq.println(soft.begin((int)sck, (int)miso, (int)mosi) ? 1 : 0);
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
                SerialDMSeq.println("SHIFT NONE");
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
                SerialDMSeq.print("SHIFT ");
                for (size_t i = 0; i < k; i++) {
                    hex(in[i], 2);
                }
                SerialDMSeq.println();
            }
        } else if (strncmp(line, "hwcs ", 5) == 0) {
#if defined(SPI_INTERFACES_COUNT) && !CH32RV_SPI_SOFT_N(CH32RV_SPI_ROW0_N)
            const bool want = line[5] == '1';
            if (want) {
                SPI.begin(-1, -1, -1, -1);
            }
            SPI.setHwCs(want);
            hwcs = SPI.hwCs();
            SerialDMSeq.print("HWCS ");
            SerialDMSeq.print(hwcs ? 1 : 0);
            SerialDMSeq.print(' ');
#ifdef PIN_SPI_SS
            SerialDMSeq.println(want && hwcs ? (unsigned)PIN_SPI_SS : 0u);
#else
            SerialDMSeq.println(0u);
#endif
#else
            SerialDMSeq.println("HWCS 0 0");
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
            SerialDMSeq.print("SPIPINS ");
            SerialDMSeq.print(PIN_SPI_SCK);
            SerialDMSeq.print(' ');
            SerialDMSeq.print(PIN_SPI_MISO);
            SerialDMSeq.print(' ');
            SerialDMSeq.println(PIN_SPI_MOSI);
#else
            SerialDMSeq.println("SPIPINS NONE");
#endif
        } else if (strncmp(line, "hw ", 3) == 0) {
#if defined(SPI_INTERFACES_COUNT)
            p += 3;
            cs = (int)strtol(p, &p, 10);
            pinMode(cs, OUTPUT);
            digitalWrite(cs, HIGH);
            SPI.begin();
            bus = &SPI;
            SerialDMSeq.print("HW ");
            SerialDMSeq.print(PIN_SPI_SCK);
            SerialDMSeq.print(' ');
            SerialDMSeq.print(PIN_SPI_MISO);
            SerialDMSeq.print(' ');
            SerialDMSeq.println(PIN_SPI_MOSI);
#else
            SerialDMSeq.println("HW NONE");   /* the part has no SPI (CH32RV_SPI1_ABSENT); SoftSPI still runs */
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
            SerialDMSeq.print("SLAVE ");
            SerialDMSeq.println(ok ? 1 : 0);
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
            SerialDMSeq.println("SEND");
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

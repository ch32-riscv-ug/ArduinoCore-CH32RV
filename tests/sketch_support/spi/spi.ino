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
 * CH32RVSPISlave on the SPI's default pins (the probe's spi-controller is the master):
 *
 *   slave <cs> <mode> <lsb> <fill>     the SPI given back (SPI.end()), the slave begun with CS
 *                                      on <cs> and <fill> (hex) once tx runs out: SLAVE <1 / 0>
 *   sq <size> <tx bytes, hex>          queue(tx, rx, size): SQ <1 / 0>
 *   sres                               the oldest result, and the counters:
 *                                      SRES <length> <rx bytes kept, hex> <missed> <overruns>
 *                                      (SRES NONE - <missed> <overruns> when none)
 *   send                               slave.end(): SEND */
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

static void xfer(uint8_t mode, bool lsb, uint32_t hz, uint16_t word, const char *bytes)
{
    uint8_t out[16];
    const size_t n = unhex(bytes, out, sizeof out);
    bus->beginTransaction(SPISettings(hz, lsb ? LSBFIRST : MSBFIRST, mode));
    digitalWrite(cs, LOW);
    delayMicroseconds(50);             /* the probe's CS set-up */
    for (size_t i = 0; i < n; i++) {
        out[i] = bus->transfer(out[i]);
    }
    const uint16_t in = bus->transfer16(word);
    delayMicroseconds(10);
    digitalWrite(cs, HIGH);
    bus->endTransaction();
    SerialDMSeq.print("XFER ");
    for (size_t i = 0; i < n; i++) {
        hex(out[i], 2);
    }
    SerialDMSeq.print(' ');
    hex(in, 4);
    SerialDMSeq.println();
}

#if defined(SPI_INTERFACES_COUNT)
static CH32RVSPISlave slave;
/* One tx and one rx buffer per queue slot, used round the ring as queue() takes them. */
static uint8_t slave_tx[CH32RVSPISlave::QUEUE_DEPTH][16], slave_rx[CH32RVSPISlave::QUEUE_DEPTH][16];
static uint8_t slave_next;
#endif

static char line[64];
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
            SerialDMSeq.print("PINS ");
            SerialDMSeq.println(soft.begin((int)sck, (int)miso, (int)mosi) ? 1 : 0);
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
#if defined(SPI_INTERFACES_COUNT)
        } else if (strncmp(line, "slave ", 6) == 0) {
            p += 6;
            const int scs = (int)strtol(p, &p, 10);
            const unsigned long mode = strtoul(p, &p, 10);
            const unsigned long lsb = strtoul(p, &p, 10);
            const unsigned long fill = strtoul(p, &p, 16);
            SPI.end();
            bus = &soft;
            slave.end();
            slave_next = 0;
            const bool ok = slave.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, scs, (uint8_t)mode, lsb ? LSBFIRST : MSBFIRST);
            slave.setFill((uint8_t)fill);
            SerialDMSeq.print("SLAVE ");
            SerialDMSeq.println(ok ? 1 : 0);
        } else if (strncmp(line, "sq ", 3) == 0) {
            p += 3;
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
            const bool ok = slave.queue(tx, rx, size);
            if (ok) {
                slave_next = (uint8_t)((slave_next + 1) % CH32RVSPISlave::QUEUE_DEPTH);
            }
            SerialDMSeq.print("SQ ");
            SerialDMSeq.println(ok ? 1 : 0);
        } else if (strcmp(line, "sres") == 0) {
            CH32RVSPISlave::Result r;
            SerialDMSeq.print("SRES ");
            if (slave.poll(r)) {
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
            SerialDMSeq.print((unsigned long)slave.missed());
            SerialDMSeq.print(' ');
            SerialDMSeq.println((unsigned long)slave.overruns());
        } else if (strcmp(line, "send") == 0) {
            slave.end();
            SerialDMSeq.println("SEND");
#endif
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

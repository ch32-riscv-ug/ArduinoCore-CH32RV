/* The buses with no wiring (tests/instrumented/selfbus), selfcheck's sibling -
 * selfcheck no longer fits CH32V003's 16 KB with these too. Console on
 * SerialDMSeq:
 *
 *   ready <nonce>             READY <nonce> <build id>
 *   spipins                   SPIPINS <SPIn> <SCK> <MISO> <MOSI> <each held: 0 / 1>, read before SPI.begin()
 *                             drives anything; SPIPINS 0 -1 -1 -1 0 0 0 without a hardware SPI
 *   spiout <mode> <lsb> <byte> one byte sent at the slowest clock while the sketch reads SCK and MOSI on their
 *                             own pads: SPIOUT <SCK idle level> <edges seen> <byte rebuilt from MOSI at the
 *                             mode's sampling edge>
 *   spiin <level>             MISO made a GPIO output at level (0 / 1), one byte exchanged:
 *                             SPIIN <byte received>, or SPIIN HELD when the pulls do not move MISO */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <stdlib.h>
#include <string.h>
#include <SPI.h>
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

#if CH32RV_SPI_COUNT > 0 && !CH32RV_SPI_SOFT_N(CH32RV_SPI_ROW0_N) && defined(PIN_SPI_SCK)
#define SELFBUS_SPI 1
#endif

static char line[64];
static uint8_t used;

/* Whether something on the board holds the pad (selfcheck.ino's). */
static bool held(pin_size_t p)
{
    pinMode(p, INPUT_PULLUP);
    delay(1);
    const int up = digitalRead(p);
    pinMode(p, INPUT_PULLDOWN);
    delay(1);
    const int down = digitalRead(p);
    pinMode(p, INPUT);
    return up == down;
}

#if SELFBUS_SPI
static uint32_t spi_base(unsigned number)
{
    switch (number) {
    case 1: return CH32RV_SPI1_BASE;
#ifdef CH32RV_SPI2_BASE
    case 2: return CH32RV_SPI2_BASE;
#endif
#ifdef CH32RV_SPI3_BASE
    case 3: return CH32RV_SPI3_BASE;
#endif
    default: return 0;
    }
}

/* One byte at the slowest divider, started by writing DATAR so the sketch is
 * free to watch: SCK and MOSI read on their pads (the input path stays on
 * with the alternate function) until the peripheral is idle again. MOSI is
 * taken at each edge of the mode's sampling side: the leading edge with
 * CPHA 0, the trailing one with CPHA 1. */
static void spi_out(int mode, int lsb, int value)
{
    SPI.begin();
    SPI.beginTransaction(SPISettings(1000, lsb ? LSBFIRST : MSBFIRST, static_cast<SPIMode>(mode)));
    const uint32_t base = spi_base(SPI.peripheral());
    const int idle = digitalRead(PIN_SPI_SCK);
    const int cpol = (mode >> 1) & 1, cpha = mode & 1;
    /* The level SCK goes to at a sampling edge: leading (away from idle) with
     * CPHA 0, trailing (back to idle) with CPHA 1. */
    const int sample_level = cpha ? cpol : !cpol;
    while (CH32RV_SPI_STATR(base) & CH32RV_SPI_STATR_RXNE) {
        (void)CH32RV_SPI_DATAR(base);
    }
    /* The input registers read straight: digitalRead() twice and micros() a
     * turn missed edges at X035's 187 kHz and L103's 375 kHz. */
    const uint8_t sck_port = (uint8_t)CH32RV_PIN_PORT(PIN_SPI_SCK), sck_bit = (uint8_t)CH32RV_PIN_BIT(PIN_SPI_SCK);
    const uint8_t mosi_port = (uint8_t)CH32RV_PIN_PORT(PIN_SPI_MOSI), mosi_bit = (uint8_t)CH32RV_PIN_BIT(PIN_SPI_MOSI);
    volatile uint32_t *const sck_in = &CH32RV_GPIO_INDR(sck_port);
    volatile uint32_t *const mosi_in = &CH32RV_GPIO_INDR(mosi_port);
    int edges = 0, bits = 0;
    uint32_t got = 0;
    uint32_t prev = (uint32_t)idle;
    noInterrupts();
    CH32RV_SPI_DATAR(base) = (uint16_t)value;
    for (uint32_t n = 0; n < 2000000u && edges < 16; n++) {
        const uint32_t sck = (*sck_in >> sck_bit) & 1u;
        const uint32_t mosi = (*mosi_in >> mosi_bit) & 1u;
        if (sck != prev) {
            edges++;
            if ((int)sck == sample_level && bits < 16) {
                got |= mosi << bits;
                bits++;
            }
            prev = sck;
        }
    }
    while (!(CH32RV_SPI_STATR(base) & CH32RV_SPI_STATR_RXNE)) {
    }
    interrupts();
    (void)CH32RV_SPI_DATAR(base);
    SPI.endTransaction();
    SPI.end();
    /* got holds the bits in the order they went out. */
    uint32_t byte = 0;
    for (int i = 0; i < 8 && i < bits; i++) {
        const uint32_t bit = (got >> i) & 1u;
        byte |= lsb ? bit << i : bit << (7 - i);
    }
    SerialDMSeq.printf("SPIOUT %d %d %lu\r\n", idle, edges, (unsigned long)byte);
}

static void spi_in(int level)
{
    if (held((pin_size_t)PIN_SPI_MISO)) {
        SerialDMSeq.printf("SPIIN HELD\r\n");
        return;
    }
    SPI.begin();
    digitalWrite(PIN_SPI_MISO, level ? HIGH : LOW);
    pinMode(PIN_SPI_MISO, OUTPUT);              /* the SPI still hears the pad */
    digitalWrite(PIN_SPI_MISO, level ? HIGH : LOW);
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    const uint8_t got = SPI.transfer(0x5A);
    SPI.endTransaction();
    pinMode(PIN_SPI_MISO, INPUT);
    SPI.end();
    SerialDMSeq.printf("SPIIN %u\r\n", got);
}
#endif

static void reply(char *command)
{
    char *argv[5];
    int argc = 0;
    for (char *t = strtok(command, " "); t && argc < 5; t = strtok(nullptr, " ")) {
        argv[argc++] = t;
    }
    if (!argc) {
        return;
    }
    if (!strcmp(argv[0], "ready") && argc == 2) {
        SerialDMSeq.printf("READY %s %s\r\n", argv[1], BENCH_BUILD_ID);
    } else if (!strcmp(argv[0], "spipins")) {
#if SELFBUS_SPI
        /* Held first: SPI.begin() drives SCK and MOSI. */
        const int hs = held(PIN_SPI_SCK), hi = held(PIN_SPI_MISO), ho = held(PIN_SPI_MOSI);
        SPI.begin();
        const unsigned number = SPI.peripheral();
        SPI.end();
        SerialDMSeq.printf("SPIPINS %u %d %d %d %d %d %d\r\n", number, (int)PIN_SPI_SCK, (int)PIN_SPI_MISO,
                           (int)PIN_SPI_MOSI, hs, hi, ho);
#else
        SerialDMSeq.printf("SPIPINS 0 -1 -1 -1 0 0 0\r\n");
#endif
#if SELFBUS_SPI
    } else if (!strcmp(argv[0], "spiout") && argc == 4) {
        spi_out(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
    } else if (!strcmp(argv[0], "spiin") && argc == 2) {
        spi_in(atoi(argv[1]));
#endif
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

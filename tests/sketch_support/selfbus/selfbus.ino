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
 *   wires                     WIRES <hardware TwoWire objects>
 *   i2cinfo <k>               I2CINFO <I2Cn> <SCL> <SDA> <SCL with pull-up> <with pull-down> <SDA up> <down>
 *   i2creg <k> <hz>           setClock(hz), then I2CREG <PCLK1> <CTLR2 FREQ> <CKCFGR>
 *   i2cnack <k>               a write to 0x50 with nobody there: I2CNACK <endTransmission()> <microseconds>
 *   ports                     PORTS <HardwareSerial objects> <receive buffer size>
 *   rxpin <k>                 RXPIN <USARTn> <its default RX pin or -1>
 *   rxfmt <k> <fmt> <kind> <n> port k at 9600 in fmt (8N1 8E1 8O1 7E1 8N2), fed n frames on its RX pad (made an
 *                             output): kind g good, p bad parity, s 0 stop bit, b break, o interrupts off
 *                             throughout (overrun), f never read (the buffer fills):
 *                             RXFMT <USARTn> <received> <matching> <takeErrors()>, or RXFMT <n> HELD / CLOSED
 *   txfmt <k> <fmt> <n>       port k sends n bytes at 115200 in fmt: TXFMT <USARTn> <microseconds>
 *   spiin <level>             MISO made a GPIO output at level (0 / 1), one byte exchanged:
 *                             SPIIN <byte received>, or SPIIN HELD when the pulls do not move MISO */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <stdlib.h>
#include <string.h>
#include <SPI.h>
#include <Wire.h>
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

/* Up here, before the prototypes the Arduino build puts in: a UART format.
 * name -> config, data bits, parity (0 none, 1 even, 2 odd), stop bits */
struct Format {
    const char *name;
    uint16_t config;
    uint8_t data, parity, stop;
};

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

/* ---- UART: frame formats and receive errors ---- */
static arduino::CH32RVHardwareSerial *const ports[] = {
#if CH32RV_SERIAL_COUNT >= 1
    &Serial,
#endif
#if CH32RV_SERIAL_COUNT >= 2
    &Serial1,
#endif
#if CH32RV_SERIAL_COUNT >= 3
    &Serial2,
#endif
#if CH32RV_SERIAL_COUNT >= 4
    &Serial3,
#endif
#if CH32RV_SERIAL_COUNT >= 5
    &Serial4,
#endif
#if CH32RV_SERIAL_COUNT >= 6
    &Serial5,
#endif
#if CH32RV_SERIAL_COUNT >= 7
    &Serial6,
#endif
#if CH32RV_SERIAL_COUNT >= 8
    &Serial7,
#endif
#if CH32RV_SERIAL_COUNT >= 9
    &Serial8,
#endif
#if CH32RV_SERIAL_COUNT >= 10
    &Serial9,
#endif
    nullptr,
};
static const int PORT_COUNT = (int)(sizeof ports / sizeof ports[0]) - 1;

static int rx_pin(uint8_t n)
{
    switch (n) {
#ifdef CH32RV_SERIAL1_RX
    case 1: return CH32RV_SERIAL1_RX;
#endif
#ifdef CH32RV_SERIAL2_RX
    case 2: return CH32RV_SERIAL2_RX;
#endif
#ifdef CH32RV_SERIAL3_RX
    case 3: return CH32RV_SERIAL3_RX;
#endif
#ifdef CH32RV_SERIAL4_RX
    case 4: return CH32RV_SERIAL4_RX;
#endif
#ifdef CH32RV_SERIAL5_RX
    case 5: return CH32RV_SERIAL5_RX;
#endif
#ifdef CH32RV_SERIAL6_RX
    case 6: return CH32RV_SERIAL6_RX;
#endif
#ifdef CH32RV_SERIAL7_RX
    case 7: return CH32RV_SERIAL7_RX;
#endif
#ifdef CH32RV_SERIAL8_RX
    case 8: return CH32RV_SERIAL8_RX;
#endif
    default: return -1;
    }
}

static const Format formats[] = {
    {"8N1", SERIAL_8N1, 8, 0, 1}, {"8E1", SERIAL_8E1, 8, 1, 1}, {"8O1", SERIAL_8O1, 8, 2, 1},
    {"7E1", SERIAL_7E1, 7, 1, 1}, {"8N2", SERIAL_8N2, 8, 0, 2},
};

static const Format *format_named(const char *name)
{
    for (const Format &f : formats) {
        if (!strcmp(f.name, name)) {
            return &f;
        }
    }
    return nullptr;
}

static uint8_t pattern(int i) { return (uint8_t)(0x55u ^ (uint8_t)(i * 37u)); }

/* One frame on the pad, LSB first, timed by micros() from its start bit:
 * start, data, the parity bit (flipped when `bad` is 'p'), the stop bits
 * (0 when `bad` is 's'); `bad` 'b' sends every bit 0 - a break. */
static void frame(pin_size_t pin, const Format &f, uint8_t value, char bad, uint32_t bit_us)
{
    uint8_t bits[13];
    uint8_t n = 0;
    bits[n++] = 0;
    uint8_t ones = 0;
    for (uint8_t i = 0; i < f.data; i++) {
        const uint8_t b = (value >> i) & 1u;
        ones += b;
        bits[n++] = b;
    }
    if (f.parity) {
        uint8_t p = f.parity == 1 ? (ones & 1u) : !(ones & 1u);
        bits[n++] = bad == 'p' ? !p : p;
    }
    for (uint8_t i = 0; i < f.stop; i++) {
        bits[n++] = bad == 's' ? 0 : 1;
    }
    if (bad == 'b') {
        for (uint8_t i = 0; i < n; i++) {
            bits[i] = 0;
        }
    }
    const uint32_t t0 = micros();
    for (uint8_t i = 0; i < n; i++) {
        digitalWrite(pin, bits[i] ? HIGH : LOW);
        while ((uint32_t)(micros() - t0) < (uint32_t)(i + 1u) * bit_us) {
        }
    }
    digitalWrite(pin, HIGH);
    delayMicroseconds(bit_us * 2u);           /* idle between frames, and after a bad stop */
}

/* Port k at 9600 in format `fmt`, fed n frames on its own RX pad (made an
 * output; the USART still hears it). kind: 'g' good frames, 'p' bad parity,
 * 's' a 0 stop bit, 'b' breaks, 'o' n good frames with interrupts off
 * throughout (an overrun), 'f' n good frames not read (the buffer fills).
 *   RXFMT <USARTn> <received> <matching> <takeErrors()>, RXFMT <n> HELD / CLOSED */
static void rx_format(int k, const Format &f, char kind, int n)
{
    arduino::CH32RVHardwareSerial &port = *ports[k];
    port.begin(9600, f.config);
    const uint8_t number = port.peripheral();
    const int pin = number ? rx_pin(number) : -1;
    if (!port || pin < 0) {
        port.end();
        SerialDMSeq.printf("RXFMT %u CLOSED\r\n", number);
        return;
    }
    const pin_size_t p = (pin_size_t)pin;
    if (held(p)) {
        port.end();
        SerialDMSeq.printf("RXFMT %u HELD\r\n", number);
        return;
    }
    port.begin(9600, f.config);                   /* held() left the pad an input */
    digitalWrite(p, HIGH);
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
    delay(5);
    const uint32_t bit_us = 1000000u / 9600u;
    const uint8_t mask = f.data == 7 ? 0x7Fu : 0xFFu;
    /* A warm-up frame (its first pass through the bit loop runs slow, see
     * selfcheck), dropped with whatever it raised. */
    noInterrupts();
    frame(p, f, mask, 'g', bit_us);
    interrupts();
    delay(2);
    while (port.available()) {
        port.read();
    }
    port.takeErrors();
    int received = 0, matching = 0;
    if (kind == 'o') {
        noInterrupts();
    }
    for (int i = 0; i < n; i++) {
        if (kind != 'o') {
            noInterrupts();
        }
        frame(p, f, (uint8_t)(pattern(i) & mask), kind == 'o' || kind == 'f' ? 'g' : kind, bit_us);
        if (kind != 'o') {
            interrupts();
        }
        if (kind != 'f') {
            while (port.available()) {
                matching += port.read() == (int)(pattern(received) & mask);
                received++;
            }
        }
    }
    if (kind == 'o') {
        interrupts();
    }
    delay(3);
    while (port.available()) {
        matching += port.read() == (int)(pattern(received) & mask);
        received++;
    }
    const unsigned errors = port.takeErrors();
    port.end();
    pinMode(p, INPUT);
    SerialDMSeq.printf("RXFMT %u %d %d %u\r\n", number, received, matching, errors);
}

/* Port k sends n bytes at 115200 in format `fmt`: TXFMT <USARTn> <microseconds to the last stop bit> */
static void tx_format(int k, const Format &f, int n)
{
    arduino::CH32RVHardwareSerial &port = *ports[k];
    port.begin(115200, f.config);
    const uint8_t number = port.peripheral();
    if (!port) {
        port.end();
        SerialDMSeq.printf("TXFMT %u CLOSED\r\n", number);
        return;
    }
    const uint32_t t0 = micros();
    for (int i = 0; i < n; i++) {
        port.write(pattern(i));
    }
    port.flush();
    const uint32_t took = micros() - t0;
    port.end();
    SerialDMSeq.printf("TXFMT %u %lu\r\n", number, (unsigned long)took);
}

/* ---- I2C: the clock registers and a transfer with nobody there ---- */
#if defined(CH32RV_WIRE_COUNT) && CH32RV_WIRE_COUNT > 0 && !CH32RV_WIRE0_SOFT
#define SELFBUS_WIRES 1
static arduino::CH32RVTwoWire *const wires[] = {
    &Wire,
#if CH32RV_WIRE_COUNT > 1 && !CH32RV_WIRE1_SOFT
    &Wire1,
#endif
};
static const int WIRE_COUNT = (int)(sizeof wires / sizeof wires[0]);

static uint32_t i2c_base(unsigned n)
{
    switch (n) {
    case 1: return CH32RV_I2C1_BASE;
#ifdef CH32RV_I2C2_BASE
    case 2: return CH32RV_I2C2_BASE;
#endif
    default: return 0;
    }
}

static void i2c_pins(unsigned n, int *scl, int *sda)
{
    *scl = *sda = -1;
    switch (n) {
#ifdef CH32RV_I2C1_SCL
    case 1: *scl = CH32RV_I2C1_SCL; *sda = CH32RV_I2C1_SDA; break;
#endif
#ifdef CH32RV_I2C2_SCL
    case 2: *scl = CH32RV_I2C2_SCL; *sda = CH32RV_I2C2_SDA; break;
#endif
    default: break;
    }
}

/* Which I2C wire k is and its pads, each read with the pull-up and the
 * pull-down before anything drives it: I2CINFO <I2Cn> <SCL> <SDA> <SCL up> <SCL down> <SDA up> <SDA down> */
static void i2c_info(int k)
{
    wires[k]->begin();
    const unsigned n = wires[k]->peripheral();
    wires[k]->end();
    int scl, sda;
    i2c_pins(n, &scl, &sda);
    int pulls[4] = {-1, -1, -1, -1};
    for (int i = 0; i < 2; i++) {
        const int pin = i ? sda : scl;
        if (pin < 0) {
            continue;
        }
        pinMode((pin_size_t)pin, INPUT_PULLUP);
        delay(1);
        pulls[2 * i] = digitalRead((pin_size_t)pin);
        pinMode((pin_size_t)pin, INPUT_PULLDOWN);
        delay(1);
        pulls[2 * i + 1] = digitalRead((pin_size_t)pin);
        pinMode((pin_size_t)pin, INPUT);
    }
    SerialDMSeq.printf("I2CINFO %u %d %d %d %d %d %d\r\n", n, scl, sda, pulls[0], pulls[1], pulls[2], pulls[3]);
}

/* setClock(hz) and what it wrote: I2CREG <PCLK1> <CTLR2 FREQ> <CKCFGR> */
static void i2c_reg(int k, unsigned long hz)
{
    wires[k]->begin();
    wires[k]->setClock(hz);
    const uint32_t base = i2c_base(wires[k]->peripheral());
    const unsigned freq = base ? CH32RV_I2C_CTLR2(base) & CH32RV_I2C_CTLR2_FREQ_MASK : 0;
    const unsigned ckcfgr = base ? CH32RV_I2C_CKCFGR(base) : 0;
    wires[k]->end();
    SerialDMSeq.printf("I2CREG %lu %u %u\r\n", (unsigned long)CH32RV_PCLK1_HZ, freq, ckcfgr);
}

/* A write to 0x50 with nobody on the bus: I2CNACK <endTransmission()> <microseconds> */
static void i2c_nack(int k)
{
    wires[k]->begin();
    wires[k]->setClock(100000);
    const uint32_t t0 = micros();
    wires[k]->beginTransmission(0x50);
    wires[k]->write((uint8_t)0);
    const uint8_t code = wires[k]->endTransmission();
    const uint32_t took = micros() - t0;
    wires[k]->end();
    SerialDMSeq.printf("I2CNACK %u %lu\r\n", code, (unsigned long)took);
}
#else
static const int WIRE_COUNT = 0;
#endif

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
    /* The whole line, for UNKNOWN: on H417's V3F build a "ready <nonce>" came
     * back as UNKNOWN ready now and then (2026-11-12), and argv[0] alone did
     * not say whether a word was lost or one too many came. */
    char whole[sizeof line];
    strncpy(whole, command, sizeof whole - 1u);
    whole[sizeof whole - 1u] = 0;
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
    } else if (!strcmp(argv[0], "wires")) {
        SerialDMSeq.printf("WIRES %d\r\n", WIRE_COUNT);
#if SELFBUS_WIRES
    } else if (!strcmp(argv[0], "i2cinfo") && argc == 2 && atoi(argv[1]) < WIRE_COUNT) {
        i2c_info(atoi(argv[1]));
    } else if (!strcmp(argv[0], "i2creg") && argc == 3 && atoi(argv[1]) < WIRE_COUNT) {
        i2c_reg(atoi(argv[1]), strtoul(argv[2], nullptr, 10));
    } else if (!strcmp(argv[0], "i2cnack") && argc == 2 && atoi(argv[1]) < WIRE_COUNT) {
        i2c_nack(atoi(argv[1]));
#endif
    } else if (!strcmp(argv[0], "ports")) {
        SerialDMSeq.printf("PORTS %d %u\r\n", PORT_COUNT, (unsigned)CH32RV_SERIAL_RX_BUFFER_SIZE);
    } else if (!strcmp(argv[0], "rxpin") && argc == 2 && atoi(argv[1]) < PORT_COUNT) {
        arduino::CH32RVHardwareSerial &port = *ports[atoi(argv[1])];
        port.begin(9600);
        const uint8_t number = port.peripheral();
        port.end();
        SerialDMSeq.printf("RXPIN %u %d\r\n", number, number ? rx_pin(number) : -1);
    } else if (!strcmp(argv[0], "rxfmt") && argc == 5 && atoi(argv[1]) < PORT_COUNT && format_named(argv[2])) {
        rx_format(atoi(argv[1]), *format_named(argv[2]), argv[3][0], atoi(argv[4]));
    } else if (!strcmp(argv[0], "txfmt") && argc == 4 && atoi(argv[1]) < PORT_COUNT && format_named(argv[2])) {
        tx_format(atoi(argv[1]), *format_named(argv[2]), atoi(argv[3]));
#if SELFBUS_SPI
    } else if (!strcmp(argv[0], "spiout") && argc == 4) {
        spi_out(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
    } else if (!strcmp(argv[0], "spiin") && argc == 2) {
        spi_in(atoi(argv[1]));
#endif
    } else {
        SerialDMSeq.printf("UNKNOWN %d [%s]\r\n", argc, whole);
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

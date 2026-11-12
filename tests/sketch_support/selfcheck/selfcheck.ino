/* Checks with no wiring at all (tests/instrumented/selfcheck): the sketch
 * looks at itself - its clock registers, the time its own transfers take,
 * its USARTs' receivers fed from their own RX pads, the ADC's internal
 * reference. What
 * only the outside can tell (which pad a signal leaves on, its levels, a
 * partner) stays with the wired cases. Console on SerialDMSeq:
 *
 *   ready <nonce>             READY <nonce> <build id>
 *   clocks                    CLOCKS <F_CPU> <HCLK> <PCLK1> <PCLK2> <CFGR0 hex> <PCLK2 from CFGR0, or 0>
 *   ports                     PORTS <count>: the HardwareSerial objects (Serial, Serial1, ...)
 *   tx <k> <baud> <n>         port k sends n bytes: TX <USARTn> <sent> <microseconds to the last stop bit> <BRR>
 *   rx <k> <n>                port k at 9600 receives n bytes the sketch sends on its RX pad (made an output):
 *                             RX <USARTn> <received> <matching> <takeErrors()>, or RX <USARTn> HELD
 *                             either: <USARTn or 0> CLOSED when begin() or the default route refused
 *   spi <hz> <n>              SPI (the library's default instance), n bytes at hz, MISO left as it is:
 *                             SPI <SPIn> <microseconds> <BR field>, or SPI 0 NONE
 *   allpins                   ALLPINS <pin> ...: every pad the part bonds
 *   rxpin <k>                 RXPIN <USARTn> <its default RX pin or -1>
 *   gpio <pin>                GPIO <pin> <read at HIGH> <read at LOW> <RISING count> <FALLING count>
 *                             <CHANGE count> <read with INPUT_PULLUP> <read with INPUT_PULLDOWN>
 *                             (five pulses written on the output for each interrupt mode), or
 *                             GPIO <pin> HELD <level> when the pulls do not move it (never driven)
 *   pwm <pin> <duty> <hz>     analogWrite(pin, duty) at hz, the pad read for 50 ms:
 *                             PWM <pin> <timer> <channel> <rising edges> <samples at 1> <samples>,
 *                             PWM <pin> NONE (no timer channel, or a DAC pad) or PWM <pin> HELD (never driven)
 *   vref                      VREF <analogReadVref()> <full scale> <CH32RV.getVddMillivolts()>,
 *                             or VREF -1 0 0 without an ADC */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <stdlib.h>
#include <string.h>
#include <SPI.h>
/* A hardware SPI as the bare name, as SPI.h declares it (none on CH32M030G8R7,
 * a SoftSPI when the board menu says Software). */
#if CH32RV_SPI_COUNT > 0 && !CH32RV_SPI_SOFT_N(CH32RV_SPI_ROW0_N)
#define SELFCHECK_SPI 1
#endif
#include "internal/ch32rv_registers.h"

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static char line[64];
static uint8_t used;

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

static uint32_t usart_base(uint8_t n)
{
    switch (n) {
#ifdef CH32RV_USART1_BASE
    case 1: return CH32RV_USART1_BASE;
#endif
#ifdef CH32RV_USART2_BASE
    case 2: return CH32RV_USART2_BASE;
#endif
#ifdef CH32RV_USART3_BASE
    case 3: return CH32RV_USART3_BASE;
#endif
#ifdef CH32RV_USART4_BASE
    case 4: return CH32RV_USART4_BASE;
#endif
#ifdef CH32RV_USART5_BASE
    case 5: return CH32RV_USART5_BASE;
#endif
#ifdef CH32RV_USART6_BASE
    case 6: return CH32RV_USART6_BASE;
#endif
#ifdef CH32RV_USART7_BASE
    case 7: return CH32RV_USART7_BASE;
#endif
#ifdef CH32RV_USART8_BASE
    case 8: return CH32RV_USART8_BASE;
#endif
    default: return 0;
    }
}

/* PCLK2 as RCC_CFGR0 says, on the families with the STM32F1-shaped PPRE2
 * field ([13:11]: 0xx /1, 1xx /2 << (x)); 0 elsewhere (not checked). */
static uint32_t pclk2_from_register(uint32_t cfgr0)
{
#if !CH32RV_HPRE_LINEAR && !defined(CH32RV_SERIES_CH32H417) && !defined(CH32RV_SERIES_CH32H416) && \
    !defined(CH32RV_SERIES_CH32H415)
    const uint32_t field = (cfgr0 >> 11) & 7u;
    const uint32_t div = field < 4u ? 1u : 2u << (field - 4u);
    return (uint32_t)CH32RV_HCLK_HZ / div;
#else
    (void)cfgr0;
    return 0;
#endif
}

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

static uint8_t pattern(int i) { return (uint8_t)(0x55u ^ (uint8_t)(i * 37u)); }

/* The transmitter: n bytes at baud, timed to the end of the last stop bit
 * (flush() waits for TC), and the BRR begin() wrote. */
static void transmit(int k, unsigned long baud, int n)
{
    arduino::CH32RVHardwareSerial &port = *ports[k];
    port.begin(baud);
    const uint8_t number = port.peripheral();
    const uint32_t base = number ? usart_base(number) : 0;
    if (!port || !base) {
        port.end();
        SerialDMSeq.printf("TX %u CLOSED\r\n", number);
        return;
    }
    int sent = 0;
    const uint32_t t0 = micros();
    for (int i = 0; i < n; i++) {
        sent += (int)port.write(pattern(i));
    }
    port.flush();
    const uint32_t took = micros() - t0;
    const unsigned brr = CH32RV_USART_BRR(base);
    port.end();
    SerialDMSeq.printf("TX %u %d %lu %u\r\n", number, sent, (unsigned long)took, brr);
}

/* Whether something on the board holds the pad (a driver, a strong pull):
 * then it reads the same with the pull-up and the pull-down, and driving it
 * would fight that driver - such a pad is never made an output here. */
static bool held(pin_size_t p, int *up, int *down)
{
    pinMode(p, INPUT_PULLUP);
    delay(1);
    *up = digitalRead(p);
    pinMode(p, INPUT_PULLDOWN);
    delay(1);
    *down = digitalRead(p);
    pinMode(p, INPUT);
    return *up == *down;
}

/* Port k's USART and its default RX pin (begin() and end() to learn which). */
static void rx_pin_of(int k)
{
    arduino::CH32RVHardwareSerial &port = *ports[k];
    port.begin(9600);
    const uint8_t number = port.peripheral();
    port.end();
    SerialDMSeq.printf("RXPIN %u %d\r\n", number, number ? rx_pin(number) : -1);
}

/* The receiver: its RX pad made a GPIO output, which the USART still hears
 * (a pad's input path stays on in output mode), and n bytes sent on it from
 * here at 9600 baud, 8N1 - timed by micros() against the start of each byte.
 * Not on a pad the board holds (a WCH-Link's TX on USART1's RX). */
static void receive(int k, int n)
{
    arduino::CH32RVHardwareSerial &port = *ports[k];
    port.begin(9600);
    const uint8_t number = port.peripheral();
    const int pin = number ? rx_pin(number) : -1;
    if (!port || pin < 0) {
        port.end();
        SerialDMSeq.printf("RX %u CLOSED\r\n", number);
        return;
    }
    int up, down;
    if (held((pin_size_t)pin, &up, &down)) {
        port.end();
        SerialDMSeq.printf("RX %u HELD\r\n", number);
        return;
    }
    port.begin(9600);                   /* held() left the pad an input */
    digitalWrite((pin_size_t)pin, HIGH);    /* idle high from the first moment it drives */
    pinMode((pin_size_t)pin, OUTPUT);
    digitalWrite((pin_size_t)pin, HIGH);
    delay(5);
    while (port.available()) {
        port.read();
    }
    port.takeErrors();
    int received = 0, matching = 0;
    uint8_t first[4] = {0, 0, 0, 0};
    /* i = -1: a 0xFF byte first, which warms both writes of this loop: its
     * first pass runs slow (cold), and on V006 and X035 the first byte sent
     * came in with a framing error while every later one was right - a frame
     * of ten 1 bits did not help (it never wrote LOW), a 0xFF byte did. The
     * receiver drops it before counting. */
    bool synced = false;
    for (int i = -1; i < n; i++) {
        const uint16_t frame = i < 0 ? 0x3FEu
                                     : (uint16_t)(0x200u | ((uint16_t)pattern(i) << 1));   /* start 0, data LSB first, stop 1 */
        noInterrupts();
        const uint32_t t0 = micros();
        for (int b = 0; b < 10; b++) {
            digitalWrite((pin_size_t)pin, (frame >> b) & 1u ? HIGH : LOW);
            while ((uint32_t)(micros() - t0) < (uint32_t)((b + 1) * 1000000UL / 9600UL)) {
            }
        }
        interrupts();
        delayMicroseconds(200);
        while (port.available()) {
            const int c = port.read();
            if (!synced && c == 0xFF) {
                synced = true;
                continue;
            }
            synced = true;
            if (received < 4) {
                first[received] = (uint8_t)c;
            }
            matching += c == (int)pattern(received);
            received++;
        }
        if (i < 0) {
            port.takeErrors();          /* the warm-up byte's own (X035 USART2: a framing error) */
        }
    }
    delay(5);
    while (port.available()) {
        const int c = port.read();
        if (!synced && c == 0xFF) {
            synced = true;
            continue;
        }
        synced = true;
        if (received < 4) {
            first[received] = (uint8_t)c;
        }
        matching += c == (int)pattern(received);
        received++;
    }
    const unsigned errors = port.takeErrors();
    port.end();
    SerialDMSeq.printf("RX %u %d %d %u %02x%02x%02x%02x\r\n", number, received, matching, errors,
                       first[0], first[1], first[2], first[3]);
}

static volatile uint16_t edges;
static void count_edge(void) { edges++; }

/* One pad on its own: its pull-up and pull-down as an input first (a pad the
 * board holds stops there), then written as an output and read back, and its
 * pin interrupt (the EXTI line, the dispatch) raised by writing it (the input
 * path stays on in output mode). */
static void gpio(int pin)
{
    const pin_size_t p = (pin_size_t)pin;
    int up, down;
    if (held(p, &up, &down)) {
        SerialDMSeq.printf("GPIO %d HELD %d\r\n", pin, up);
        return;
    }
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
    delayMicroseconds(10);
    const int high = digitalRead(p);
    digitalWrite(p, LOW);
    delayMicroseconds(10);
    const int low = digitalRead(p);
    static const PinStatus modes[3] = {RISING, FALLING, CHANGE};
    unsigned counts[3];
    for (int m = 0; m < 3; m++) {
        digitalWrite(p, LOW);
        delayMicroseconds(10);
        edges = 0;
        attachInterrupt(digitalPinToInterrupt(p), count_edge, modes[m]);
        for (int i = 0; i < 5; i++) {
            digitalWrite(p, HIGH);
            delayMicroseconds(20);
            digitalWrite(p, LOW);
            delayMicroseconds(20);
        }
        detachInterrupt(digitalPinToInterrupt(p));
        counts[m] = edges;
    }
    digitalWrite(p, LOW);
    pinMode(p, INPUT);
    SerialDMSeq.printf("GPIO %d %d %d %u %u %u %d %d\r\n", pin, high, low, counts[0], counts[1], counts[2], up, down);
}

/* Every pad the part bonds (the variant's port masks); the test leaves out
 * the debug, reset, strap, crystal and USB pads before asking for any. */
static void all_pins(void)
{
    SerialDMSeq.printf("ALLPINS");
    for (uint8_t port = 0; port < CH32RV_PORT_COUNT; port++) {
        const uint32_t mask = CH32RV_PORT_MASK(port);
        for (uint8_t bit = 0; bit < 32; bit++) {
            if (mask & (1ul << bit)) {
                SerialDMSeq.printf(" %d", (int)CH32RV_PIN(port, bit));
            }
        }
    }
    SerialDMSeq.printf("\r\n");
}

/* PWM on one pad (analogWrite at hz, 8-bit duty), read back on the pad's own
 * input path for 50 ms: rising edges and the share of samples at 1. */
static void pwm(int pin, int duty, unsigned long hz)
{
    const pin_size_t p = (pin_size_t)pin;
#ifdef CH32RV_PWM_PIN_COUNT
    const unsigned timer = (unsigned)CH32RV_PWM_PIN_TO_TIMER(p);
    const unsigned channel = (unsigned)CH32RV_PWM_PIN_TO_CHANNEL(p);
#else
    const unsigned timer = 0, channel = 0;
#endif
    /* analogWrite() on a DAC pad sets the DAC, not a timer (V307's PA4 read a
     * steady 0.8 V at 64 and stayed driven): no PWM to see there. */
    bool dac = false;
#ifdef CH32RV_DAC1_PIN
    dac = dac || p == (pin_size_t)CH32RV_DAC1_PIN;
#endif
#ifdef CH32RV_DAC2_PIN
    dac = dac || p == (pin_size_t)CH32RV_DAC2_PIN;
#endif
    if (!timer || dac) {
        SerialDMSeq.printf("PWM %d NONE\r\n", pin);
        return;
    }
    int up, down;
    if (held(p, &up, &down)) {
        SerialDMSeq.printf("PWM %d HELD\r\n", pin);
        return;
    }
    analogWriteFrequency(p, hz);
    analogWrite(p, duty);
    delay(5);
    uint32_t samples = 0, highs = 0, edges = 0;
    int prev = digitalRead(p);
    const uint32_t t0 = micros();
    while ((uint32_t)(micros() - t0) < 50000u) {
        const int v = digitalRead(p);
        samples++;
        highs += (uint32_t)v;
        edges += (uint32_t)(v && !prev);
        prev = v;
    }
    analogWrite(p, 0);
    pinMode(p, INPUT);
    SerialDMSeq.printf("PWM %d %u %u %lu %lu %lu\r\n", pin, timer, channel, (unsigned long)edges,
                       (unsigned long)highs, (unsigned long)samples);
}

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
    } else if (!strcmp(argv[0], "clocks")) {
        const uint32_t cfgr0 = CH32RV_RCC_CFGR0;
        SerialDMSeq.printf("CLOCKS %lu %lu %lu %lu %08lx %lu\r\n", (unsigned long)F_CPU,
                           (unsigned long)CH32RV_HCLK_HZ, (unsigned long)CH32RV_PCLK1_HZ,
                           (unsigned long)CH32RV_PCLK2_HZ, (unsigned long)cfgr0,
                           (unsigned long)pclk2_from_register(cfgr0));
    } else if (!strcmp(argv[0], "ports")) {
        SerialDMSeq.printf("PORTS %d\r\n", PORT_COUNT);
    } else if (!strcmp(argv[0], "tx") && argc == 4 && atoi(argv[1]) < PORT_COUNT) {
        transmit(atoi(argv[1]), strtoul(argv[2], nullptr, 10), atoi(argv[3]));
    } else if (!strcmp(argv[0], "rx") && argc == 3 && atoi(argv[1]) < PORT_COUNT) {
        receive(atoi(argv[1]), atoi(argv[2]));
    } else if (!strcmp(argv[0], "spi") && argc == 3) {
#if SELFCHECK_SPI
        static uint8_t buf[256];
        const int n = atoi(argv[2]) < (int)sizeof buf ? atoi(argv[2]) : (int)sizeof buf;
        SPI.begin();
        SPI.beginTransaction(SPISettings(strtoul(argv[1], nullptr, 10), MSBFIRST, SPI_MODE0));
        const uint32_t t0 = micros();
        SPI.transfer(buf, (size_t)n);
        const uint32_t took = micros() - t0;
        SPI.endTransaction();
        const unsigned number = SPI.peripheral();
        const uint32_t base = number == 1 ? CH32RV_SPI1_BASE :
#ifdef CH32RV_SPI2_BASE
                              number == 2 ? CH32RV_SPI2_BASE :
#endif
#ifdef CH32RV_SPI3_BASE
                              number == 3 ? CH32RV_SPI3_BASE :
#endif
                              0;
        const unsigned br = base ? (CH32RV_SPI_CTLR1(base) >> CH32RV_SPI_CTLR1_BR_SHIFT) & 7u : 0;
        SPI.end();
        SerialDMSeq.printf("SPI %u %lu %u\r\n", number, (unsigned long)took, br);
#else
        SerialDMSeq.printf("SPI 0 NONE\r\n");
#endif
    } else if (!strcmp(argv[0], "allpins")) {
        all_pins();
    } else if (!strcmp(argv[0], "rxpin") && argc == 2 && atoi(argv[1]) < PORT_COUNT) {
        rx_pin_of(atoi(argv[1]));
    } else if (!strcmp(argv[0], "pwm") && argc == 4) {
        pwm(atoi(argv[1]), atoi(argv[2]), strtoul(argv[3], nullptr, 10));
    } else if (!strcmp(argv[0], "gpio") && argc == 2) {
        gpio(atoi(argv[1]));
    } else if (!strcmp(argv[0], "vref")) {
#if defined(NUM_ANALOG_INPUTS) && defined(CH32RV_ADC_BITS)
        analogReadResolution(CH32RV_ADC_BITS);
        SerialDMSeq.printf("VREF %d %d %lu\r\n", analogReadVref(), (1 << CH32RV_ADC_BITS) - 1,
                           (unsigned long)CH32RV.getVddMillivolts());
#else
        SerialDMSeq.printf("VREF -1 0 0\r\n");
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

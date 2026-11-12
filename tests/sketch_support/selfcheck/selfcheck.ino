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
 *                             RX <USARTn> <received> <matching> <takeErrors()>
 *                             either: <USARTn or 0> CLOSED when begin() or the default route refused
 *   spi <hz> <n>              SPI (the library's default instance), n bytes at hz, MISO left as it is:
 *                             SPI <SPIn> <microseconds> <BR field>, or SPI 0 NONE
 *   pins                      PINS <pin> ...: the pads gpio may drive (USART RX defaults, SPI SCK / MOSI)
 *   gpio <pin>                GPIO <pin> <read at HIGH> <read at LOW> <RISING count> <FALLING count>
 *                             <CHANGE count> <read with INPUT_PULLUP> <read with INPUT_PULLDOWN>
 *                             (five pulses written on the output for each interrupt mode)
 *   vref                      VREF <analogReadVref()> <full scale> <CH32RV.getVddMillivolts()>,
 *                             or VREF -1 0 0 without an ADC */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <stdlib.h>
#include <string.h>
#if defined(CH32RV_SPI1_SCK) || defined(CH32RV_SPI2_SCK) || defined(CH32RV_SPI3_SCK)
#include <SPI.h>
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

/* The receiver: its RX pad made a GPIO output, which the USART still hears
 * (a pad's input path stays on in output mode), and n bytes sent on it from
 * here at 9600 baud, 8N1 - timed by micros() against the start of each byte. */
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
    pinMode((pin_size_t)pin, OUTPUT);
    digitalWrite((pin_size_t)pin, HIGH);
    delay(5);
    while (port.available()) {
        port.read();
    }
    port.takeErrors();
    int received = 0, matching = 0;
    for (int i = 0; i < n; i++) {
        const uint16_t frame = (uint16_t)(0x200u | ((uint16_t)pattern(i) << 1));   /* start 0, data LSB first, stop 1 */
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
            matching += port.read() == (int)pattern(received);
            received++;
        }
    }
    delay(5);
    while (port.available()) {
        matching += port.read() == (int)pattern(received);
        received++;
    }
    const unsigned errors = port.takeErrors();
    port.end();
    SerialDMSeq.printf("RX %u %d %d %u\r\n", number, received, matching, errors);
}

static volatile uint16_t edges;
static void count_edge(void) { edges++; }

/* One pad on its own: written as an output and read back, its pin interrupt
 * (the EXTI line, the dispatch) raised by writing it (the input path stays on
 * in output mode), then its pull-up and pull-down as an input. */
static void gpio(int pin)
{
    const pin_size_t p = (pin_size_t)pin;
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
    pinMode(p, INPUT_PULLUP);
    delay(1);
    const int up = digitalRead(p);
    pinMode(p, INPUT_PULLDOWN);
    delay(1);
    const int down = digitalRead(p);
    pinMode(p, INPUT);
    SerialDMSeq.printf("GPIO %d %d %d %u %u %u %d %d\r\n", pin, high, low, counts[0], counts[1], counts[2], up, down);
}

/* The pads this check may drive: each USART's default RX (driven by the
 * receive check already) and the default SPI's SCK and MOSI - pads a board
 * leaves for its user. */
static int gpio_pins[16];
static int gpio_count;
static void collect_pins(void)
{
    gpio_count = 0;
    for (uint8_t n = 1; n <= 8 && gpio_count < 14; n++) {
        const int pin = rx_pin(n);
        if (pin >= 0) {
            gpio_pins[gpio_count++] = pin;
        }
    }
#if defined(PIN_SPI_SCK) && defined(PIN_SPI_MOSI)
    gpio_pins[gpio_count++] = PIN_SPI_SCK;
    gpio_pins[gpio_count++] = PIN_SPI_MOSI;
#endif
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
    } else if (!strcmp(argv[0], "pins")) {
        collect_pins();
        SerialDMSeq.printf("PINS");
        for (int i = 0; i < gpio_count; i++) {
            SerialDMSeq.printf(" %d", gpio_pins[i]);
        }
        SerialDMSeq.printf("\r\n");
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

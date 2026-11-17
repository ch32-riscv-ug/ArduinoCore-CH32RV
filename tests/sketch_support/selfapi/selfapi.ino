/* The core's utility APIs with no wiring (tests/instrumented/selfapi), beside selfcheck, selfbus and selfsys (on
 * V003 none of those has room for them). Console on SerialDMSeq:
 *
 *   ready <nonce>             READY <nonce> <build id>
 *   crc                       CRC <block 1/0> <crc32("123456789")> <crc32 of the bytes 1..128>
 *                             <the same in pieces of 1, 2, 3, 5, 7, ...> <crc32 of bytes 2..127 (unaligned)>
 *   rng                       RNG <generator 1/0> <8 ch32rv_random() words, hex> <ch32rv_fill_random() of 7 bytes, hex>
 *   adcpins                   ADCPINS <pin> ...: every bonded pad with an ADC channel
 *   mv <pin>                  the pad driven 1 then 0 as an output, converted by analogReadMilliVolts()'s own path
 *                             (ch32rv_adc_millivolts(): it leaves the pad as it is):
 *                             MV <pin> <channel> <mV at 1> <mV at 0> <CH32RV.getVddMillivolts()>,
 *                             MV <pin> HELD / NONE
 *   prio                      two timers' update interrupts, a slow one (every 2 ms, its handler 300 us long) and a
 *                             fast one (every 100 us), 50 ms in each of three settings - slow less urgent than fast,
 *                             both the same, slow more urgent: PRIO <preemption levels> then <slow handlers run>
 *                             <of them interrupted by the fast one> for each setting; PRIO NONE without two timers */
#include <Arduino.h>
#include <CH32RV.h>
#include <CH32RVTimer.h>
#include <SerialDMSeq.h>
#include <ch32rv_crc.h>
#include <ch32rv_priority.h>
#include <stdlib.h>
#include <string.h>
#include "internal/ch32rv_ticks.h"

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

/* wiring_analog.c: analogReadMilliVolts() for a channel, the pad left as it is. */
extern "C" uint32_t ch32rv_adc_millivolts(uint8_t index, uint32_t channel);

static char line[64];
static uint8_t used;

static void crc_check()
{
    uint8_t buf[128];
    for (unsigned i = 0; i < sizeof buf; i++) {
        buf[i] = (uint8_t)(i + 1u);
    }
    /* Pieces of 1, 2, 3, 5, 7, 9, ...: the block's path from a running CRC (its seed word) and the table's. */
    uint32_t pieces = CH32RV_CRC32_INIT;
    for (unsigned at = 0, step = 1; at < sizeof buf; step = step < 3 ? step + 1 : step + 2) {
        const unsigned n = at + step <= sizeof buf ? step : sizeof buf - at;
        pieces = ch32rv_crc32_update(pieces, buf + at, n);
        at += n;
    }
#ifdef CH32RV_HAS_CRC_HARDWARE
    const int block = 1;
#else
    const int block = 0;
#endif
    SerialDMSeq.printf("CRC %d %08lx %08lx %08lx %08lx\r\n", block, (unsigned long)ch32rv_crc32("123456789", 9),
                       (unsigned long)CH32RV.crc32(buf, sizeof buf), (unsigned long)pieces,
                       (unsigned long)ch32rv_crc32(buf + 1, sizeof buf - 2u));
}

static void rng_check()
{
#ifdef CH32RV_RNG_BASE
    SerialDMSeq.printf("RNG 1");
#else
    SerialDMSeq.printf("RNG 0");
#endif
    for (int i = 0; i < 8; i++) {
        SerialDMSeq.printf(" %08lx", (unsigned long)ch32rv_random());
    }
    uint8_t b[7];
    ch32rv_fill_random(b, sizeof b);
    SerialDMSeq.printf(" ");
    for (unsigned i = 0; i < sizeof b; i++) {
        SerialDMSeq.printf("%02x", b[i]);
    }
    SerialDMSeq.printf("\r\n");
}

#if defined(NUM_ANALOG_INPUTS) && CH32RV_ADC_CLOCK_OK
#define SELFAPI_ADC 1
#ifdef CH32RV_PIN_TO_ADC_INSTANCE
#define SELFAPI_ADC_INSTANCE(p) CH32RV_PIN_TO_ADC_INSTANCE(p)
#else
#define SELFAPI_ADC_INSTANCE(p) ((void)(p), 1u)
#endif

static void adc_pins()
{
    SerialDMSeq.printf("ADCPINS");
    for (uint8_t port = 0; port < CH32RV_PORT_COUNT; port++) {
        const uint32_t mask = CH32RV_PORT_MASK(port);
        for (uint8_t bit = 0; bit < 32; bit++) {
            const pin_size_t p = (pin_size_t)CH32RV_PIN(port, bit);
            if ((mask & (1ul << bit)) && CH32RV_PIN_TO_ADC_CHANNEL(p) != NOT_AN_ANALOG_PIN) {
                SerialDMSeq.printf(" %d", (int)p);
            }
        }
    }
    SerialDMSeq.printf("\r\n");
}

static void mv_check(int pin)
{
    const pin_size_t p = (pin_size_t)pin;
    const uint32_t channel = CH32RV_PIN_TO_ADC_CHANNEL(p);
    if (!digitalPinIsValid(p) || channel == NOT_AN_ANALOG_PIN) {
        SerialDMSeq.printf("MV %d NONE\r\n", pin);
        return;
    }
    pinMode(p, INPUT_PULLUP);
    delay(1);
    const int up = digitalRead(p);
    pinMode(p, INPUT_PULLDOWN);
    delay(1);
    const int down = digitalRead(p);
    pinMode(p, INPUT);
    if (up == down) {                            /* held by the board: never driven */
        SerialDMSeq.printf("MV %d HELD\r\n", pin);
        return;
    }
    (void)analogReadMilliVolts(p);               /* the ADC started and calibrated */
    uint32_t mv[2];
    for (int v = 1; v >= 0; v--) {
        digitalWrite(p, v ? HIGH : LOW);
        pinMode(p, OUTPUT);
        digitalWrite(p, v ? HIGH : LOW);
        delayMicroseconds(50);
        mv[v] = ch32rv_adc_millivolts((uint8_t)(SELFAPI_ADC_INSTANCE(p) - 1u), channel);
    }
    pinMode(p, INPUT);
    SerialDMSeq.printf("MV %d %lu %lu %lu %lu\r\n", pin, (unsigned long)channel, (unsigned long)mv[1], (unsigned long)mv[0],
                       (unsigned long)CH32RV.getVddMillivolts());
}
#endif

/* The priority check: the slow handler spins and watches the fast one's count. */
static volatile uint32_t fast_count, slow_runs, slow_nested;
static uint32_t spin_ticks;

static void on_fast(void *)
{
    fast_count++;
}

static void on_slow(void *)
{
    const uint32_t seen = fast_count, t0 = ch32rv_ticks();
    while ((uint32_t)(ch32rv_ticks() - t0) < spin_ticks) {
    }
    slow_runs++;
    slow_nested += fast_count != seen;
}

static void on_quiesce(void *, uint8_t, uint8_t)
{
}

static uint8_t prio_identity[2];

static void prio_check()
{
    const CH32RVTimerOwner owners[2] = {{&prio_identity[0], on_quiesce, nullptr},
                                        {&prio_identity[1], on_quiesce, nullptr}};
    CH32RVTimerLease lease[2];
    uint32_t irq[2];
    int got = 0;
    /* 1 us counts: the slow one every 2 ms, the fast one every 100 us. */
    for (size_t i = 0; i < ch32rv_timer_count() && got < 2; i++) {
        const CH32RVTimerCapability *cap = ch32rv_timer_capability(i);
        const CH32RVTimerRequest request = {cap->number, CH32RV_TIMER_WHOLE,
                                            {(uint16_t)(ch32rv_timer_clock_hz(cap->number) / 1000000u - 1u),
                                             got == 0 ? 1999u : 99u, 0u}};
        lease[got] = ch32rv_timer_try_acquire(&request, &owners[got]);
        if (ch32rv_timer_lease_valid(&lease[got])) {
            irq[got] = cap->update_irq;
            got++;
        }
    }
    if (got < 2) {
        for (int k = 0; k < got; k++) {
            ch32rv_timer_release(&lease[k]);
        }
        SerialDMSeq.printf("PRIO NONE\r\n");
        return;
    }
    spin_ticks = CH32RV_SYSTICK_HZ / 1000000u * 300u;
    const uint8_t settings[3][2] = {{CH32RV_IRQ_PRIORITY(1), CH32RV_IRQ_PRIORITY(0)},
                                    {CH32RV_IRQ_PRIORITY(0), CH32RV_IRQ_PRIORITY(0)},
                                    {CH32RV_IRQ_PRIORITY(0), CH32RV_IRQ_PRIORITY(1)}};
    uint32_t runs[3], nested[3];
    ch32rv_timer_apply_base(&lease[0]);
    ch32rv_timer_apply_base(&lease[1]);
    ch32rv_timer_attach_update_interrupt(&lease[0], on_slow, nullptr);
    ch32rv_timer_attach_update_interrupt(&lease[1], on_fast, nullptr);
    for (int s = 0; s < 3; s++) {
        ch32rv_irq_set_priority(irq[0], settings[s][0]);
        ch32rv_irq_set_priority(irq[1], settings[s][1]);
        slow_runs = 0;
        slow_nested = 0;
        ch32rv_timer_start(&lease[0]);
        ch32rv_timer_start(&lease[1]);
        delay(50);
        ch32rv_timer_stop(&lease[0]);
        ch32rv_timer_stop(&lease[1]);
        runs[s] = slow_runs;
        nested[s] = slow_nested;
    }
    for (int k = 0; k < 2; k++) {
        ch32rv_timer_detach_update_interrupt(&lease[k]);
        ch32rv_irq_set_priority(irq[k], CH32RV_IRQ_PRIORITY_CORE);
        ch32rv_timer_release(&lease[k]);
    }
    SerialDMSeq.printf("PRIO %u %lu %lu %lu %lu %lu %lu\r\n", (unsigned)CH32RV_IRQ_PREEMPT_LEVELS,
                       (unsigned long)runs[0], (unsigned long)nested[0], (unsigned long)runs[1],
                       (unsigned long)nested[1], (unsigned long)runs[2], (unsigned long)nested[2]);
}


static void reply(char *command)
{
    char *argv[3];
    int argc = 0;
    for (char *t = strtok(command, " "); t && argc < 3; t = strtok(nullptr, " ")) {
        argv[argc++] = t;
    }
    if (!argc) {
        return;
    }
    if (!strcmp(argv[0], "ready") && argc == 2) {
        SerialDMSeq.printf("READY %s %s\r\n", argv[1], BENCH_BUILD_ID);
    } else if (!strcmp(argv[0], "crc")) {
        crc_check();
    } else if (!strcmp(argv[0], "rng")) {
        rng_check();
#if SELFAPI_ADC
    } else if (!strcmp(argv[0], "adcpins")) {
        adc_pins();
    } else if (!strcmp(argv[0], "mv") && argc == 2) {
        mv_check(atoi(argv[1]));
#else
    } else if (!strcmp(argv[0], "adcpins")) {
        SerialDMSeq.printf("ADCPINS\r\n");
#endif
    } else if (!strcmp(argv[0], "prio")) {
        prio_check();
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

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
 *                             <of them interrupted by the fast one> for each setting; PRIO NONE without two timers
 *   cores                     CORES V5F <ports> (CH32H41x, the sketch on the V5F and the V3F to start: the commands
 *                             below), CORES V3F (H41x with CPU Core = V3F: hsem and ipc alone), CORES NONE
 *   hsem                      the HSEM between the cores, step by step (hsem_check(): the reply's fields), or on one
 *                             core with CPU Core = V3F
 *   ipc <n>                   n round trips V5F -> V3F (its handler answers m + 1) -> V5F, then one message the V5F
 *                             polls for; IPC V3F 0 0 0 with CPU Core = V3F (no other core)
 *   v3fport <k>               V3FPORT <USARTn> <RX pin>: port k (Serial, Serial1, ...)
 *   v3fser <k>                port k begun and used on the V3F: 64 bytes sent at 115200, then 16 received at 9600
 *                             from its own RX pad driven as a GPIO, its interrupt allocated to the V3F
 *   canlb <bitrate>           CAN (Arduino_CAN) in silent loopback: 20 frames - standard and extended identifiers in
 *                             turn, 0 .. 8 bytes - each read back: CANLB <begun 1/0> <right> <tx errors> <rx errors>,
 *                             CANLB NONE without a CAN */
#include <Arduino.h>
#include <CH32RV.h>
#include <CH32RVTimer.h>
#include <SerialDMSeq.h>
#include <ch32rv_crc.h>
#include <ch32rv_priority.h>
#include <Arduino_CAN.h>
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


/* ---- CH32H41x's two cores: the HSEM, the IPC, and the V3F's own peripherals and interrupts ---- */
#if defined(CH32RV_HAS_HSEM)
#define CORES_HSEM_CH    5u        /* the channel the steps pass between the cores */
#define CORES_COUNT_CH   6u        /* the one guarding the shared count */
#define CORES_RAW_CH     7u        /* the one read raw once */

/* One read of a free channel's RLR (the 1-step lock): what it returns when it locks - 0 by the RM, this core's
 * LOCK | CID by the EVT's HSEM_FastTake() - then the channel released. Recorded, not judged. */
static uint32_t cores_raw_lock()
{
    const uint32_t raw = CH32RV_HSEM_RLR(CORES_RAW_CH);
    ch32rv_hsem_unlock(CORES_RAW_CH);
    return raw;
}

#if defined(CH32RV_HAS_V3F)
#define CORES_ADDS       20000u    /* each core's additions under CORES_COUNT_CH */

static uint8_t cores_stack[4096] __attribute__((aligned(16)));
static volatile uint32_t cores_step;          /* the V5F's: the step the V3F is to do next */
static volatile uint32_t cores_done;          /* the V3F's: the last step it did */
static volatile uint32_t cores_result[8];     /* the V3F's answers */
static volatile uint32_t cores_count;         /* added to by both, under CORES_COUNT_CH */

/* A wait of the V5F's for a word the V3F writes: never back to back (wiring_time.c's pause). */
static bool cores_wait(volatile uint32_t *word, uint32_t want, uint32_t ms)
{
    const uint32_t start = millis();
    while (*word != want) {
        if ((uint32_t)(millis() - start) >= ms) {
            return false;
        }
        delayMicroseconds(2);
    }
    return true;
}

static bool cores_start(void (*fn)(void *))
{
    cores_step = 0;
    cores_done = 0;
    for (unsigned i = 0; i < 8; i++) {
        cores_result[i] = 0;
    }
    return CH32RV.startV3F(fn, nullptr, cores_stack, sizeof cores_stack);
}

/* The V5F's end of a V3F function: up to ms for it to return. */
static bool cores_finish(uint32_t ms)
{
    const uint32_t start = millis();
    while (CH32RV.isV3FRunning()) {
        if ((uint32_t)(millis() - start) >= ms) {
            return false;
        }
        delay(1);
    }
    return true;
}

/* The V3F's wait for the V5F's next step (at most 2 s: a lost step still ends the function). */
static void v3f_await(uint32_t step)
{
    const uint32_t start = millis();
    while (cores_step != step && (uint32_t)(millis() - start) < 2000u) {
    }
}

static void cores_add(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        CH32RV.hsemTake(CORES_COUNT_CH);
        cores_count = cores_count + 1;          /* a plain read and write: only the semaphore keeps it whole */
        CH32RV.hsemRelease(CORES_COUNT_CH);
    }
}

static void hsem_v3f(void *)
{
    v3f_await(1);
    cores_result[0] = CH32RV.hsemTryTake(CORES_HSEM_CH);    /* the V5F holds it: 0 */
    cores_result[1] = CH32RV.hsemRelease(CORES_HSEM_CH);    /* not the V3F's: 0, and it stays taken */
    cores_done = 1;
    v3f_await(2);
    cores_result[2] = CH32RV.hsemTryTake(CORES_HSEM_CH);    /* released: 1, kept */
    cores_done = 2;
    v3f_await(3);
    cores_result[3] = CH32RV.hsemRelease(CORES_HSEM_CH);    /* 1 */
    cores_done = 3;
    v3f_await(4);
    cores_add(CORES_ADDS);
    cores_done = 4;
}

/* HSEM V5F <V5F take> <V5F again> <V3F take while the V5F holds> <V3F release of the V5F's> <V5F release>
 *          <V3F take after it> <V5F try while the V3F holds> <V5F take(20 ms)> <ms it waited> <V3F release>
 *          <V5F take after the V3F's release> <count> <expected> <raw read, hex> */
static void hsem_check()
{
    cores_count = 0;
    const uint32_t raw = cores_raw_lock();
    if (!cores_start(hsem_v3f)) {
        SerialDMSeq.printf("HSEM V5F BUSY\r\n");
        return;
    }
    const int a = CH32RV.hsemTryTake(CORES_HSEM_CH);
    const int b = CH32RV.hsemTryTake(CORES_HSEM_CH);
    cores_step = 1;
    cores_wait(&cores_done, 1, 1000);
    const int e = CH32RV.hsemRelease(CORES_HSEM_CH);
    cores_step = 2;
    cores_wait(&cores_done, 2, 1000);
    const int g = CH32RV.hsemTryTake(CORES_HSEM_CH);
    const uint32_t t0 = millis();
    const int h = CH32RV.hsemTake(CORES_HSEM_CH, 20);
    const uint32_t waited = millis() - t0;
    cores_step = 3;
    cores_wait(&cores_done, 3, 1000);
    const int i = CH32RV.hsemTake(CORES_HSEM_CH, 100);
    CH32RV.hsemRelease(CORES_HSEM_CH);
    cores_step = 4;
    cores_add(CORES_ADDS);
    cores_finish(5000);
    SerialDMSeq.printf("HSEM V5F %d %d %lu %lu %d %lu %d %d %lu %lu %d %lu %lu %08lx\r\n", a, b,
                       (unsigned long)cores_result[0], (unsigned long)cores_result[1], e,
                       (unsigned long)cores_result[2], g, h, (unsigned long)waited, (unsigned long)cores_result[3],
                       i, (unsigned long)cores_count, (unsigned long)(2u * CORES_ADDS), (unsigned long)raw);
}

/* ---- IPC ---- */
static volatile uint32_t ipc_v5f_last, ipc_v5f_cause, ipc_v5f_core, ipc_v3f_cause, ipc_v3f_core, ipc_v3f_count;

static uint32_t read_mcause()
{
    uint32_t c;
    __asm__ volatile ("csrr %0, mcause" : "=r"(c));   /* machine mode in a handler, on either core */
    return c;
}

static void ipc_on_v5f(uint32_t m)
{
    ipc_v5f_cause = read_mcause();
    ipc_v5f_core = ch32rv_core_id();
    ipc_v5f_last = m;
}

static void ipc_on_v3f(uint32_t m)
{
    ipc_v3f_cause = read_mcause();
    ipc_v3f_core = ch32rv_core_id();
    ipc_v3f_count = ipc_v3f_count + 1;
    CH32RV.ipcSend(m + 1u, 0);                        /* the answer, from the V3F's handler */
}

static void ipc_v3f(void *)
{
    CH32RV.ipcOnReceive(ipc_on_v3f);
    cores_done = 1;
    v3f_await(5);
    cores_result[1] = CH32RV.ipcSend(0x11110000u, 100);   /* one way, to the V5F's handler */
    cores_done = 5;
    v3f_await(2);                                     /* the round trips run in the handler meanwhile */
    CH32RV.ipcOnReceive(nullptr);
    cores_result[0] = CH32RV.ipcSend(0x5A5A1234u, 100);   /* to the V5F, which polls */
    cores_done = 2;
}

/* IPCREGS <CTLR> <ENA> <STS> <ISR> <ISM>, hex, read on the V5F (the ISM is the reading core's view)
 * IPC V5F <round trips asked> <answered> <microseconds for them> <V3F's mcause> <V3F's core> <V5F's mcause>
 *          <V5F's core> <V3F handler runs> <the polled message, hex> <V3F -> V5F one way: 1 reached its handler>
 * The registers are printed twice: after the one-way message and right after the first round trip's send,
 * before its answer - what the bench needs to see when an interrupt does not come. */
static void ipc_regs()
{
    SerialDMSeq.printf("IPCREGS %08lx %08lx %08lx %08lx %08lx\r\n", (unsigned long)CH32RV_REG32(0xE000D000u),
                       (unsigned long)CH32RV_REG32(0xE000D010u), (unsigned long)CH32RV_REG32(0xE000D014u),
                       (unsigned long)CH32RV_REG32(0xE000D004u), (unsigned long)CH32RV_REG32(0xE000D008u));
}

static void ipc_check(uint32_t n)
{
    ipc_v5f_last = ipc_v5f_cause = ipc_v5f_core = ipc_v3f_cause = ipc_v3f_core = ipc_v3f_count = 0;
    uint32_t stale;
    while (CH32RV.ipcReceive(stale)) {
    }
    if (!cores_start(ipc_v3f)) {
        SerialDMSeq.printf("IPC V5F BUSY\r\n");
        return;
    }
    cores_wait(&cores_done, 1, 1000);
    CH32RV.ipcOnReceive(ipc_on_v5f);
    cores_step = 5;
    const bool to_v5f = cores_wait(&ipc_v5f_last, 0x11110000u, 200);
    cores_wait(&cores_done, 5, 1000);
    ipc_regs();
    uint32_t ok = 0;
    uint32_t took_first = 0;
    const uint32_t t0 = micros();
    for (uint32_t i = 1; i <= n; i++) {
        const uint32_t m = i * 2u;
        if (!CH32RV.ipcSend(m, 100)) {
            break;
        }
        if (i == 1u) {
            const uint32_t r0 = micros();
            ipc_regs();     /* before its answer can matter; its time is taken out of the total */
            took_first = micros() - r0;
        }
        if (!cores_wait(&ipc_v5f_last, m + 1u, 100)) {
            break;
        }
        ok++;
    }
    const uint32_t took = micros() - t0 - took_first;
    CH32RV.ipcOnReceive(nullptr);
    cores_step = 2;
    cores_wait(&cores_done, 2, 1000);
    uint32_t polled = 0;
    const uint32_t start = millis();
    while (!CH32RV.ipcReceive(polled) && (uint32_t)(millis() - start) < 100u) {
        delayMicroseconds(2);
    }
    cores_finish(1000);
    SerialDMSeq.printf("IPC V5F %lu %lu %lu %08lx %lu %08lx %lu %lu %08lx %d\r\n", (unsigned long)n,
                       (unsigned long)ok, (unsigned long)took, (unsigned long)ipc_v3f_cause,
                       (unsigned long)ipc_v3f_core, (unsigned long)ipc_v5f_cause, (unsigned long)ipc_v5f_core,
                       (unsigned long)ipc_v3f_count, (unsigned long)polled, to_v5f ? 1 : 0);
}

/* ---- a USART on the V3F: begun there, its interrupts taken there ---- */
static arduino::CH32RVHardwareSerial *const cores_ports[] = {
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
    nullptr,
};
static const int CORES_PORTS = (int)(sizeof cores_ports / sizeof cores_ports[0]) - 1;

static int usart_irqn(uint8_t n)
{
    switch (n) {
#ifdef CH32RV_IRQN_USART1
    case 1: return CH32RV_IRQN_USART1;
#endif
#ifdef CH32RV_IRQN_USART2
    case 2: return CH32RV_IRQN_USART2;
#endif
#ifdef CH32RV_IRQN_USART3
    case 3: return CH32RV_IRQN_USART3;
#endif
#ifdef CH32RV_IRQN_USART4
    case 4: return CH32RV_IRQN_USART4;
#endif
#ifdef CH32RV_IRQN_USART5
    case 5: return CH32RV_IRQN_USART5;
#endif
#ifdef CH32RV_IRQN_USART6
    case 6: return CH32RV_IRQN_USART6;
#endif
#ifdef CH32RV_IRQN_USART7
    case 7: return CH32RV_IRQN_USART7;
#endif
#ifdef CH32RV_IRQN_USART8
    case 8: return CH32RV_IRQN_USART8;
#endif
    default: return -1;
    }
}

static int usart_rx_pin(uint8_t n)
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

static uint8_t cores_pattern(int i) { return (uint8_t)(0x55u ^ (uint8_t)(i * 37u)); }

static int cores_port;     /* the port the V3F function works on */

/* Receive on the V3F what this side sends on the RX pad, as selfcheck's receive() does; counts into
 * cores_result[5] (received) and [6] (matching). */
static void serial_v3f_take(arduino::CH32RVHardwareSerial &port, bool &synced)
{
    while (port.available()) {
        const int c = port.read();
        if (!synced && c == 0xFF) {
            synced = true;
            continue;
        }
        synced = true;
        cores_result[6] = cores_result[6] + (c == (int)cores_pattern((int)cores_result[5]) ? 1u : 0u);
        cores_result[5] = cores_result[5] + 1u;
    }
}

/* On the V3F: TX of 64 bytes at 115200 (the TXE interrupt drains the ring), which core the USART's interrupt is
 * allocated to, then RX of 16 bytes at 9600 sent on its own RX pad from here as a GPIO (selfcheck's receive(): the
 * RXNE interrupt fills the ring). cores_result: [0] USART number, [1] bytes sent, [2] us to the last stop bit,
 * [3] PFIC_IALLOCR's bit for its interrupt (0 = the V3F), [4] 1 when the RX pad is held (not driven),
 * [5] received, [6] matching, [7] takeErrors(). */
static void serial_v3f(void *)
{
    arduino::CH32RVHardwareSerial &port = *cores_ports[cores_port];
    port.begin(115200);
    const uint8_t number = port.peripheral();
    cores_result[0] = number;
    if (!port || !number) {
        port.end();
        return;
    }
    const int irqn = usart_irqn(number);
    cores_result[3] = irqn >= 0 ? (*(volatile uint8_t *)(0xE000E600u + (uint32_t)irqn) & 1u) : 9u;
    uint32_t sent = 0;
    const uint32_t t0 = micros();
    for (int i = 0; i < 64; i++) {
        sent += (uint32_t)port.write(cores_pattern(i));
    }
    port.flush();
    cores_result[2] = micros() - t0;
    cores_result[1] = sent;

    const int pin = usart_rx_pin(number);
    if (pin < 0) {
        cores_result[4] = 1;
        port.end();
        return;
    }
    const pin_size_t p = (pin_size_t)pin;
    pinMode(p, INPUT_PULLUP);
    delay(1);
    const int up = digitalRead(p);
    pinMode(p, INPUT_PULLDOWN);
    delay(1);
    const int down = digitalRead(p);
    pinMode(p, INPUT);
    if (up == down) {
        cores_result[4] = 1;
        port.end();
        return;
    }
    port.begin(9600);
    digitalWrite(p, HIGH);
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
    delay(5);
    while (port.available()) {
        port.read();
    }
    port.takeErrors();
    bool synced = false;
    for (int i = -1; i < 16; i++) {
        const uint16_t frame = i < 0 ? 0x3FEu : (uint16_t)(0x200u | ((uint16_t)cores_pattern(i) << 1));
        noInterrupts();
        const uint32_t s = micros();
        for (int b = 0; b < 10; b++) {
            digitalWrite(p, (frame >> b) & 1u ? HIGH : LOW);
            while ((uint32_t)(micros() - s) < (uint32_t)((b + 1) * 1000000UL / 9600UL)) {
            }
        }
        interrupts();
        delayMicroseconds(200);
        serial_v3f_take(port, synced);
        if (i < 0) {
            port.takeErrors();
        }
    }
    delay(5);
    serial_v3f_take(port, synced);
    cores_result[7] = port.takeErrors();
    pinMode(p, INPUT);
    port.end();
}

/* V3FPORT <USARTn> <its RX pin or -1>, learnt on the V5F (begin() and end()). */
static void v3f_port(int k)
{
    if (k < 0 || k >= CORES_PORTS) {
        SerialDMSeq.printf("V3FPORT 0 -1\r\n");
        return;
    }
    cores_ports[k]->begin(9600);
    const uint8_t number = cores_ports[k]->peripheral();
    cores_ports[k]->end();
    SerialDMSeq.printf("V3FPORT %u %d\r\n", number, number ? usart_rx_pin(number) : -1);
}

/* V3FSER <USARTn> CLOSED | <USARTn> <sent> <us> <IALLOC> HELD | <USARTn> <sent> <us> <IALLOC> <received>
 * <matching> <errors hex> | 0 BUSY (the V3F did not start or return) */
static void v3f_serial(int k)
{
    if (k < 0 || k >= CORES_PORTS) {
        SerialDMSeq.printf("V3FSER 0 CLOSED\r\n");
        return;
    }
    cores_port = k;
    if (!cores_start(serial_v3f) || !cores_finish(5000)) {
        SerialDMSeq.printf("V3FSER 0 BUSY\r\n");
        return;
    }
    const uint32_t r0 = cores_result[0], r1 = cores_result[1], r2 = cores_result[2], r3 = cores_result[3];
    if (!r1 && !r2) {
        SerialDMSeq.printf("V3FSER %lu CLOSED\r\n", (unsigned long)r0);
    } else if (cores_result[4]) {
        SerialDMSeq.printf("V3FSER %lu %lu %lu %lu HELD\r\n", (unsigned long)r0, (unsigned long)r1,
                           (unsigned long)r2, (unsigned long)r3);
    } else {
        SerialDMSeq.printf("V3FSER %lu %lu %lu %lu %lu %lu %lx\r\n", (unsigned long)r0, (unsigned long)r1,
                           (unsigned long)r2, (unsigned long)r3, (unsigned long)cores_result[5],
                           (unsigned long)cores_result[6], (unsigned long)cores_result[7]);
    }
}

#else   /* CPU Core = V3F: one core runs the sketch, the V5F sleeps */

/* HSEM V3F <take> <again> <release> <release again> <take(20 ms)> <raw read, hex> */
static void hsem_check()
{
    const uint32_t raw = cores_raw_lock();
    const int a = CH32RV.hsemTryTake(CORES_HSEM_CH);
    const int b = CH32RV.hsemTryTake(CORES_HSEM_CH);
    const int c = CH32RV.hsemRelease(CORES_HSEM_CH);
    const int d = CH32RV.hsemRelease(CORES_HSEM_CH);
    const int e = CH32RV.hsemTake(CORES_HSEM_CH, 20);
    CH32RV.hsemRelease(CORES_HSEM_CH);
    SerialDMSeq.printf("HSEM V3F %d %d %d %d %d %08lx\r\n", a, b, c, d, e, (unsigned long)raw);
}

/* IPC V3F <send> <onReceive> <receive>: no other core, all 0. */
static void ipc_check(uint32_t)
{
    uint32_t m;
    const int s = CH32RV.ipcSend(1, 10);
    const int o = CH32RV.ipcOnReceive(nullptr);
    const int r = CH32RV.ipcReceive(m);
    SerialDMSeq.printf("IPC V3F %d %d %d\r\n", s, o, r);
}
#endif
#endif

/* CORES V5F <ports> (two cores: the sketch on the V5F, the V3F to start) | CORES V3F (CPU Core = V3F) | CORES NONE */
static void cores_mode()
{
#if defined(CH32RV_HAS_V3F)
    SerialDMSeq.printf("CORES V5F %d\r\n", CORES_PORTS);
#elif defined(CH32RV_HAS_HSEM)
    SerialDMSeq.printf("CORES V3F\r\n");
#else
    SerialDMSeq.printf("CORES NONE\r\n");
#endif
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
    } else if (!strcmp(argv[0], "canlb") && argc == 2) {
#if defined(CH32RV_CAN1_BASE)
        CAN.setLoopback(true, true);
        const bool ok = CAN.begin((uint32_t)atol(argv[1]));
        int right = 0;
        for (int i = 0; i < 20 && ok; i++) {
            uint8_t d[8];
            for (int k = 0; k < 8; k++) {
                d[k] = (uint8_t)(i * 31 + k);
            }
            const uint32_t id = (i & 1) ? CanExtendedId(0x1ABCDE0u + (uint32_t)i) : CanStandardId(0x100u + (uint32_t)i);
            const uint8_t len = (uint8_t)(i % 9);
            CAN.write(CanMsg(id, len, d));
            const uint32_t t0 = millis();
            while (!CAN.available() && millis() - t0 < 50u) {
            }
            if (CAN.available()) {
                const CanMsg m = CAN.read();
                right += m.id == id && m.data_length == len && !memcmp(m.data, d, len);
            }
        }
        SerialDMSeq.printf("CANLB %d %d %u %u\r\n", ok ? 1 : 0, right, CAN.txErrors(), CAN.rxErrors());
        CAN.end();
#else
        SerialDMSeq.printf("CANLB NONE\r\n");
#endif
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
    } else if (!strcmp(argv[0], "cores")) {
        cores_mode();
#if defined(CH32RV_HAS_HSEM)
    } else if (!strcmp(argv[0], "hsem")) {
        hsem_check();
    } else if (!strcmp(argv[0], "ipc") && argc == 2) {
        ipc_check(strtoul(argv[1], nullptr, 10));
#endif
#if defined(CH32RV_HAS_V3F)
    } else if (!strcmp(argv[0], "v3fport") && argc == 2) {
        v3f_port(atoi(argv[1]));
    } else if (!strcmp(argv[0], "v3fser") && argc == 2) {
        v3f_serial(atoi(argv[1]));
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

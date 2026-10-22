/* CH32RVSyncKit contracts (tests/instrumented/synckit): the IsrSafe parts
 * under a real interrupt. A timer's update interrupt at 10 kHz (whichever
 * timer the timer manager lends) feeds a Queue, a Notify counter and a Ring,
 * and bumps a 64-bit value whose halves always match; loop() drains and
 * checks for as long as asked. No wiring. Console on SerialDMSeq:
 *
 *   ready <nonce>  READY <nonce> <build id> <timer>
 *   run <ms>       RUN <isr calls> <queue sent> <queue dropped> <received> <gaps> <dups>
 *                      <notified> <taken> <ring written> <ring dropped> <ring read> <ring breaks>
 *                      <torn guarded> <torn unguarded> <reads> <max cs us>
 *                  (gaps: sequence jumps not explained by drops; breaks: ring bytes out of order
 *                   beyond its drops; torn: 64-bit reads whose halves differ)
 *   fail <step>    when the timer cannot be had */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVSyncKit.h>
#include <CH32RVTimer.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

using namespace CH32RVSyncKit;

static Queue<uint32_t, 16> queue;
static Notify<> notify;
static Ring<32> ring;
static volatile uint64_t wide;          /* both halves move together: +0x100000001 */
static volatile uint32_t isrCalls, sent, qDropped, notified, ringWritten, ringDropped;
static volatile bool armed;

static CH32RVTimerOwner owner = {&owner, nullptr, nullptr};
static CH32RVTimerLease lease;
static uint8_t timerNumber;

static void onTick(void *)
{
    if (!armed) {
        return;
    }
    isrCalls = isrCalls + 1;
    const uint32_t seq = sent;
    if (queue.trySend(seq)) {
        sent = seq + 1;
    } else {
        qDropped = qDropped + 1;
        sent = seq + 1;                 /* the number is used up either way */
    }
    if (notify.notify()) {
        notified = notified + 1;
    }
    const uint8_t b = (uint8_t)ringWritten;
    if (ring.write(b)) {
        ringWritten = ringWritten + 1;
    } else {
        ringDropped = ringDropped + 1;
    }
    wide = wide + 0x100000001ull;
}

static bool startTimer()
{
    CH32RVTimerRequest req = {CH32RV_TIMER_ANY, CH32RV_TIMER_WHOLE,
                              {(uint16_t)((F_CPU / 1000000u) - 1u), 99u, 0}};   /* 1 MHz / 100 = 10 kHz */
    lease = ch32TimerTryAcquire(&req, &owner);
    if (!ch32TimerLeaseValid(&lease) || !ch32TimerApplyBase(&lease) ||
        !ch32TimerAttachUpdateInterrupt(&lease, onTick, nullptr) || !ch32TimerStart(&lease)) {
        return false;
    }
    timerNumber = lease.timer;
    return true;
}

static void say(const char *head, const uint32_t *v, uint8_t n)
{
    SerialDMSeq.print(head);
    for (uint8_t i = 0; i < n; i++) {
        SerialDMSeq.print(' ');
        SerialDMSeq.print((unsigned long)v[i]);
    }
    SerialDMSeq.println();
}

static void run(uint32_t ms)
{
    {
        CriticalSection cs;
        isrCalls = sent = qDropped = notified = ringWritten = ringDropped = 0;
        wide = 0;
        queue.clear();
        notify.tryTakeAll();
        ring.clear();
    }
    uint32_t received = 0, gaps = 0, dups = 0, taken = 0, ringRead = 0, breaks = 0;
    uint32_t tornGuarded = 0, tornUnguarded = 0, reads = 0, maxCsUs = 0;
    uint32_t expect = 0;
    uint8_t ringExpect = 0;
    armed = true;
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < ms) {
        uint32_t v;
        while (queue.tryReceive(v)) {
            if (v < expect) {
                dups++;
            } else {
                gaps += v - expect;     /* accounted against drops below */
                expect = v + 1;
            }
            received++;
        }
        taken += notify.tryTakeAll();
        int c;
        while ((c = ring.read()) >= 0) {
            if ((uint8_t)c != ringExpect) {
                breaks++;
            }
            ringExpect = (uint8_t)(c + 1);
            ringRead++;
        }
        uint64_t a;
        const uint32_t s = micros();
        {
            CriticalSection cs;
            a = wide;
        }
        const uint32_t d = micros() - s;
        if (d > maxCsUs) {
            maxCsUs = d;
        }
        if ((uint32_t)a != (uint32_t)(a >> 32)) {
            tornGuarded++;
        }
        const uint64_t u = wide;        /* the negative control: no guard */
        if ((uint32_t)u != (uint32_t)(u >> 32)) {
            tornUnguarded++;
        }
        reads++;
    }
    armed = false;
    /* What is still queued counts as received for the accounting. */
    uint32_t v;
    while (queue.tryReceive(v)) {
        if (v >= expect) {
            gaps += v - expect;
            expect = v + 1;
        } else {
            dups++;
        }
        received++;
    }
    taken += notify.tryTakeAll();
    int c;
    while ((c = ring.read()) >= 0) {
        if ((uint8_t)c != ringExpect) {
            breaks++;
        }
        ringExpect = (uint8_t)(c + 1);
        ringRead++;
    }
    /* Every sequence jump must be a dropped number. */
    const uint32_t q = qDropped;
    const uint32_t unexplained = gaps > q ? gaps - q : q - gaps;
    const uint32_t out[16] = {isrCalls, sent, q, received, unexplained, dups, notified, taken,
                              ringWritten, ringDropped, ringRead, breaks, tornGuarded, tornUnguarded,
                              reads, maxCsUs};
    say("RUN", out, 16);
}

static char line[48];
static uint8_t used;

void setup()
{
    SerialDMSeq.begin(115200);
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
        if (strncmp(line, "ready ", 6) == 0) {
            SerialDMSeq.print("READY ");
            SerialDMSeq.print(line + 6);
            SerialDMSeq.print(' ');
            SerialDMSeq.print(BENCH_BUILD_ID);
            SerialDMSeq.print(' ');
            SerialDMSeq.println(timerNumber ? timerNumber : (startTimer() ? timerNumber : 0));
        } else if (strncmp(line, "run ", 4) == 0) {
            if (!timerNumber && !startTimer()) {
                SerialDMSeq.println("FAIL timer");
            } else {
                run(strtoul(line + 4, nullptr, 10));
            }
        } else if (used == 0 && line[0]) {
            SerialDMSeq.println("ERROR COMMAND");
        }
    }
}

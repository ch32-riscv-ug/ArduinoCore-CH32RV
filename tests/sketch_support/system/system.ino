/* CH32RV's chip-level calls (tests/instrumented/system): the reset reasons,
 * the independent watchdog and the sleeps, on the lines the bench labels.
 * Pins are the sketch's pin numbers. Console on SerialDMSeq:
 *
 *   ready <nonce>                READY <nonce> <build id> <reset reason> <wakeup cause>
 *                                (resetReason() and wakeupCause() of this boot, as numbers)
 *   restart                      CH32RV.restart()
 *   wdt <ms> <every> <for> <pin> the pin low (output), wdtEnable(ms), then fed every <every>
 *                                ms for <for> ms: WDT <1 / 0> at once, FED when the feeding
 *                                ends; then the pin goes high and nothing feeds it, so the
 *                                watchdog resets the chip (the pin falls with the reset: its
 *                                high time is the timeout)
 *   tsleep <us> <pin>            the pin high, enableTimerWakeup(us), lightSleep(), the pin
 *                                low: TSLEEP <result> <cause> <millis() across it>
 *   psleep <wake pin> <pin>      the pin high, enablePinWakeup(wake pin, LOW), lightSleep(),
 *                                the pin low: PSLEEP <result> <cause> <millis() across it>
 *   dsleep <us> <pin>            the pin high, enableTimerWakeup(us), deepSleep(): DSLEEP
 *                                <result> only when it could not sleep; otherwise the chip
 *                                starts again (READY says the wakeup cause) */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static char line[64];
static uint8_t used;
static int reason = -1, cause = -1;

static void reply(char *command)
{
    char *argv[6];
    int argc = 0;
    for (char *t = strtok(command, " "); t && argc < 6; t = strtok(nullptr, " ")) {
        argv[argc++] = t;
    }
    if (!argc) {
        return;
    }
    if (!strcmp(argv[0], "ready") && argc == 2) {
        SerialDMSeq.printf("READY %s %s %d %d\r\n", argv[1], BENCH_BUILD_ID, reason, cause);
    } else if (!strcmp(argv[0], "restart")) {
        SerialDMSeq.flush();
        CH32RV.restart();
    } else if (!strcmp(argv[0], "wdt") && argc == 5) {
        const uint32_t ms = (uint32_t)atol(argv[1]), every = (uint32_t)atol(argv[2]), span = (uint32_t)atol(argv[3]);
        const pin_size_t pin = (pin_size_t)atoi(argv[4]);
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
        const bool ok = CH32RV.wdtEnable(ms);
        SerialDMSeq.printf("WDT %d\r\n", ok ? 1 : 0);
        if (!ok) {
            return;
        }
        const uint32_t start = millis();
        while (millis() - start < span) {
            CH32RV.wdtFeed();
            delay(every);
        }
        SerialDMSeq.printf("FED\r\n");
        SerialDMSeq.flush();
        CH32RV.wdtFeed();
        digitalWrite(pin, HIGH);          /* from the last feed until the reset */
        for (;;) {
        }
    } else if (!strcmp(argv[0], "tsleep") && argc == 3) {
        const pin_size_t pin = (pin_size_t)atoi(argv[2]);
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
        CH32RV.disableWakeup();
        CH32RV.enableTimerWakeup((uint64_t)atol(argv[1]));
        SerialDMSeq.flush();
        const uint32_t before = millis();
        const int result = CH32RV.lightSleep();
        const uint32_t slept = millis() - before;
        digitalWrite(pin, LOW);
        SerialDMSeq.printf("TSLEEP %d %d %lu\r\n", result, (int)CH32RV.wakeupCause(), (unsigned long)slept);
    } else if (!strcmp(argv[0], "psleep") && argc == 3) {
        const int wake = atoi(argv[1]);
        const pin_size_t pin = (pin_size_t)atoi(argv[2]);
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
        pinMode(wake, INPUT);
        CH32RV.disableWakeup();
        CH32RV.enablePinWakeup(wake, LOW);
        SerialDMSeq.printf("PSLEEP READY\r\n");
        SerialDMSeq.flush();
        const uint32_t before = millis();
        const int result = CH32RV.lightSleep();
        const uint32_t slept = millis() - before;
        digitalWrite(pin, LOW);
        SerialDMSeq.printf("PSLEEP %d %d %lu\r\n", result, (int)CH32RV.wakeupCause(), (unsigned long)slept);
    } else if (!strcmp(argv[0], "dsleep") && argc == 3) {
        const pin_size_t pin = (pin_size_t)atoi(argv[2]);
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
        CH32RV.disableWakeup();
        CH32RV.enableTimerWakeup((uint64_t)atol(argv[1]));
        SerialDMSeq.flush();
        const int result = CH32RV.deepSleep();
        digitalWrite(pin, LOW);
        SerialDMSeq.printf("DSLEEP %d\r\n", result);
    } else {
        SerialDMSeq.printf("UNKNOWN %s\r\n", argv[0]);
    }
}

void setup()
{
    reason = (int)CH32RV.resetReason();
    cause = (int)CH32RV.wakeupCause();
    SerialDMSeq.begin(115200);
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

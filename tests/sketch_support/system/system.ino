/* CH32RV's chip-level calls (tests/instrumented/system): the reset reasons,
 * the independent watchdog and the sleeps, on the lines the bench labels.
 * Pins are the sketch's pin numbers. Console on CONSOLE (bench_console.h):
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
 *                                starts again (READY says the wakeup cause)
 *   wwdg <early|late> <pin>      the window watchdog: T = min(wwdgMaxTimeoutUs(), 40 ms), the
 *                                window T / 2, an early-warning callback that counts:
 *                                WWDG <T us> <window us> <wwdgMaxTimeoutUs()> (WWDG REFUSED
 *                                after it when wwdgEnable() says false); then fed
 *                                every 3T/4 for 300 ms (inside the window), the pin goes high at
 *                                the last feed, and either one more feed T/8 after it (early:
 *                                before the window - a reset) or none (late: the timeout). The
 *                                pin falls with the reset: its high time is T/8 or T
 *   ewi                          EWI <early-warning callbacks the boot before this one counted>
 *                                <us> <counter steps>: the WWDG counter's rate before its first feed
 *   mcohz                        MCOHZ <the MCO pad, 0 when none> <source>:<its Hz> ... (SYSCLK and HSI where the
 *                                series offers them)
 *   mco <source>                 CH32RV.setClockOutput(source): MCO <1 / 0>
 *   bkpw <seed>                  every backup word i := seed + 257 i: BKPW <count> <keeps power>
 *   bkpr <seed>                  BKPR <count> <words that read seed + 257 i>
 *   alarm <s> <pin>              the pin high, setAlarm(time() + s) with a callback that lets the
 *                                pin low: ALARM <1 / 0>, then ALARM DONE <ms until the callback,
 *                                or 0 after s + 3 s> <getAlarm() after it>
 *   asleep <s> <pin>             the pin high, setAlarm(time() + s), enableAlarmWakeup(), a 10 s
 *                                timer as a backstop, lightSleep(), the pin low: ASLEEP <result>
 *                                <cause> <millis() across it> (ASLEEP REFUSED without an RTC)
 *   adeep <s> <pin>              the same with deepSleep(): ADEEP <result> only when it could not
 *                                sleep; otherwise the chip starts again (READY says the cause) */
#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
#include "internal/ch32rv_clock.h"   // CH32RV_CLOCK_SYSCLK_HZ, CH32RV_HSI_HZ: what mcohz reports
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static char line[64];
static uint8_t used;
static int reason = -1, cause = -1;

/* The window watchdog's early warnings, across the reset that follows them. */
static volatile struct {
    uint32_t magic;
    uint32_t count;
    uint32_t span_us;      /* the counter's own rate, before the first feed: */
    uint32_t steps;        /* steps counted in span_us */
} ewi_record __attribute__((section(".noinit")));

/* Feeds wait until `due` us after `start`; the first wait also watches the
 * counter (WWDG_CTLR's T) and keeps its rate, across a reset, in ewi_record. */
static void wwdg_wait(uint32_t start, uint32_t due, bool watch)
{
    const uint32_t c0 = *(volatile uint32_t *)0x40002C00u & 0x7Fu;
    uint32_t last = c0;
    while ((uint32_t)(micros() - start) < due) {
        if (watch) {
            const uint32_t c = *(volatile uint32_t *)0x40002C00u & 0x7Fu;
            if (c != last) {
                last = c;
                ewi_record.steps = c0 - c;
                ewi_record.span_us = micros() - start;
            }
        }
    }
}
#define EWI_MAGIC 0xE1A1C0DEu
static uint32_t ewi_seen, ewi_span, ewi_steps;

static void on_early_warning()
{
    if (ewi_record.magic != EWI_MAGIC) {
        ewi_record.magic = EWI_MAGIC;
        ewi_record.count = 0;
        ewi_record.span_us = 0;
        ewi_record.steps = 0;
    }
    ewi_record.count++;
}

static volatile pin_size_t alarm_pin;
static volatile uint32_t alarm_at;
static volatile bool alarm_fired;

static void on_alarm()
{
    digitalWrite(alarm_pin, LOW);
    alarm_at = millis();
    alarm_fired = true;
}

static void wwdg(bool early, pin_size_t pin)
{
    const uint32_t max = CH32RV.wwdgMaxTimeoutUs();
    const uint32_t t = max < 40000u ? max : 40000u, w = t / 2u;
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
    ewi_record.magic = EWI_MAGIC;
    ewi_record.count = 0;
    ewi_record.span_us = 0;
    ewi_record.steps = 0;
    /* Said before it starts: nothing slow may come between two feeds. */
    CONSOLE.printf("WWDG %lu %lu %lu\r\n", (unsigned long)t, (unsigned long)w, (unsigned long)max);
    CONSOLE.flush();
    if (!CH32RV.wwdgEnable(t, w, on_early_warning)) {
        CONSOLE.printf("WWDG REFUSED\r\n");
        return;
    }
    const uint32_t start = millis();
    bool first = true;
    while (millis() - start < 300u) {
        wwdg_wait(micros(), t * 3u / 4u, first);
        first = false;
        CH32RV.wwdgFeed();
    }
    digitalWrite(pin, HIGH);              /* from the last feed until the reset */
    if (early) {
        delayMicroseconds(t / 8u);
        CH32RV.wwdgFeed();                /* before the window opens: a reset */
    }
    for (;;) {
    }
}

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
        CONSOLE.printf("READY %s %s %d %d\r\n", argv[1], BENCH_BUILD_ID, reason, cause);
    } else if (!strcmp(argv[0], "restart")) {
        CONSOLE.flush();
        CH32RV.restart();
    } else if (!strcmp(argv[0], "wdt") && argc == 5) {
        const uint32_t ms = (uint32_t)atol(argv[1]), every = (uint32_t)atol(argv[2]), span = (uint32_t)atol(argv[3]);
        const pin_size_t pin = (pin_size_t)atoi(argv[4]);
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
        const bool ok = CH32RV.wdtEnable(ms);
        CONSOLE.printf("WDT %d\r\n", ok ? 1 : 0);
        if (!ok) {
            return;
        }
        const uint32_t start = millis();
        while (millis() - start < span) {
            CH32RV.wdtFeed();
            delay(every);
        }
        CONSOLE.printf("FED\r\n");
        CONSOLE.flush();
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
        CONSOLE.flush();
        const uint32_t before = millis();
        const int result = CH32RV.lightSleep();
        const uint32_t slept = millis() - before;
        digitalWrite(pin, LOW);
        CONSOLE.printf("TSLEEP %d %d %lu\r\n", result, (int)CH32RV.wakeupCause(), (unsigned long)slept);
    } else if (!strcmp(argv[0], "psleep") && argc == 3) {
        const int wake = atoi(argv[1]);
        const pin_size_t pin = (pin_size_t)atoi(argv[2]);
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
        pinMode(wake, INPUT);
        CH32RV.disableWakeup();
        CH32RV.enablePinWakeup(wake, LOW);
        CONSOLE.printf("PSLEEP READY\r\n");
        CONSOLE.flush();
        const uint32_t before = millis();
        const int result = CH32RV.lightSleep();
        const uint32_t slept = millis() - before;
        digitalWrite(pin, LOW);
        CONSOLE.printf("PSLEEP %d %d %lu\r\n", result, (int)CH32RV.wakeupCause(), (unsigned long)slept);
    } else if (!strcmp(argv[0], "dsleep") && argc == 3) {
        const pin_size_t pin = (pin_size_t)atoi(argv[2]);
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
        CH32RV.disableWakeup();
        CH32RV.enableTimerWakeup((uint64_t)atol(argv[1]));
        CONSOLE.flush();
        const int result = CH32RV.deepSleep();
        digitalWrite(pin, LOW);
        CONSOLE.printf("DSLEEP %d\r\n", result);
    } else if (!strcmp(argv[0], "wwdg") && argc == 3) {
        wwdg(!strcmp(argv[1], "early"), (pin_size_t)atoi(argv[2]));
    } else if (!strcmp(argv[0], "ewi")) {
        CONSOLE.printf("EWI %lu %lu %lu\r\n", (unsigned long)ewi_seen, (unsigned long)ewi_span, (unsigned long)ewi_steps);
    } else if (!strcmp(argv[0], "bkpw") && argc == 2) {
        const uint16_t seed = (uint16_t)strtoul(argv[1], nullptr, 0);
        const uint8_t n = CH32RV.backupRegisterCount();
        for (uint8_t i = 0; i < n; i++) {
            CH32RV.setBackupRegister(i, (uint16_t)(seed + 257u * i));
        }
        CONSOLE.printf("BKPW %d %d\r\n", n, CH32RV.backupRegistersKeepPower() ? 1 : 0);
    } else if (!strcmp(argv[0], "bkpr") && argc == 2) {
        const uint16_t seed = (uint16_t)strtoul(argv[1], nullptr, 0);
        const uint8_t n = CH32RV.backupRegisterCount();
        int same = 0;
        for (uint8_t i = 0; i < n; i++) {
            same += CH32RV.getBackupRegister(i) == (uint16_t)(seed + 257u * i);
        }
        CONSOLE.printf("BKPR %d %d\r\n", n, same);
    } else if (!strcmp(argv[0], "alarm") && argc == 3) {
        const pin_size_t pin = (pin_size_t)atoi(argv[2]);
        const uint32_t s = (uint32_t)atol(argv[1]);
        alarm_pin = pin;
        alarm_fired = false;
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
        const uint32_t t0 = millis();
        const bool ok = CH32RV.setAlarm(time(nullptr) + (time_t)s, on_alarm);
        CONSOLE.printf("ALARM %d\r\n", ok ? 1 : 0);
        if (!ok) {
            digitalWrite(pin, LOW);
            return;
        }
        while (!alarm_fired && millis() - t0 < (s + 3u) * 1000u) {
        }
        digitalWrite(pin, LOW);
        CONSOLE.printf("ALARM DONE %lu %ld\r\n", alarm_fired ? (unsigned long)(alarm_at - t0) : 0ul,
                           (long)CH32RV.getAlarm());
    } else if ((!strcmp(argv[0], "asleep") || !strcmp(argv[0], "adeep")) && argc == 3) {
        const bool deep = argv[0][1] == 'd';
        const pin_size_t pin = (pin_size_t)atoi(argv[2]);
        const uint32_t s = (uint32_t)atol(argv[1]);
        CH32RV.disableWakeup();
        if (!CH32RV.setAlarm(time(nullptr) + (time_t)s) || !CH32RV.enableAlarmWakeup()) {
            CONSOLE.printf(deep ? "ADEEP REFUSED\r\n" : "ASLEEP REFUSED\r\n");
            return;
        }
        CH32RV.enableTimerWakeup(10u * 1000000u);     /* a backstop, should the alarm not come */
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
        CONSOLE.flush();
        if (deep) {
            const int result = CH32RV.deepSleep();
            digitalWrite(pin, LOW);
            CONSOLE.printf("ADEEP %d\r\n", result);
            return;
        }
        const uint32_t before = millis();
        const int result = CH32RV.lightSleep();
        const uint32_t slept = millis() - before;
        digitalWrite(pin, LOW);
        CH32RV.disableWakeup();
        CONSOLE.printf("ASLEEP %d %d %lu\r\n", result, (int)CH32RV.wakeupCause(), (unsigned long)slept);
    } else if (!strcmp(argv[0], "mcohz") && argc == 1) {
#if defined(CH32RV_MCO_PIN)
        CONSOLE.printf("MCOHZ %u", (unsigned)CH32RV_MCO_PIN);
#if defined(CH32RV_MCO_SYSCLK)
        CONSOLE.printf(" %u:%lu", (unsigned)CH32RV_MCO_SYSCLK, (unsigned long)CH32RV_CLOCK_SYSCLK_HZ);
#endif
#if defined(CH32RV_MCO_HSI)
        CONSOLE.printf(" %u:%lu", (unsigned)CH32RV_MCO_HSI, (unsigned long)CH32RV_HSI_HZ);
#endif
        CONSOLE.printf("\r\n");
#else
        CONSOLE.printf("MCOHZ 0\r\n");
#endif
    } else if (!strcmp(argv[0], "mco") && argc == 2) {
        CONSOLE.printf("MCO %d\r\n", CH32RV.setClockOutput((uint8_t)atoi(argv[1])) ? 1 : 0);
    } else {
        CONSOLE.printf("UNKNOWN %s\r\n", argv[0]);
    }
}

void setup()
{
    reason = (int)CH32RV.resetReason();
    cause = (int)CH32RV.wakeupCause();
    if (ewi_record.magic == EWI_MAGIC) {
        ewi_seen = ewi_record.count;
        ewi_span = ewi_record.span_us;
        ewi_steps = ewi_record.steps;
    }
    ewi_record.magic = 0;
    CONSOLE_BEGIN(115200);
    CONSOLE.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    CONSOLE.println(CH32RV.resetReasonName());
}

void loop()
{
    while (CONSOLE.available()) {
        const int c = CONSOLE.read();
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

/* The chip-level calls with no wiring (tests/instrumented/selfsys), beside
 * selfcheck and selfbus. Console on CONSOLE (bench_console.h):
 *
 *   ready <nonce>             READY <nonce> <build id>
 *   id                        ID <UNIID1 UNIID2 as 16 hex digits, or -> <getFlashChipSize() or 0> <getChipModel()>
 *   temp                      TEMP <analogReadTemp() x 10, or -9999 where there is no sensor> <the factory's
 *                             calibration word in hex, or ->
 *   restart                   RESTARTING, then CH32RV.restart() (the next STARTUP line says why it booted)
 *   wdt <ms>                  wdtEnable(ms), fed every 50 ms for 300 ms: WDT FED; then not fed: WDT LET GO,
 *                             and the watchdog resets the chip (or WDT REFUSED)
 *   sleep <ms>                lightSleep() with a timer wake after ms:
 *                             SLEEP <CH32RVSleepResult> <wakeupCause()> <millis() across it>
 *   adcpins                   ADCPINS <pin> ...: every bonded pad with an ADC channel
 *   adc <pin>                 the pad driven 1 then 0, each converted after another channel:
 *                             ADC <pin> <channel> <at 1> <at 0> <full scale>, ADC <pin> HELD / NONE
 *   rtc                       RTC <gettimeofday() seconds> <microseconds> <1: it counts LSE, 0: LSI> on the RTC
 *                             families, else RTC NONE
 *   pad <pin>                 PAD <pin> <1 when the board holds it: the pulls do not move it>
 *   rgbhw <pin>               RGBHW <pin> <1 when rgbLedWrite() sends there by the WS2812 block>
 *   rgbirq <pin> <leds>       ch32rv_rgb_led_write() of leds (<= 100) LEDs while a hardware timer's 100 us
 *                             alarm stamps its interrupts: RGBIRQ <hardware 1 / 0> <interrupts during the call>
 *                             <the longest gap between them, us> <the call's us>, or RGBIRQ NONE (no WS2812
 *                             block in the series, or no timer)
 *   tone <pin> <hz>           tone(pin, hz), the pad read for 100 ms: TONE <rising edges> <samples>
 *   servo <pin> <us>          a Servo on pin at us, pulseIn() three times: SERVO <width> <width> <width>
 *   wwdg <early|late>         the window watchdog, T = min(wwdgMaxTimeoutUs(), 40 ms), window T / 2:
 *                             WWDG <T us> <window us> <max us>; fed every 3T/4 for 300 ms, then once
 *                             more T/8 after the last feed (early: before the window) or never
 *                             (late); either resets the chip (WWDG REFUSED when wwdgEnable() is false)
 *   ewi                       EWI <early-warning callbacks the boot before this one counted> <us>
 *                             <counter steps>: the WWDG counter's rate before its first feed
 *   bkpw <seed>               every backup word i := seed + 257 i: BKPW <count> <keeps power>
 *   bkpr <seed>               BKPR <count> <words that read seed + 257 i>
 *   alarm <s>                 setAlarm(time() + s) with a callback: ALARM <1 / 0> <ms from the call to
 *                             the callback, 0 if none in s + 3 s> <getAlarm() after it>
 *   asleep <s>                setAlarm(time() + s), enableAlarmWakeup(), a 10 s timer as a backstop,
 *                             lightSleep(): ASLEEP <result> <wakeupCause()> <millis() across it>,
 *                             or ASLEEP REFUSED without an RTC
 *
 * STARTUP <resetReasonName()> <wakeupCause()> comes first after every boot;
 * `why` asks for the same again (WHY ... <RCC_RSTSCKR at boot, hex>): the probe's console may miss the
 * first bytes after a reset. */
#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
#include <Servo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_rtc.h"
#include "internal/ch32rv_ticks.h"

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

/* Numbers are read with atol() / atoi() only (the host sends decimal): strtoul() beside them cost V003 262
 * bytes, and the sketch overflowed its 16 KB when pulseIn() took a timer's capture (2026-11-17). */

static char line[64];
static uint8_t used;

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
/* RCC_RSTSCKR as it was at boot, before resetReason() reads and clears its flags: the WHY line's last field, so a
 * reason of "unknown" shows which flags (none?) the reset left. */
static uint32_t boot_rstsckr;

#define EWI_MAGIC 0xE1A1C0DEu
static uint32_t ewi_seen, ewi_span, ewi_steps;

static void on_early_warning()
{
    ewi_record.count++;
}

#ifdef CH32RV_HAS_RTC
static volatile uint32_t alarm_at;
static volatile bool alarm_fired;

static void on_alarm()
{
    alarm_at = millis();
    alarm_fired = true;
}
#endif

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

#if defined(NUM_ANALOG_INPUTS) && defined(CH32RV_ADC_BITS) && CH32RV_ADC_CLOCK_OK
#define SELFSYS_ADC 1
/* ADC1's base: the instance a pad reaches is 1 everywhere but on the parts
 * with a second ADC, whose pads this check leaves out (ADC 1 only). */
#ifdef CH32RV_PIN_TO_ADC_INSTANCE
#define SELFSYS_ADC_INSTANCE(p) CH32RV_PIN_TO_ADC_INSTANCE(p)
#else
#define SELFSYS_ADC_INSTANCE(p) ((void)(p), 1u)
#endif

/* One conversion of `channel` on ADC1, the pad left as it is (wiring_analog.c's
 * ch32rv_adc_convert_raw). */
static uint32_t adc_convert(uint32_t channel)
{
    const uint32_t base = CH32RV_ADC1_BASE;
    CH32RV_ADC_RSQR1_AT(base) = 0;
    CH32RV_ADC_RSQR3_AT(base) = channel;
    CH32RV_ADC_CTLR2_AT(base) |= CH32RV_ADC_CTLR2_SWSTART;
    while ((CH32RV_ADC_STATR_AT(base) & CH32RV_ADC_STATR_EOC) == 0u) {
    }
    return CH32RV_ADC_RDATAR_AT(base) & ((1u << CH32RV_ADC_BITS) - 1u);
}

/* Another channel first, so a channel that returns the previous one's value
 * (CH32X035's errata on IN3 / IN7 / IN11 / IN15) shows: the internal
 * reference where there is one. */
static void adc_other(uint32_t channel)
{
#ifdef CH32RV_ADC_VREFINT_CHANNEL
    (void)channel;
    (void)adc_convert(CH32RV_ADC_VREFINT_CHANNEL);
#else
    (void)adc_convert(channel == 0u ? 1u : 0u);
#endif
}

/* The pad driven 1 and 0 as an output, converted each time after another
 * channel: ADC <pin> <channel> <reading at 1> <reading at 0> <full scale>,
 * ADC <pin> HELD, or ADC <pin> NONE (not an ADC1 pad). */
static void adc_pad(int pin)
{
    const pin_size_t p = (pin_size_t)pin;
    const uint32_t channel = CH32RV_PIN_TO_ADC_CHANNEL(p);
    if (channel == NOT_AN_ANALOG_PIN || SELFSYS_ADC_INSTANCE(p) != 1u) {
        CONSOLE.printf("ADC %d NONE\r\n", pin);
        return;
    }
    if (held(p)) {
        CONSOLE.printf("ADC %d HELD\r\n", pin);
        return;
    }
    (void)analogRead(p);                         /* the ADC started and calibrated */
    uint32_t level[2];
    for (int v = 1; v >= 0; v--) {
        digitalWrite(p, v ? HIGH : LOW);
        pinMode(p, OUTPUT);
        digitalWrite(p, v ? HIGH : LOW);
        delayMicroseconds(50);
        uint32_t sum = 0;
        for (int i = 0; i < 4; i++) {
            adc_other(channel);
            sum += adc_convert(channel);
        }
        level[v] = sum / 4u;
    }
    pinMode(p, INPUT);
    CONSOLE.printf("ADC %d %lu %lu %lu %d\r\n", pin, (unsigned long)channel, (unsigned long)level[1],
                       (unsigned long)level[0], (1 << CH32RV_ADC_BITS) - 1);
}
#endif

#if CH32RV_HAS_ARGB
/* Whether interrupts run while ch32rv_rgb_led_write() sends: a hardware
 * timer's 100 us alarm, each interrupt stamped with SysTick. The count alone
 * cannot tell: alarms held off by masked interrupts are not lost but caught
 * up (ch32rv_hw_timer.c raises an overdue alarm again at once), so a frame
 * with interrupts off counts the same 30 - all in a burst after it. The
 * longest gap between interrupts (from the call's start to its end) can:
 * about the alarm's 100 us while they run, the whole frame while they are
 * held. The series with the WS2812 block only (V003's 16 KB has no room for
 * the timer). */
static volatile uint32_t rgb_irqs, rgb_last, rgb_gap;
static volatile bool rgb_timing;

static void rgb_tick()
{
    const uint32_t now = ch32rv_ticks();
    if (rgb_timing) {
        if (now - rgb_last > rgb_gap) {
            rgb_gap = now - rgb_last;
        }
        rgb_last = now;
        rgb_irqs++;
    }
}

static void rgb_irq(pin_size_t p, int leds)
{
    static uint8_t frame[3 * 100];
    if (leds < 1 || leds > 100) {
        leds = 100;
    }
    for (int i = 0; i < 3 * leds; i++) {
        frame[i] = (uint8_t)(0x5A ^ i);
    }
    hw_timer_t *t = timerBegin(1000000);
    if (t == nullptr) {
        CONSOLE.printf("RGBIRQ NONE\r\n");
        return;
    }
    timerAttachInterrupt(t, rgb_tick);
    timerAlarm(t, 100, true, 0);
    delay(2);
    /* Stamps from the call's start; the interrupts are counted until the
     * call has returned (the catch-up burst of a masked frame lands inside
     * the call, at its end, and is counted: the gap is what tells). */
    const uint32_t start = ch32rv_ticks();
    noInterrupts();
    rgb_irqs = 0;
    rgb_gap = 0;
    rgb_last = start;
    rgb_timing = true;
    interrupts();
    ch32rv_rgb_led_write(p, frame, (size_t)(3 * leds));
    const uint32_t end = ch32rv_ticks();
    noInterrupts();
    rgb_timing = false;
    const uint32_t n = rgb_irqs;
    const uint32_t gap = end - rgb_last > rgb_gap ? end - rgb_last : rgb_gap;
    interrupts();
    timerEnd(t);
    delayMicroseconds(300);
    pinMode(p, INPUT);
    const uint32_t per_us = CH32RV_SYSTICK_HZ / 1000000u;
    CONSOLE.printf("RGBIRQ %d %lu %lu %lu\r\n", ch32rv_rgb_led_is_hardware(p) ? 1 : 0, (unsigned long)n,
                       (unsigned long)(gap / per_us), (unsigned long)((end - start) / per_us));
}
#endif

static void reply(char *command)
{
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
        CONSOLE.printf("READY %s %s\r\n", argv[1], BENCH_BUILD_ID);
    } else if (!strcmp(argv[0], "id")) {
#ifdef CH32RV_HAS_UNIQUE_ID
        const uint64_t uid = CH32RV.getUniqueId();
        CONSOLE.printf("ID %08lx%08lx %lu %s\r\n", (unsigned long)(uint32_t)uid, (unsigned long)(uint32_t)(uid >> 32),
                           (unsigned long)CH32RV.getFlashChipSize(), CH32RV.getChipModel());
#else
        CONSOLE.printf("ID - 0 %s\r\n", CH32RV.getChipModel());
#endif
    } else if (!strcmp(argv[0], "temp")) {
#if CH32RV_ADC_CLOCK_OK
        const float t = analogReadTemp();
#else
        const float t = NAN;                     /* no ADC at this CPU Clock: a build error to call */
#endif
#ifdef CH32RV_ADC_TEMP_CAL_ADDR
        CONSOLE.printf("TEMP %d %08lx\r\n", isnan(t) ? -9999 : (int)lroundf(t * 10.0f),
                           (unsigned long)*(const volatile uint32_t *)CH32RV_ADC_TEMP_CAL_ADDR);
#else
        CONSOLE.printf("TEMP %d -\r\n", isnan(t) ? -9999 : (int)lroundf(t * 10.0f));
#endif
    } else if (!strcmp(argv[0], "why")) {
        CONSOLE.printf("WHY %s %d %08lx\r\n", CH32RV.resetReasonName(), (int)CH32RV.wakeupCause(),
                           (unsigned long)boot_rstsckr);
    } else if (!strcmp(argv[0], "restart")) {
        CONSOLE.printf("RESTARTING\r\n");
        delay(50);
        CH32RV.restart();
    } else if (!strcmp(argv[0], "wdt") && argc == 2) {
        if (!CH32RV.wdtEnable((uint32_t)atol(argv[1]))) {
            CONSOLE.printf("WDT REFUSED\r\n");
            return;
        }
        for (int i = 0; i < 6; i++) {
            delay(50);
            CH32RV.wdtFeed();
        }
        CONSOLE.printf("WDT FED\r\n");
        CONSOLE.printf("WDT LET GO\r\n");
        for (;;) {
        }
    } else if (!strcmp(argv[0], "sleep") && argc == 2) {
        CH32RV.enableTimerWakeup((uint64_t)(uint32_t)atol(argv[1]) * 1000u);
        const uint32_t t0 = millis();
        const CH32RVSleepResult r = CH32RV.lightSleep();
        const uint32_t took = millis() - t0;
        const CH32RVWakeupCause cause = CH32RV.wakeupCause();
        CH32RV.disableWakeup();
        CONSOLE.printf("SLEEP %d %d %lu\r\n", (int)r, (int)cause, (unsigned long)took);
    } else if (!strcmp(argv[0], "adcpins")) {
        CONSOLE.printf("ADCPINS");
#if SELFSYS_ADC
        for (uint8_t port = 0; port < CH32RV_PORT_COUNT; port++) {
            const uint32_t mask = CH32RV_PORT_MASK(port);
            for (uint8_t bit = 0; bit < 32; bit++) {
                const pin_size_t p = (pin_size_t)CH32RV_PIN(port, bit);
                if ((mask & (1ul << bit)) && CH32RV_PIN_TO_ADC_CHANNEL(p) != NOT_AN_ANALOG_PIN) {
                    CONSOLE.printf(" %d", (int)p);
                }
            }
        }
#endif
        CONSOLE.printf("\r\n");
#if SELFSYS_ADC
    } else if (!strcmp(argv[0], "adc") && argc == 2) {
        adc_pad(atoi(argv[1]));
#endif
    } else if (!strcmp(argv[0], "rtc")) {
#ifdef CH32RV_HAS_RTC
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        CONSOLE.printf("RTC %lu %lu %d\r\n", (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec,
                           ch32rv_rtc_on_lse() ? 1 : 0);
#else
        CONSOLE.printf("RTC NONE\r\n");
#endif
    } else if (!strcmp(argv[0], "pad") && argc == 2) {
        CONSOLE.printf("PAD %d %d\r\n", atoi(argv[1]), held((pin_size_t)atoi(argv[1])) ? 1 : 0);
    } else if (!strcmp(argv[0], "rgbhw") && argc == 2) {
        CONSOLE.printf("RGBHW %d %d\r\n", atoi(argv[1]),
                           ch32rv_rgb_led_is_hardware((pin_size_t)atoi(argv[1])) ? 1 : 0);
    } else if (!strcmp(argv[0], "rgbirq") && argc == 3) {
#if CH32RV_HAS_ARGB
        rgb_irq((pin_size_t)atoi(argv[1]), atoi(argv[2]));
#else
        CONSOLE.printf("RGBIRQ NONE\r\n");
#endif
    } else if (!strcmp(argv[0], "tone") && argc == 3) {
        const pin_size_t p = (pin_size_t)atoi(argv[1]);
        tone(p, (unsigned int)(uint32_t)atol(argv[2]));
        delay(5);
        uint32_t edges = 0, samples = 0;
        int prev = digitalRead(p);
        const uint32_t t0 = micros();
        while ((uint32_t)(micros() - t0) < 100000u) {
            const int v = digitalRead(p);
            samples++;
            edges += (uint32_t)(v && !prev);
            prev = v;
        }
        noTone(p);
        pinMode(p, INPUT);
        CONSOLE.printf("TONE %lu %lu\r\n", (unsigned long)edges, (unsigned long)samples);
    } else if (!strcmp(argv[0], "servo") && argc == 3) {
        const pin_size_t p = (pin_size_t)atoi(argv[1]);
        Servo servo;
        servo.attach(p);
        servo.writeMicroseconds(atoi(argv[2]));
        delay(60);
        unsigned long w[3];
        for (int i = 0; i < 3; i++) {
            w[i] = pulseIn(p, HIGH, 50000UL);
        }
        servo.detach();
        pinMode(p, INPUT);
        CONSOLE.printf("SERVO %lu %lu %lu\r\n", w[0], w[1], w[2]);
    } else if (!strcmp(argv[0], "wwdg") && argc == 2) {
        const bool early = !strcmp(argv[1], "early");
        const uint32_t max = CH32RV.wwdgMaxTimeoutUs();
        const uint32_t t = max < 40000u ? max : 40000u, w = t / 2u;
        ewi_record.magic = EWI_MAGIC;
        ewi_record.count = 0;
        ewi_record.span_us = 0;
        ewi_record.steps = 0;
        CONSOLE.printf("WWDG %lu %lu %lu\r\n", (unsigned long)t, (unsigned long)w, (unsigned long)max);
        delay(50);                                /* the line out before the feeds begin */
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
        if (early) {
            delayMicroseconds(t / 8u);
            CH32RV.wwdgFeed();
        }
        for (;;) {
        }
    } else if (!strcmp(argv[0], "ewi")) {
        CONSOLE.printf("EWI %lu %lu %lu\r\n", (unsigned long)ewi_seen, (unsigned long)ewi_span, (unsigned long)ewi_steps);
#ifdef CH32RV_HAS_RTC
    } else if (!strcmp(argv[0], "bkpw") && argc == 2) {
        const uint16_t seed = (uint16_t)(uint32_t)atol(argv[1]);
        const uint8_t n = CH32RV.backupRegisterCount();
        for (uint8_t i = 0; i < n; i++) {
            CH32RV.setBackupRegister(i, (uint16_t)(seed + 257u * i));
        }
        CONSOLE.printf("BKPW %d %d\r\n", n, CH32RV.backupRegistersKeepPower() ? 1 : 0);
    } else if (!strcmp(argv[0], "bkpr") && argc == 2) {
        const uint16_t seed = (uint16_t)(uint32_t)atol(argv[1]);
        const uint8_t n = CH32RV.backupRegisterCount();
        int same = 0;
        for (uint8_t i = 0; i < n; i++) {
            same += CH32RV.getBackupRegister(i) == (uint16_t)(seed + 257u * i);
        }
        CONSOLE.printf("BKPR %d %d\r\n", n, same);
    } else if (!strcmp(argv[0], "alarm") && argc == 2) {
        const uint32_t s = (uint32_t)atol(argv[1]);
        alarm_fired = false;
        const uint32_t t0 = millis();
        if (!CH32RV.setAlarm(time(nullptr) + (time_t)s, on_alarm)) {
            CONSOLE.printf("ALARM 0 0 0\r\n");
            return;
        }
        while (!alarm_fired && millis() - t0 < (s + 3u) * 1000u) {
        }
        CONSOLE.printf("ALARM 1 %lu %ld\r\n", alarm_fired ? (unsigned long)(alarm_at - t0) : 0ul,
                           (long)CH32RV.getAlarm());
    } else if (!strcmp(argv[0], "asleep") && argc == 2) {
        const uint32_t s = (uint32_t)atol(argv[1]);
        CH32RV.disableWakeup();
        if (!CH32RV.setAlarm(time(nullptr) + (time_t)s) || !CH32RV.enableAlarmWakeup()) {
            CONSOLE.printf("ASLEEP REFUSED\r\n");
            return;
        }
        CH32RV.enableTimerWakeup(10u * 1000000u);
        const uint32_t t0 = millis();
        const CH32RVSleepResult r = CH32RV.lightSleep();
        const uint32_t took = millis() - t0;
        const CH32RVWakeupCause cause = CH32RV.wakeupCause();
        CH32RV.disableWakeup();
        CONSOLE.printf("ASLEEP %d %d %lu\r\n", (int)r, (int)cause, (unsigned long)took);
#else
    /* No RTC and no backup registers (V003, V00x, M030, X033 / X035): the
     * calls answer 0 / false there; left out to keep V003's 16 KB. */
    } else if (!strcmp(argv[0], "bkpw") || !strcmp(argv[0], "bkpr")) {
        CONSOLE.printf("%s 0 0\r\n", argv[0][3] == 'w' ? "BKPW" : "BKPR");
    } else if (!strcmp(argv[0], "alarm")) {
        CONSOLE.printf("ALARM 0 0 0\r\n");
    } else if (!strcmp(argv[0], "asleep")) {
        CONSOLE.printf("ASLEEP REFUSED\r\n");
#endif
    } else {
        CONSOLE.printf("UNKNOWN %d [%s]\r\n", argc, whole);
    }
}

void setup()
{
    boot_rstsckr = CH32RV_RCC_RSTSCKR;
    if (ewi_record.magic == EWI_MAGIC) {
        ewi_seen = ewi_record.count;
        ewi_span = ewi_record.span_us;
        ewi_steps = ewi_record.steps;
    }
    ewi_record.magic = 0;
    CONSOLE_BEGIN(115200);
    CONSOLE.printf("STARTUP %s %d\r\n", CH32RV.resetReasonName(), (int)CH32RV.wakeupCause());
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

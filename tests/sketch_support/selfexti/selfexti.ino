/* The EXTI lines that are not pins (CH32RV.attachExtiLine(), the PVD) with no wiring (tests/instrumented/selfexti),
 * beside selfsys and selfapi (V003's 16 KB has no room left in either). Console on SerialDMSeq:
 *
 *   ready <nonce>             READY <nonce> <build id>
 *   lines                     LINES <CH32RV_EXTI_LINE_MASK in hex, 0 without> <the RTC alarm's line or -1>
 *                             <the PVD's line or -1>
 *   swi <line>                attachExtiLine(line, RISING), triggerExtiLine(line) (EXTI_SWIEVR), 20 ms for the
 *                             callback; then detachExtiLine() and the same again:
 *                             SWI <line> <attach 1/0> <callbacks> <us from the trigger to the first> <callbacks
 *                             after the detach>
 *   rtcline <s>               (RTC series) attachExtiLine(CH32RV_EXTI_RTC_ALARM) beside setAlarm(time() + s) with
 *                             its own callback, s + 3 s for both; then detachExtiLine() and setAlarm(time() + 1)
 *                             again, 4 s: RTCLINE <line callbacks> <ms to the first> <alarm callbacks> <alarm
 *                             callbacks after the detach> <line callbacks after the detach>; RTCLINE NONE
 *                             without an RTC
 *   wake <s>                  (RTC series) the RTC alarm's line as a wake source (enableExtiLineWakeup()), not the
 *                             alarm itself (no enableAlarmWakeup()), setAlarm(time() + s), a 10 s timer as a
 *                             backstop, lightSleep(): WAKE <result> <wakeupCause()> <millis() across it> <line
 *                             callbacks>; WAKE NONE without an RTC
 *   pvd <mV>                  setLowVoltageDetect(mV) with a callback, 50 ms, clearLowVoltageDetect():
 *                             PVD <threshold mV, 0 without a PVD> <getVddMillivolts()> <callbacks>
 *                             <isLowVoltage() before the clear>
 *
 * STARTUP <resetReasonName()> comes first after every boot. */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static char line[64];
static uint8_t used;

static volatile uint32_t calls, first_us;

static void on_line()
{
    if (!calls) {
        first_us = micros();
    }
    calls++;
}

#ifdef CH32RV_EXTI_RTC_ALARM
static volatile uint32_t alarm_calls;

static void on_alarm()
{
    alarm_calls++;
}
#endif

static volatile uint32_t pvd_calls;

static void on_pvd()
{
    pvd_calls++;
}

static void swi(uint8_t n)
{
    calls = 0;
    const bool ok = CH32RV.attachExtiLine(n, on_line, RISING);
    const uint32_t t0 = micros();
    CH32RV.triggerExtiLine(n);
    while (!calls && micros() - t0 < 20000u) {
    }
    const uint32_t us = calls ? first_us - t0 : 0u;
    delay(5);                                   /* a second callback would come by now */
    const uint32_t got_all = calls;
    CH32RV.detachExtiLine(n);
    calls = 0;
    CH32RV.triggerExtiLine(n);
    delay(5);
    SerialDMSeq.printf("SWI %u %d %lu %lu %lu\r\n", (unsigned)n, ok ? 1 : 0, (unsigned long)got_all,
                       (unsigned long)us, (unsigned long)calls);
}

#ifdef CH32RV_EXTI_RTC_ALARM
static void rtc_line(uint32_t s)
{
    calls = 0;
    alarm_calls = 0;
    CH32RV.attachExtiLine(CH32RV_EXTI_RTC_ALARM, on_line, RISING);
    const uint32_t t0 = millis();
    const uint32_t t0_us = micros();
    CH32RV.setAlarm(time(nullptr) + (time_t)s, on_alarm);
    while ((!calls || !alarm_calls) && millis() - t0 < (s + 3u) * 1000u) {
    }
    const uint32_t line_calls = calls, ms = calls ? (first_us - t0_us) / 1000u : 0u;
    const uint32_t alarms = alarm_calls;
    /* setAlarm() keeps the line after the generic callback lets it go. */
    CH32RV.detachExtiLine(CH32RV_EXTI_RTC_ALARM);
    calls = 0;
    alarm_calls = 0;
    const uint32_t t1 = millis();
    CH32RV.setAlarm(time(nullptr) + 1, on_alarm);
    while (!alarm_calls && millis() - t1 < 4000u) {
    }
    delay(10);
    SerialDMSeq.printf("RTCLINE %lu %lu %lu %lu %lu\r\n", (unsigned long)line_calls, (unsigned long)ms,
                       (unsigned long)alarms, (unsigned long)alarm_calls, (unsigned long)calls);
    CH32RV.clearAlarm();
}

static void wake(uint32_t s)
{
    calls = 0;
    CH32RV.disableWakeup();
    CH32RV.attachExtiLine(CH32RV_EXTI_RTC_ALARM, on_line, RISING);
    const bool ok = CH32RV.enableExtiLineWakeup(CH32RV_EXTI_RTC_ALARM) &&
                    CH32RV.setAlarm(time(nullptr) + (time_t)s);
    CH32RV.enableTimerWakeup(10u * 1000000u);
    const uint32_t t0 = millis();
    const CH32RVSleepResult r = ok ? CH32RV.lightSleep() : CH32RV_SLEEP_NO_WAKEUP_SOURCE;
    const uint32_t took = millis() - t0;
    const CH32RVWakeupCause cause = CH32RV.wakeupCause();
    CH32RV.disableWakeup();
    CH32RV.detachExtiLine(CH32RV_EXTI_RTC_ALARM);
    CH32RV.clearAlarm();
    SerialDMSeq.printf("WAKE %d %d %lu %lu\r\n", (int)r, (int)cause, (unsigned long)took, (unsigned long)calls);
}
#endif

static void pvd(uint32_t mv)
{
    pvd_calls = 0;
    const uint32_t vdd = CH32RV.getVddMillivolts();
    const uint32_t at = CH32RV.setLowVoltageDetect(mv, on_pvd);
    delay(50);
    const bool low = CH32RV.isLowVoltage();
    CH32RV.clearLowVoltageDetect();
    SerialDMSeq.printf("PVD %lu %lu %lu %d\r\n", (unsigned long)at, (unsigned long)vdd, (unsigned long)pvd_calls,
                       low ? 1 : 0);
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
    } else if (!strcmp(argv[0], "lines")) {
#ifdef CH32RV_EXTI_LINE_MASK
        const unsigned long mask = CH32RV_EXTI_LINE_MASK;
#else
        const unsigned long mask = 0;
#endif
#ifdef CH32RV_EXTI_RTC_ALARM
        const int rtc = CH32RV_EXTI_RTC_ALARM;
#else
        const int rtc = -1;
#endif
#ifdef CH32RV_EXTI_PVD
        const int pvd_line = CH32RV_EXTI_PVD;
#else
        const int pvd_line = -1;
#endif
        SerialDMSeq.printf("LINES %lx %d %d\r\n", mask, rtc, pvd_line);
    } else if (!strcmp(argv[0], "swi") && argc == 2) {
        swi((uint8_t)atoi(argv[1]));
    } else if (!strcmp(argv[0], "rtcline") && argc == 2) {
#ifdef CH32RV_EXTI_RTC_ALARM
        rtc_line((uint32_t)atol(argv[1]));
#else
        SerialDMSeq.printf("RTCLINE NONE\r\n");
#endif
    } else if (!strcmp(argv[0], "wake") && argc == 2) {
#ifdef CH32RV_EXTI_RTC_ALARM
        wake((uint32_t)atol(argv[1]));
#else
        SerialDMSeq.printf("WAKE NONE\r\n");
#endif
    } else if (!strcmp(argv[0], "pvd") && argc == 2) {
        pvd((uint32_t)atol(argv[1]));
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

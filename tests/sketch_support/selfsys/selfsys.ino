/* The chip-level calls with no wiring (tests/instrumented/selfsys), beside
 * selfcheck and selfbus. Console on SerialDMSeq:
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
 *   rtc                       RTC <gettimeofday() seconds> <microseconds> on the RTC families, else RTC NONE
 *   pad <pin>                 PAD <pin> <1 when the board holds it: the pulls do not move it>
 *   tone <pin> <hz>           tone(pin, hz), the pad read for 100 ms: TONE <rising edges> <samples>
 *   servo <pin> <us>          a Servo on pin at us, pulseIn() three times: SERVO <width> <width> <width>
 *
 * STARTUP <resetReasonName()> <wakeupCause()> comes first after every boot. */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <Servo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_rtc.h"

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static char line[64];
static uint8_t used;

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
        SerialDMSeq.printf("ADC %d NONE\r\n", pin);
        return;
    }
    if (held(p)) {
        SerialDMSeq.printf("ADC %d HELD\r\n", pin);
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
    SerialDMSeq.printf("ADC %d %lu %lu %lu %d\r\n", pin, (unsigned long)channel, (unsigned long)level[1],
                       (unsigned long)level[0], (1 << CH32RV_ADC_BITS) - 1);
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
        SerialDMSeq.printf("READY %s %s\r\n", argv[1], BENCH_BUILD_ID);
    } else if (!strcmp(argv[0], "id")) {
#ifdef CH32RV_HAS_UNIQUE_ID
        const uint64_t uid = CH32RV.getUniqueId();
        SerialDMSeq.printf("ID %08lx%08lx %lu %s\r\n", (unsigned long)(uint32_t)uid, (unsigned long)(uint32_t)(uid >> 32),
                           (unsigned long)CH32RV.getFlashChipSize(), CH32RV.getChipModel());
#else
        SerialDMSeq.printf("ID - 0 %s\r\n", CH32RV.getChipModel());
#endif
    } else if (!strcmp(argv[0], "temp")) {
#if CH32RV_ADC_CLOCK_OK
        const float t = analogReadTemp();
#else
        const float t = NAN;                     /* no ADC at this CPU Clock: a build error to call */
#endif
#ifdef CH32RV_ADC_TEMP_CAL_ADDR
        SerialDMSeq.printf("TEMP %d %08lx\r\n", isnan(t) ? -9999 : (int)lroundf(t * 10.0f),
                           (unsigned long)*(const volatile uint32_t *)CH32RV_ADC_TEMP_CAL_ADDR);
#else
        SerialDMSeq.printf("TEMP %d -\r\n", isnan(t) ? -9999 : (int)lroundf(t * 10.0f));
#endif
    } else if (!strcmp(argv[0], "restart")) {
        SerialDMSeq.printf("RESTARTING\r\n");
        delay(50);
        CH32RV.restart();
    } else if (!strcmp(argv[0], "wdt") && argc == 2) {
        if (!CH32RV.wdtEnable(strtoul(argv[1], nullptr, 10))) {
            SerialDMSeq.printf("WDT REFUSED\r\n");
            return;
        }
        for (int i = 0; i < 6; i++) {
            delay(50);
            CH32RV.wdtFeed();
        }
        SerialDMSeq.printf("WDT FED\r\n");
        SerialDMSeq.printf("WDT LET GO\r\n");
        for (;;) {
        }
    } else if (!strcmp(argv[0], "sleep") && argc == 2) {
        CH32RV.enableTimerWakeup((uint64_t)strtoul(argv[1], nullptr, 10) * 1000u);
        const uint32_t t0 = millis();
        const CH32RVSleepResult r = CH32RV.lightSleep();
        const uint32_t took = millis() - t0;
        const CH32RVWakeupCause cause = CH32RV.wakeupCause();
        CH32RV.disableWakeup();
        SerialDMSeq.printf("SLEEP %d %d %lu\r\n", (int)r, (int)cause, (unsigned long)took);
    } else if (!strcmp(argv[0], "adcpins")) {
        SerialDMSeq.printf("ADCPINS");
#if SELFSYS_ADC
        for (uint8_t port = 0; port < CH32RV_PORT_COUNT; port++) {
            const uint32_t mask = CH32RV_PORT_MASK(port);
            for (uint8_t bit = 0; bit < 32; bit++) {
                const pin_size_t p = (pin_size_t)CH32RV_PIN(port, bit);
                if ((mask & (1ul << bit)) && CH32RV_PIN_TO_ADC_CHANNEL(p) != NOT_AN_ANALOG_PIN) {
                    SerialDMSeq.printf(" %d", (int)p);
                }
            }
        }
#endif
        SerialDMSeq.printf("\r\n");
#if SELFSYS_ADC
    } else if (!strcmp(argv[0], "adc") && argc == 2) {
        adc_pad(atoi(argv[1]));
#endif
    } else if (!strcmp(argv[0], "rtc")) {
#ifdef CH32RV_HAS_RTC
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        SerialDMSeq.printf("RTC %lu %lu\r\n", (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);
#else
        SerialDMSeq.printf("RTC NONE\r\n");
#endif
    } else if (!strcmp(argv[0], "pad") && argc == 2) {
        SerialDMSeq.printf("PAD %d %d\r\n", atoi(argv[1]), held((pin_size_t)atoi(argv[1])) ? 1 : 0);
    } else if (!strcmp(argv[0], "tone") && argc == 3) {
        const pin_size_t p = (pin_size_t)atoi(argv[1]);
        tone(p, (unsigned int)strtoul(argv[2], nullptr, 10));
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
        SerialDMSeq.printf("TONE %lu %lu\r\n", (unsigned long)edges, (unsigned long)samples);
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
        SerialDMSeq.printf("SERVO %lu %lu %lu\r\n", w[0], w[1], w[2]);
    } else {
        SerialDMSeq.printf("UNKNOWN %d [%s]\r\n", argc, whole);
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.printf("STARTUP %s %d\r\n", CH32RV.resetReasonName(), (int)CH32RV.wakeupCause());
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

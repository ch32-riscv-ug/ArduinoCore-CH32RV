/* CH32RVLogicCapture contracts (tests/instrumented/logic): the sketch drives a
 * PWM on a pin of its own - from a timer other than the capture's - and
 * captures that pin's port, so no wiring is needed. Console on SerialDMSeq:
 *
 *   ready <nonce>             READY <nonce> <build id> <capture timer> <buffer samples>
 *   run <rate> <pwm hz>       LOGIC <pin> <port> <bit> <pwm timer> <rate got> <periods> <min> <max> <overruns> <halves> <bits that changed>
 *                             or LOGIC NONE (no PWM pin on another timer) / LOGIC FAIL <step>
 *   pin <n>                   PIN <n> 0/1: use this pin (it must have PWM on another timer)
 *   level                     LEVEL <highs of 10000 digitalRead()s of the pin, the PWM running>
 *   burst <pwm hz>            BURST <rate> <periods> <min> <max>   (the same pin, captureBurst, the PWM at <pwm hz>)
 *
 * A period is counted from one rising edge of the pin's bit to the next. */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVLogicCapture.h>
#include <CH32RVTimer.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

/* 2 KB of RAM on V003 (4 KB on V006's ANY): a smaller buffer there. */
#if defined(CH32RV_SERIES_CH32V002) || defined(CH32RV_SERIES_CH32V003) || defined(CH32RV_SERIES_CH32V004) || \
    defined(CH32RV_SERIES_CH32V005) || defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || \
    defined(CH32RV_SERIES_CH32M007)
#define SAMPLES 512u
#else
#define SAMPLES 2048u
#endif

static CH32RVLogicCapture logic;
static uint16_t buf[SAMPLES];
static bool havePin;
static pin_size_t pwmPin;
static uint8_t pwmBit, pwmPort;
static char line[64];
static uint8_t used;

/* The first pad on every part of the series with a PWM channel on another
 * timer than the capture's - and not a DAC output, where analogWrite() drives
 * the converter instead (CH32H417's PA5). */
static bool findPin()
{
    for (pin_size_t pin = 0; pin < NUM_DIGITAL_PINS; pin++) {
        if (!digitalPinIsCommon(pin)) {
            continue;
        }
#ifdef CH32RV_DAC1_PIN
        if (CH32RV_PIN_RESOLVE(pin) == CH32RV_DAC1_PIN) {
            continue;
        }
#endif
#ifdef CH32RV_DAC2_PIN
        if (CH32RV_PIN_RESOLVE(pin) == CH32RV_DAC2_PIN) {
            continue;
        }
#endif
        const uint8_t t = (uint8_t)CH32RV_PWM_PIN_TO_TIMER(pin);
        if (t && t != CH32RVLogicCapture::timer() && CH32RV_PWM_PIN_TO_CHANNEL(pin)) {
            pwmPin = pin;
            havePin = true;
            pwmPort = (uint8_t)digitalPinToPort(pin);   /* 0 = PA */
            pwmBit = (uint8_t)CH32RV_PIN_BIT(CH32RV_PIN_RESOLVE(pin));
            return true;
        }
    }
    return false;
}

/* One pass over a half: the periods, and the bits that differ from its first
 * sample. A second loop for the bits left a 48 MHz X035 behind the 1 MS/s
 * capture (one or two halves overrun of eight). */
static void periods(const uint16_t *v, size_t n, uint32_t &count, uint32_t &lo, uint32_t &hi, uint32_t &changed)
{
    const uint16_t m = (uint16_t)(1u << pwmBit);
    count = 0;
    lo = 0xFFFFFFFFu;
    hi = 0;
    long prev = -1;
    uint16_t diff = 0;
    for (size_t i = 1; i < n; i++) {
        diff |= (uint16_t)(v[i] ^ v[0]);
        if ((v[i] & m) && !(v[i - 1] & m)) {
            if (prev >= 0) {
                const uint32_t d = (uint32_t)(i - (size_t)prev);
                count++;
                if (d < lo) lo = d;
                if (d > hi) hi = d;
            }
            prev = (long)i;
        }
    }
    if (!count) {
        lo = 0;
    }
    changed |= diff;
}

/* Numbers after a word, one space each - no formatted printing, whose code
 * does not fit the V003's 16 KB beside the rest. */
static void say(const char *head, const uint32_t *v, uint8_t n)
{
    SerialDMSeq.print(head);
    for (uint8_t i = 0; i < n; i++) {
        SerialDMSeq.print(' ');
        SerialDMSeq.print((unsigned long)v[i]);
    }
    SerialDMSeq.println();
}

static void reply(char *command)
{
    if (strncmp(command, "ready ", 6) == 0) {
        SerialDMSeq.print("READY ");
        SerialDMSeq.print(command + 6);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(BENCH_BUILD_ID);
        const uint32_t v[2] = {CH32RVLogicCapture::timer(), SAMPLES};
        say("", v, 2);
    } else if (strncmp(command, "run ", 4) == 0) {
        char *end;
        const unsigned long rate = strtoul(command + 4, &end, 10), hz = strtoul(end, &end, 10);
        if (!rate || !hz) {
            SerialDMSeq.println("ERROR ARGS");
            return;
        }
        if (!havePin && !findPin()) {
            SerialDMSeq.println("LOGIC NONE");
            return;
        }
        if (!logic.begin((CH32RVLogicPort)pwmPort, rate)) {
            SerialDMSeq.println("LOGIC FAIL begin");
            return;
        }
        analogWriteFrequency(pwmPin, hz);
        analogWrite(pwmPin, 128);
        if (!logic.startContinuous(buf, SAMPLES)) {
            SerialDMSeq.println("LOGIC FAIL start");
            return;
        }
        uint32_t count = 0, lo = 0xFFFFFFFFu, hi = 0, halves = 0, changed = 0;
        const uint32_t t0 = millis();
        while (halves < 8 && millis() - t0 < 3000) {
            if (const uint16_t *h = logic.readBuffer()) {
                uint32_t c, l, x;
                periods(h, SAMPLES / 2, c, l, x, changed);
                count += c;
                if (c && l < lo) lo = l;
                if (x > hi) hi = x;
                halves++;
            }
        }
        logic.stop();
        const uint32_t v[11] = {pwmPin, pwmPort, pwmBit, (uint32_t)CH32RV_PWM_PIN_TO_TIMER(pwmPin), logic.sampleRate(),
                                count, count ? lo : 0, hi, logic.overruns(), halves, changed};
        say("LOGIC", v, 11);
    } else if (strncmp(command, "pin ", 4) == 0) {
        const pin_size_t pin = (pin_size_t)strtoul(command + 4, nullptr, 10);
        const uint8_t t = (uint8_t)CH32RV_PWM_PIN_TO_TIMER(pin);
        const bool ok = digitalPinIsValid(pin) && t && t != CH32RVLogicCapture::timer() && CH32RV_PWM_PIN_TO_CHANNEL(pin);
        if (ok) {
            pwmPin = pin;
            pwmPort = (uint8_t)digitalPinToPort(pin);
            pwmBit = (uint8_t)CH32RV_PIN_BIT(CH32RV_PIN_RESOLVE(pin));
            havePin = true;
        }
        const uint32_t v[2] = {pin, ok};
        say("PIN", v, 2);
    } else if (strcmp(command, "level") == 0) {
        uint32_t high = 0;
        if (havePin) {
            for (uint32_t i = 0; i < 10000u; i++) {
                high += digitalRead(pwmPin) == HIGH;
            }
        }
        /* and whether the PWM's timer counts at all: its CNT, CCR of the channel, ARR, CTLR1 */
        uint32_t v[11] = {high, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (havePin) {
            const CH32RVTimerCapability *cap = ch32TimerCapabilityFor((uint8_t)CH32RV_PWM_PIN_TO_TIMER(pwmPin));
            if (cap) {
                const uint32_t b = cap->register_base;
                v[1] = *(volatile uint32_t *)(b + 0x24u);
                delayMicroseconds(37);
                v[2] = *(volatile uint32_t *)(b + 0x24u);
                v[3] = *(volatile uint32_t *)(b + 0x2Cu);
                v[4] = *(volatile uint32_t *)(b + 0x00u);
                v[5] = *(volatile uint32_t *)(b + 0x30u + 4u * (uint32_t)CH32RV_PWM_PIN_TO_CHANNEL(pwmPin));
                v[6] = *(volatile uint32_t *)(b + 0x20u);   /* CCER */
            }
            const uint32_t g = CH32RV_GPIO_PORT_BASE(pwmPort);
            v[7] = *(volatile uint32_t *)(CH32RV_CLKEN_GPIO_ADDR);
            v[8] = *(volatile uint32_t *)(g + (pwmBit < 8 ? 0x00u : 0x04u));   /* CFGLR / CFGHR */
            v[9] = *(volatile uint32_t *)(g + 0x08u);
            v[10] = *(volatile uint32_t *)(g + 0x0Cu);
        }
        say("LEVEL", v, 11);
    } else if (strncmp(command, "burst ", 6) == 0) {
        if (!havePin) {
            SerialDMSeq.println("BURST NONE");
            return;
        }
        analogWriteFrequency(pwmPin, strtoul(command + 6, nullptr, 10));
        analogWrite(pwmPin, 128);
        const uint32_t rate = logic.captureBurst(buf, (uint16_t)(SAMPLES));
        uint32_t c, l, x, changed = 0;
        periods(buf, SAMPLES, c, l, x, changed);
        const uint32_t v[4] = {rate, c, l, x};
        say("BURST", v, 4);
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
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

/* Single-line peripherals on labelled lines (tests/instrumented/periph): a
 * second UART and an ADC input, on the pads the bench labels (bench_lines.py:
 * uart2, adc; analogWrite() is tests/instrumented/signals'). Pins are the
 * sketch's pin numbers.
 *
 *   ready <nonce>             READY <nonce> <build id>
 *   uart2 <rx> <tx> <baud>    UART2 <ok> <USART>: Serial1 (Serial on a one-USART part) on those pads (the
 *                             USART it took, 0 if none), then every byte it
 *                             receives goes back out (until `stop`)
 *   stop                      STOP <bytes echoed>
 *   adc <pin>                 ADC <reading> <full scale> <VDD mV from Vrefint, 0 unknown>
 *   acaps <pin>               ACAPS <ADC channel of the pad, or -1>
 *   ares <pin> <bits> <ref>   analogReference(ref) and analogReadResolution(bits), one analogRead(), the
 *                             width put back: ARES <reading> <the ADC's own bits>
 *   inj <pin>                CH32RVAdcInjected on the pin: INJ <begun> <convert()'s value> <startTimer(1000) ok> <rate>
 *                            <rounds in the 500 ms after> <their us> <the last round's value> <the ADC's bits>
 *   awd <pin> <band>         CH32RVAdcStream on the pin at 50 kHz with its watchdog's window the lowest
 *                             quarter of full scale (band 0) or the highest (band 1), for 50 ms:
 *                             AWD <ok> <tripped 0/1> <tripped again right after a clear 0/1>
 *   astream <rate> <halves> <pin0> [<pin1> [<pin2>]]
 *                             CH32RVAdcStream on the pins at <rate> (the first
 *                             half dropped, <halves> halves of ASTREAM_HALF
 *                             rounds read), pin0 a square wave read with
 *                             thresholds at 1/4 and 3/4 of full scale:
 *                             ASTREAM <rate got> <step> <overruns> <rounds>
 *                             <high> <low> <rises> <first rise> <last rise>
 *                             (high / low: the rounds between the first and
 *                             the last rise, whole periods, when there are two)
 *                             <pin1 min> <pin1 max> <full scale> <pin1 rounds over
 *                             3/4> <pin1 rounds under 1/4> (rounds counted from the
 *                             first read; ASTREAM 0 when begin() refused)
 *   has                       HAS <atimer> <aext> <acont> <inj>: 1 for each of those commands
 *                             this build holds, 0 for one it leaves out (V003's 16 KB: atimer,
 *                             acont and inj; a part without ADC: all four)
 *   atimer <rate> <halves> <pin>
 *                             CH32RVAdcStream.beginTimer() on the pin at <rate>, read as
 *                             astream does (one pin): ATIMER <ok> <rate got> <timer>
 *                             <step> <overruns> <rounds> <high> <low> <rises> <first rise>
 *                             <last rise> <conversions> <us> (the last two: conversions()
 *                             counted over micros() from the first half's end to the last's;
 *                             ATIMER 0 when beginTimer() refused)
 *   tcaps <pin>               TCAPS <1 when the pad can start the ADC by its edge, else 0>
 *                             <1 when it can on CHANGE too, else 0>
 *   aext <trigger> <edge> <pin> [<max rate>]
 *                             CH32RVAdcStream.beginExternal() (edge: 2 CHANGE, 3 FALLING,
 *                             4 RISING, as PinStatus) and startContinuous(): AEXT <ok>
 *   aextread                  AEXTREAD <conversions> <levels>: the first 64 rounds' values
 *                             as H (over 3/4 of full scale), L (under 1/4) or ? (between),
 *                             "-" for none
 *   aextrate <ms>             AEXTRATE <conversions> <us>: conversions() counted over a
 *                             window of about <ms> on micros()
 *   aextstop                  AEXTSTOP (end())
 *   acont <hz> <per pin> <frames> <pin>
 *                             analogContinuous() on the pin, started, <frames> frames waited
 *                             for: ACONT <ok> <frames seen> <us from the first frame's
 *                             callback to the last's> <average raw> <average mV> <full scale>
 *                             (ACONT 0 when analogContinuous() refused)
 *   hscaps <pin>              HSCAPS <HSADC channel of the pad, or -1 (no HSADC)>
 *   hstream <rate> <bufs> <pin>
 *                             CH32RVHsAdc on the pin at <rate> (the first buffer dropped,
 *                             <bufs> buffers of HSTREAM_SAMPLES read), a square wave read
 *                             as astream does (10 bits): HSTREAM <rate got> <overruns>
 *                             <samples> <high> <low> <rises> <first rise> <last rise>
 *                             (HSTREAM 0 when begin() refused)
 *
 * The quad-SPI controller (CH32RVQspi: V205, H41x; QSPIFIND NONE / QSPI 2 elsewhere):
 *
 *   qspifind <pin>...         SCK, CS, IO0, IO1 of one QSPI flash among the pins (the variant's
 *                             CH32RV_QSPIn_PADS): QSPIFIND <n> <sck> <cs> <io0> <io1> (QSPIFIND NONE)
 *   qspi <n> <sck> <cs> <io0> <io1> <hz> <kind> <hex>
 *                             CH32RVQspi n on those pins, mode 0: kind c = instruction 20 and the address
 *                             (24 bits, the hex) on one line; w = 02, the address 000100 and the bytes on
 *                             one line; d = A2, the address 000100 and the bytes on two lines; r = 0B, the
 *                             address 000100, 8 dummy clocks, as many bytes read as the hex has on one
 *                             line; e = BB, the address 000100 on two lines, 4 dummy clocks, read on two:
 *                             QSPI <1 / 0> <bytes read, hex or -> <clock>
 *
 * The LCD controller (CH32RVLtdc: H41x; LTDCFIND NONE / LTDC 2 elsewhere):
 *
 *   ltdcfind <pin>...         CLK, HSYNC, VSYNC, DE, an R and a G line among the pins (the variant's
 *                             CH32RV_LTDC_PADS, six pads): LTDCFIND <clk> <hs> <vs> <de> <r> <rbit> <g> <gbit>
 *   ltdc <clk> <hs> <vs> <de> <r> <rbit> <g> <gbit> <rgb hex> [<mode>]
 *                             a 64 x 32 panel (HSYNC 4, back 8, front 8 pixels; VSYNC 2, back 4, front 4
 *                             lines) at the slowest pixel clock: mode 0 the background <rgb> alone, 1 a
 *                             layer of <rgb> over the whole area (the background black), 2 a black layer
 *                             of alpha 0 over the background <rgb>:
 *                             LTDC <1 / 0> <pixelClock()>
 *   ltdcoff                   end(): LTDCOFF
 *
 * The low-power timer (CH32RVLowPowerTimer: L103 / M103, H41x; LPT NONE elsewhere):
 *
 *   lptfind <pin>...          the first of the pins that is a timer's CH1 pad:
 *                             LPTFIND <timer> <pin> (LPTFIND NONE)
 *   lptcount <timer> <pin> <async> <ms>
 *                             beginCounter(pin, RISING, async) for <ms>: LPTCOUNT <ok> <count> <us
 *                                                         from begin's return to the read>
 *   lpttimer <timer> <clock> <divider> <period> <ms>
 *                             begin(clock 0 bus, 3 LSI), setPeriod(), onPeriod() counted for
 *                             <ms>: LPTTIMER <ok> <clockHz()> <periods> <us from start to the count>
 *   lptwake <timer> <ms>      a period of <ms> on LSI, its wake line enabled (and the timer
 *                             wake-up at 3 x <ms> as a backstop), lightSleep():
 *                             LPTWAKE <ok> <wakeupCause()> <ms slept by millis()> <periods> */
#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
#include <string.h>
#if defined(NUM_ANALOG_INPUTS)
#include <CH32RVAdcStream.h>
#include <CH32RVAdcInjected.h>
#endif
/* The ADC's triggers: CH32RVAdcStream's timer (atimer) and pad (aext), and
 * analogContinuous() (acont). V003's 16 KB holds the pad's alone beside the
 * rest: with all three the build passed it by about 3 KB, with the pad's and
 * the timer's by about 1 KB. */
#if defined(NUM_ANALOG_INPUTS)
#define PERIPH_AEXT 1
#if !defined(CH32RV_VARIANT_CH32V003)
#define PERIPH_ATIMER 1
#define PERIPH_ACONT 1
#endif
#endif
/* CH32RVAdcInjected (inj): about 3.2 KB on V003, which passed its 16 KB by
 * 2.6 KB with it (2026-11-21); V203 and X035 run it. */
#if defined(NUM_ANALOG_INPUTS) && !defined(CH32RV_VARIANT_CH32V003)
#define PERIPH_INJ 1
#endif
#include <CH32RVLowPowerTimer.h>
#include <CH32RVQspi.h>
#include <CH32RVLtdc.h>
static CH32RVLtdc ltdc;
/* a row of the variant's CH32RV_QSPIn_PADS (before the prototypes the IDE adds) */
struct QspiPad {
    uint8_t role, pad;
    uint32_t value, value2;
};
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
#define PERIPH_HSADC 1
#include <CH32RVHsAdc.h>
#endif
/* The USART uart2 moves around: Serial1, or Serial where the part has one
 * USART (V003's Serial is USART1; this sketch's console stays SerialDMSeq:
 * run_hardware.py keeps periph off the probe's UART lines). */
#if defined(SERIAL_PORT_HARDWARE1)
#define ROUTED Serial1
#elif defined(SERIAL_PORT_HARDWARE)
#define ROUTED Serial
#endif

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

#if defined(CH32RV_VARIANT_CH32V003)
static char line[64];
#else
static char line[192];   /* lptfind / qspifind name every labelled line */
#endif
static uint8_t used;
static bool echoing;
static uint32_t echoed;

#if defined(NUM_ANALOG_INPUTS)
#if defined(CH32RV_VARIANT_CH32V003)
#define ASTREAM_HALF 96    /* 2 KB of RAM: 2 x 256 values left the stack 16 bytes and the sketch never started */
#else
#define ASTREAM_HALF 256
#endif
static CH32RVAdcStream stream;
static uint16_t stream_buf[2 * ASTREAM_HALF];

/* What astream / atimer read from a running stream: pin 0 a square wave,
 * pin 1 (when there is one) a level. */
struct Scan {
    uint32_t round, high, low, rises, first, last, high1n, low1n;
    uint16_t min1, max1;
    uint32_t c0, t0, c1, t1;      /* conversions() and micros() at the first used half and the last */
};

static void scan(uint32_t halves, uint8_t count, uint32_t timeout_ms, Scan &r)
{
    const uint16_t full = (uint16_t)((1u << CH32RV_ADC_BITS) - 1u);
    const uint16_t lo = full / 4, hi = (uint16_t)(full - full / 4);
    const uint16_t per = (uint16_t)(ASTREAM_HALF / count * count);
    memset(&r, 0, sizeof r);
    r.min1 = 0xFFFF;
    uint32_t high0 = 0, low0 = 0, high1 = 0, low1 = 0;   /* the counts at the first and the last rise */
    int8_t state = -1;            /* 0 low, 1 high, -1 not yet known */
    bool skipped = false;
    const uint32_t start = millis();
    while (halves && millis() - start < timeout_ms) {
        const uint16_t *half = stream.readBuffer();
        if (!half) {
            continue;
        }
        const uint32_t now = micros(), done = stream.conversions();
        if (!skipped) {
            skipped = true;       /* the first half starts mid-setup */
            r.c0 = done;
            r.t0 = now;
            continue;
        }
        r.c1 = done;
        r.t1 = now;
        for (uint16_t i = 0; i < per; i += count, r.round++) {
            const uint16_t v = half[i];
            if (v >= hi) {
                r.high++;
                if (state == 0) {
                    if (!r.rises++) {
                        r.first = r.round;
                        high0 = r.high;
                        low0 = r.low;
                    }
                    r.last = r.round;
                    high1 = r.high;
                    low1 = r.low;
                }
                state = 1;
            } else if (v <= lo) {
                r.low++;
                state = 0;
            }
            if (count > 1) {
                r.high1n += half[i + 1] >= hi;
                r.low1n += half[i + 1] <= lo;
                r.min1 = half[i + 1] < r.min1 ? half[i + 1] : r.min1;
                r.max1 = half[i + 1] > r.max1 ? half[i + 1] : r.max1;
            }
        }
        halves--;
    }
    if (r.rises > 1) {            /* whole periods only: a part of one at either end tilts the duty */
        r.high = high1 - high0;
        r.low = low1 - low0;
    }
}

static void astream(uint32_t rate, uint32_t halves, const pin_size_t *pins, uint8_t count)
{
    if (!stream.begin(pins, count, rate)) {
        CONSOLE.printf("ASTREAM 0\r\n");
        return;
    }
    const uint16_t full = (uint16_t)((1u << CH32RV_ADC_BITS) - 1u);
    stream.startContinuous(stream_buf, (uint16_t)(ASTREAM_HALF / count * count));
    Scan r;
    scan(halves, count, 2000, r);
    stream.end();
    CONSOLE.printf("ASTREAM %lu %u %lu %lu %lu %lu %lu %lu %lu %u %u %u %lu %lu\r\n", (unsigned long)stream.sampleRate(),
                       stream.sampleTimeStep(), (unsigned long)stream.overruns(), (unsigned long)r.round,
                       (unsigned long)r.high, (unsigned long)r.low, (unsigned long)r.rises, (unsigned long)r.first,
                       (unsigned long)r.last, count > 1 ? r.min1 : 0, count > 1 ? r.max1 : 0, full,
                       (unsigned long)r.high1n, (unsigned long)r.low1n);
}

#if PERIPH_ATIMER
static void atimer(uint32_t rate, uint32_t halves, pin_size_t pin)
{
    if (!stream.beginTimer(pin, rate)) {
        CONSOLE.printf("ATIMER 0\r\n");
        return;
    }
    stream.startContinuous(stream_buf, ASTREAM_HALF);
    Scan r;
    /* The halves' time at the rate, and a second more. */
    scan(halves, 1, (uint32_t)((uint64_t)(halves + 1u) * ASTREAM_HALF * 1000u / rate) + 1000u, r);
    const uint32_t got = stream.sampleRate(), overruns = stream.overruns();
    const uint8_t timer = stream.triggerTimer(), step = stream.sampleTimeStep();
    stream.end();
    CONSOLE.printf("ATIMER 1 %lu %u %u %lu %lu %lu %lu %lu %lu %lu %lu %lu\r\n", (unsigned long)got, timer, step,
                       (unsigned long)overruns, (unsigned long)r.round, (unsigned long)r.high, (unsigned long)r.low,
                       (unsigned long)r.rises, (unsigned long)r.first, (unsigned long)r.last,
                       (unsigned long)(r.c1 - r.c0), (unsigned long)(r.t1 - r.t0));
}

#endif

#if PERIPH_AEXT
static bool ext_running;

static void aext(pin_size_t trigger, int edge, pin_size_t pin, uint32_t max_rate)
{
    stream.end();
    /* Pulled down: the probe's channel does not drive the line between its
     * GPIO and its PWM, and a floating PB11 picked up hundreds of pulses
     * there (V103, 2026-11-17). */
    pinMode(trigger, INPUT_PULLDOWN);
    ext_running = stream.beginExternal(pin, trigger, (PinStatus)edge, max_rate) &&
                  stream.startContinuous(stream_buf, ASTREAM_HALF);
    CONSOLE.printf("AEXT %d\r\n", ext_running ? 1 : 0);
}

static void aextread()
{
    const uint16_t full = (uint16_t)((1u << CH32RV_ADC_BITS) - 1u);
    const uint32_t n = ext_running ? stream.conversions() : 0;
    char levels[65];
    uint32_t k = 0;
    for (; k < n && k < 64u && k < 2u * ASTREAM_HALF; k++) {
        const uint16_t v = stream_buf[k];
        levels[k] = v >= full - full / 4 ? 'H' : v <= full / 4 ? 'L' : '?';
    }
    levels[k] = 0;
    CONSOLE.printf("AEXTREAD %lu %s\r\n", (unsigned long)n, k ? levels : "-");
}

static void aextrate(uint32_t ms)
{
    const uint32_t c0 = stream.conversions(), t0 = micros();
    while (micros() - t0 < ms * 1000u) {
    }
    const uint32_t c1 = stream.conversions(), t1 = micros();
    CONSOLE.printf("AEXTRATE %lu %lu\r\n", (unsigned long)(c1 - c0), (unsigned long)(t1 - t0));
}
#endif

#if PERIPH_ACONT
static volatile uint32_t cont_frames, cont_first_us, cont_last_us;

static void on_cont_frame()
{
    const uint32_t now = micros();
    if (!cont_frames++) {
        cont_first_us = now;
    }
    cont_last_us = now;
}

static void acont(uint32_t hz, uint32_t per_pin, uint32_t frames, pin_size_t pin)
{
    const uint8_t pins[1] = {(uint8_t)pin};
    if (!analogContinuous(pins, 1, per_pin, hz, on_cont_frame)) {
        CONSOLE.printf("ACONT 0\r\n");
        return;
    }
    cont_frames = 0;
    analogContinuousStart();
    const uint32_t start = millis();
    const uint32_t wait = (uint32_t)((uint64_t)(frames + 1u) * per_pin * 1000u / hz) + 1000u;
    while (cont_frames < frames && millis() - start < wait) {
    }
    adc_continuous_result_t *result = nullptr;
    const bool read = analogContinuousRead(&result, 100);
    analogContinuousStop();
    const uint32_t seen = cont_frames, span = cont_last_us - cont_first_us;
    CONSOLE.printf("ACONT 1 %lu %lu %d %d %u\r\n", (unsigned long)seen, (unsigned long)span,
                       read ? result[0].avg_read_raw : -1, read ? result[0].avg_read_mvolts : -1,
                       (1u << CH32RV_ADC_BITS) - 1u);
    analogContinuousDeinit();
}
#endif
#endif

#if PERIPH_HSADC
#define HSTREAM_SAMPLES 1024
static CH32RVHsAdc hsadc;
static uint16_t hs_buf[2][HSTREAM_SAMPLES];

static void hstream(uint32_t rate, uint32_t bufs, pin_size_t pin)
{
    if (!hsadc.begin(pin, rate)) {
        CONSOLE.printf("HSTREAM 0\r\n");
        return;
    }
    const uint16_t full = 1023, lo = full / 4, hi = (uint16_t)(full - full / 4);
    hsadc.startContinuous(hs_buf[0], hs_buf[1], HSTREAM_SAMPLES);
    uint32_t n = 0, high = 0, low = 0, rises = 0, first = 0, last = 0;
    int8_t state = -1;
    bool skipped = false;
    const uint32_t start = millis();
    while (bufs && millis() - start < 2000) {
        const uint16_t *buf = hsadc.readBuffer();
        if (!buf) {
            continue;
        }
        if (!skipped) {
            skipped = true;
            continue;
        }
        for (uint16_t i = 0; i < HSTREAM_SAMPLES; i++, n++) {
            const uint16_t v = buf[i];
            if (v >= hi) {
                high++;
                if (state == 0) {
                    if (!rises++) {
                        first = n;
                    }
                    last = n;
                }
                state = 1;
            } else if (v <= lo) {
                low++;
                state = 0;
            }
        }
        bufs--;
    }
    hsadc.stop();
    const uint32_t overruns = hsadc.overruns(), got = hsadc.sampleRate();
    hsadc.end();
    CONSOLE.printf("HSTREAM %lu %lu %lu %lu %lu %lu %lu %lu\r\n", (unsigned long)got,
                       (unsigned long)overruns, (unsigned long)n, (unsigned long)high, (unsigned long)low,
                       (unsigned long)rises, (unsigned long)first, (unsigned long)last);
}
#endif

#if CH32RV_HAS_QSPI
static size_t unhexq(const char *text, uint8_t *out, size_t room)
{
    size_t n = 0;
    while (text[0] && text[1] && n < room) {
        char pair[3] = {text[0], text[1], 0};
        out[n++] = (uint8_t)strtoul(pair, nullptr, 16);
        text += 2;
    }
    return n;
}

static void qspiFind(int argc, char **argv)
{
    bool found = false;
    /* the table's rows: role (0 SCK, 1 SCSN, 2 SIO0, 3 SIO1, 6 SCSXN, 7 SIOX0, 8 SIOX1), pad */
    for (uint8_t q = 1; q <= 2 && !found; q++) {
        const QspiPad *t = nullptr;
        size_t tn = 0;
#ifdef CH32RV_QSPI1_PADS
        static const QspiPad t1[] = CH32RV_QSPI1_PADS;
        if (q == 1) { t = t1; tn = CH32RV_QSPI1_PAD_COUNT; }
#endif
#ifdef CH32RV_QSPI2_PADS
        static const QspiPad t2[] = CH32RV_QSPI2_PADS;
        if (q == 2) { t = t2; tn = CH32RV_QSPI2_PAD_COUNT; }
#endif
        for (uint8_t bank = 0; bank < 2 && t && !found; bank++) {
            const uint8_t roles[4] = {0, (uint8_t)(bank ? 6 : 1), (uint8_t)(bank ? 7 : 2), (uint8_t)(bank ? 8 : 3)};
            long got[4];
            uint8_t have = 0;
            for (uint8_t r = 0; r < 4; r++) {
                for (size_t i = 0; i < tn; i++) {
                    bool listed = false;
                    for (int k = 1; k < argc; k++) {
                        listed = listed || atol(argv[k]) == t[i].pad;
                    }
                    if (t[i].role == roles[r] && listed) {
                        got[r] = t[i].pad;
                        have++;
                        break;
                    }
                }
            }
            if (have == 4) {
                CONSOLE.printf("QSPIFIND %u %ld %ld %ld %ld\r\n", q, got[0], got[1], got[2], got[3]);
                found = true;
            }
        }
    }
    if (!found) {
        CONSOLE.printf("QSPIFIND NONE\r\n");
    }
}

static void qspiRun(char **argv)
{
    long v[6];
    for (uint8_t k = 0; k < 6; k++) {
        v[k] = atol(argv[1 + k]);
    }
    const char kind = argv[7][0];
    uint8_t data[16];
    const size_t n = unhexq(argv[8], data, sizeof data);
    CH32RVQspi q((uint8_t)v[0]);
    bool ok = q.begin((int)v[1], (int)v[2], (int)v[3], (int)v[4]);
    q.setClock((uint32_t)v[5]);
    CH32RVQspiCommand cmd;
    cmd.address = 0x000100;
    cmd.addressLines = 1;
    bool reading = false;
    if (kind == 'c') {
        cmd.instruction = 0x20;
        cmd.address = (uint32_t)data[0] << 16 | (uint32_t)data[1] << 8 | data[2];
        ok = ok && q.command(cmd);
    } else if (kind == 'w') {
        cmd.instruction = 0x02;
        ok = ok && q.write(cmd, data, n);
    } else if (kind == 'd') {
        cmd.instruction = 0xA2;
        cmd.addressLines = 2;
        cmd.dataLines = 2;
        ok = ok && q.write(cmd, data, n);
    } else if (kind == 'r' || kind == 'e') {
        cmd.instruction = kind == 'r' ? 0x0B : 0xBB;
        cmd.addressLines = kind == 'r' ? 1 : 2;
        cmd.dummyCycles = kind == 'r' ? 8 : 4;
        cmd.dataLines = kind == 'r' ? 1 : 2;
        ok = ok && q.read(cmd, data, n);
        reading = true;
    } else {
        ok = false;
    }
    const uint32_t hz = q.clock();
    q.end();
    CONSOLE.printf("QSPI %d ", ok ? 1 : 0);
    if (reading && ok) {
        for (size_t i = 0; i < n; i++) {
            CONSOLE.printf("%02x", data[i]);
        }
    } else {
        CONSOLE.printf("-");
    }
    CONSOLE.printf(" %lu\r\n", (unsigned long)hz);
}
#endif

#if CH32RV_HAS_LTDC
static void ltdcFind(int argc, char **argv)
{
    static const QspiPad t[] = CH32RV_LTDC_PADS;
    long got[6] = {-1, -1, -1, -1, -1, -1};
    int rbit = -1, gbit = -1;
    for (uint8_t want = 0; want < 6; want++) {
        /* the colours' highest bit labelled first (the low bits may sit below the output's width) */
        for (int bit = 7; bit >= 0 && got[want] < 0; bit--) {
        for (const QspiPad &p : t) {
            bool listed = false, used = false;
            for (int k = 1; k < argc; k++) {
                listed = listed || atol(argv[k]) == p.pad;
            }
            for (uint8_t k = 0; k < want; k++) {
                used = used || got[k] == p.pad;
            }
            if (!listed || used) {
                continue;
            }
            const bool fit = want < 4 ? p.role == 24 + want : want == 4 ? p.role == bit : p.role == 8 + bit;
            if (fit) {
                got[want] = p.pad;
                if (want == 4) rbit = p.role;
                if (want == 5) gbit = p.role - 8;
                break;
            }
        }
        if (want < 4) {
            break;   /* the signals have no bits */
        }
        }
        if (got[want] < 0) {
            CONSOLE.printf("LTDCFIND NONE\r\n");
            return;
        }
    }
    CONSOLE.printf("LTDCFIND %ld %ld %ld %ld %ld %d %ld %d\r\n", got[0], got[1], got[2], got[3], got[4], rbit, got[5], gbit);
}

static void ltdcRun(char **argv)
{
    int r[8], g[8], b[8];
    for (uint8_t k = 0; k < 8; k++) {
        r[k] = g[k] = b[k] = -1;
    }
    r[atoi(argv[6]) & 7] = atoi(argv[5]);
    g[atoi(argv[8]) & 7] = atoi(argv[7]);
    CH32RVLtdcTiming tm;
    tm.width = 64;
    tm.height = 32;
    tm.hsync = 4; tm.hbp = 8; tm.hfp = 8;
    tm.vsync = 2; tm.vbp = 4; tm.vfp = 4;
    tm.pixelHz = 1;   /* the slowest the divider makes */
    bool ok = ltdc.setSignalPins(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4])) &&
              ltdc.setColorPins(r, g, b) && ltdc.begin(tm);
    const uint32_t rgb = (uint32_t)strtoul(argv[9], nullptr, 16);
    const int mode = argv[10] ? atoi(argv[10]) : 0;
    if (ok) {
        ltdc.setBackground(mode == 1 ? 0u : rgb);
    }
    if (ok && mode) {
        /* mode 1: a 64 x 32 RGB565 layer of the colour; mode 2: a black one, alpha 0, over the background */
        static uint16_t fb[64 * 32];
        const uint16_t c = mode == 2 ? 0u : (uint16_t)(((rgb >> 19) & 0x1Fu) << 11 | ((rgb >> 10) & 0x3Fu) << 5 |
                                                       ((rgb >> 3) & 0x1Fu));
        for (uint16_t &px : fb) {
            px = c;
        }
        ok = ltdc.layer(1, fb, CH32RV_LTDC_RGB565, 0, 0, 64, 32, mode == 2 ? 0 : 255);
    }
    CONSOLE.printf("LTDC %d %lu %08lx\r\n", ok ? 1 : 0, (unsigned long)ltdc.pixelClock(),
                   (unsigned long)CH32RV_REG32(0x40014810u));   /* GCR, for its data widths */
}
#endif

#if CH32RV_HAS_LPTIM
static volatile uint32_t lptPeriods;
static void lptTick()
{
    lptPeriods++;
}

/* The first CH1 pad among the pins, of LPTIM1 or 2 (beginCounter() tried
 * on each: it takes the pad or refuses it, nothing else). */
static void lptFind(int argc, char **argv)
{
    for (uint8_t t = 1; t <= 2; t++) {
        for (int i = 1; i < argc; i++) {
            CH32RVLowPowerTimer lpt(t);
            const int pin = atoi(argv[i]);
            if (lpt.beginCounter(pin, RISING, true)) {
                lpt.end();
                pinMode(pin, INPUT);
                CONSOLE.printf("LPTFIND %u %d\r\n", t, pin);
                return;
            }
        }
    }
    CONSOLE.printf("LPTFIND NONE\r\n");
}
#endif

static void reply(char *command)
{
    char *argv[34];
    int argc = 0;
    for (char *t = strtok(command, " "); t && argc < 34; t = strtok(nullptr, " ")) {
        argv[argc++] = t;
    }
    if (!argc) {
        return;
    }
    if (!strcmp(argv[0], "ready") && argc == 2) {
        CONSOLE.printf("READY %s %s\r\n", argv[1], BENCH_BUILD_ID);
    } else if (!strcmp(argv[0], "ltdcfind")) {
#if CH32RV_HAS_LTDC
        ltdcFind(argc, argv);
#else
        CONSOLE.printf("LTDCFIND NONE\r\n");
#endif
    } else if (!strcmp(argv[0], "ltdc") && (argc == 10 || argc == 11)) {
#if CH32RV_HAS_LTDC
        ltdcRun(argv);
#else
        CONSOLE.printf("LTDC 2 0 0\r\n");
#endif
    } else if (!strcmp(argv[0], "ltdcoff")) {
#if CH32RV_HAS_LTDC
        ltdc.end();
#endif
        CONSOLE.printf("LTDCOFF\r\n");
    } else if (!strcmp(argv[0], "qspifind")) {
#if CH32RV_HAS_QSPI
        qspiFind(argc, argv);
#else
        CONSOLE.printf("QSPIFIND NONE\r\n");
#endif
    } else if (!strcmp(argv[0], "qspi") && argc == 9) {
#if CH32RV_HAS_QSPI
        qspiRun(argv);
#else
        CONSOLE.printf("QSPI 2 - 0\r\n");
#endif
    } else if (!strncmp(argv[0], "lpt", 3)) {
#if CH32RV_HAS_LPTIM
        if (!strcmp(argv[0], "lptfind")) {
            lptFind(argc, argv);
        } else if (!strcmp(argv[0], "lptcount") && argc == 5) {
            CH32RVLowPowerTimer lpt((uint8_t)atoi(argv[1]));
            const bool ok = lpt.beginCounter(atoi(argv[2]), RISING, atoi(argv[3]) != 0);
            const uint32_t t0 = micros();
            delay((uint32_t)atol(argv[4]));
            const uint16_t n = lpt.count();
            const uint32_t us = micros() - t0;
            CONSOLE.printf("LPTCOUNT %d %u %lu\r\n", ok ? 1 : 0, (unsigned)n, (unsigned long)us);
            lpt.end();
            pinMode(atoi(argv[2]), INPUT);
        } else if (!strcmp(argv[0], "lpttimer") && argc == 6) {
            CH32RVLowPowerTimer lpt((uint8_t)atoi(argv[1]));
            bool ok = lpt.begin((CH32RVLowPowerTimer::Clock)atoi(argv[2]), (uint8_t)atoi(argv[3])) &&
                      lpt.setPeriod((uint32_t)atol(argv[4]));
            lptPeriods = 0;
            lpt.onPeriod(lptTick);
            ok = ok && lpt.start();
            const uint32_t t0 = micros();
            delay((uint32_t)atol(argv[5]));
            const uint32_t n = lptPeriods;
            const uint32_t us = micros() - t0;
            CONSOLE.printf("LPTTIMER %d %lu %lu %lu\r\n", ok ? 1 : 0, (unsigned long)lpt.clockHz(), (unsigned long)n,
                           (unsigned long)us);
            lpt.end();
        } else if (!strcmp(argv[0], "lptwake") && argc == 3) {
            CH32RVLowPowerTimer lpt((uint8_t)atoi(argv[1]));
            const uint32_t ms = (uint32_t)atol(argv[2]);
            bool ok = lpt.begin(CH32RVLowPowerTimer::CLOCK_LSI, 32);
            ok = ok && lpt.setPeriod((uint32_t)((uint64_t)lpt.clockHz() * ms / 1000u));
            lptPeriods = 0;
            lpt.onPeriod(lptTick);
            /* the RTC's timer as a backstop three times later: it also keeps millis() over the sleep */
            ok = ok && CH32RV.enableExtiLineWakeup(lpt.wakeLine()) && CH32RV.enableTimerWakeup(3000ull * ms) &&
                 lpt.start();
            CONSOLE.flush();
            const uint32_t t0 = millis();
            if (ok) {
                CH32RV.lightSleep();
            }
            const uint32_t slept = millis() - t0;
            const uint32_t n = lptPeriods;
            lpt.end();
            CH32RV.disableWakeup();
            CONSOLE.printf("LPTWAKE %d %d %lu %lu\r\n", ok ? 1 : 0, (int)CH32RV.wakeupCause(), (unsigned long)slept,
                           (unsigned long)n);
        } else {
            CONSOLE.printf("LPT ?\r\n");
        }
#else
        CONSOLE.printf("LPT NONE\r\n");
#endif
    } else if (!strcmp(argv[0], "uart2") && argc == 4) {
#if defined(ROUTED)
        ROUTED.begin((unsigned long)atol(argv[3]), SERIAL_8N1, atoi(argv[1]), atoi(argv[2]));
        echoing = (bool)ROUTED;
        echoed = 0;
#endif
#if defined(ROUTED)
        CONSOLE.printf("UART2 %d %d\r\n", echoing ? 1 : 0, (int)ROUTED.peripheral());
#else
        CONSOLE.printf("UART2 0 0\r\n");
#endif
    } else if (!strcmp(argv[0], "stop")) {
#if defined(ROUTED)
        ROUTED.end();
#endif
        echoing = false;
        CONSOLE.printf("STOP %lu\r\n", (unsigned long)echoed);
    } else if (!strcmp(argv[0], "acaps") && argc == 2) {
        const pin_size_t pin = (pin_size_t)atoi(argv[1]);
#if defined(NUM_ANALOG_INPUTS)
        const int channel = digitalPinIsValid(pin) && digitalPinHasADC(pin) ? (int)digitalPinToAnalogChannel(pin) : -1;
#else
        const int channel = -1;
        (void)pin;
#endif
        CONSOLE.printf("ACAPS %d\r\n", channel);
    } else if (!strcmp(argv[0], "adc") && argc == 2) {
        const pin_size_t pin = (pin_size_t)atoi(argv[1]);
        pinMode(pin, INPUT);
        analogReadResolution(CH32RV_ADC_BITS);
        const int reading = analogRead(pin);
        CONSOLE.printf("ADC %d %d %lu\r\n", reading, (1 << CH32RV_ADC_BITS) - 1,
                           (unsigned long)ch32rv_adc_vdd_millivolts());
    } else if (!strcmp(argv[0], "ares") && argc == 4) {
        const pin_size_t pin = (pin_size_t)atoi(argv[1]);
        pinMode(pin, INPUT);
        analogReference((uint8_t)atoi(argv[3]));
        analogReadResolution(atoi(argv[2]));
        const unsigned long reading = (unsigned long)analogRead(pin);
        analogReadResolution(CH32RV_ADC_BITS);
        CONSOLE.printf("ARES %lu %d\r\n", reading, CH32RV_ADC_BITS);
    } else if (!strcmp(argv[0], "hscaps") && argc == 2) {
#if PERIPH_HSADC
        CONSOLE.printf("HSCAPS %d\r\n", CH32RVHsAdc::channelOf((pin_size_t)atoi(argv[1])));
#else
        CONSOLE.printf("HSCAPS -1\r\n");
#endif
    } else if (!strcmp(argv[0], "hstream") && argc == 4) {
#if PERIPH_HSADC
        hstream((uint32_t)atol(argv[1]), (uint32_t)atol(argv[2]), (pin_size_t)atoi(argv[3]));
#else
        CONSOLE.printf("HSTREAM 0\r\n");
#endif
    } else if (!strcmp(argv[0], "has")) {
#if PERIPH_ATIMER
        const int atimer_in = 1;
#else
        const int atimer_in = 0;
#endif
#if PERIPH_AEXT
        const int aext_in = 1;
#else
        const int aext_in = 0;
#endif
#if PERIPH_ACONT
        const int acont_in = 1;
#else
        const int acont_in = 0;
#endif
#if PERIPH_INJ
        const int inj_in = 1;
#else
        const int inj_in = 0;
#endif
        CONSOLE.printf("HAS %d %d %d %d\r\n", atimer_in, aext_in, acont_in, inj_in);
    } else if (!strcmp(argv[0], "atimer") && argc == 4) {
#if PERIPH_ATIMER
        atimer((uint32_t)atol(argv[1]), (uint32_t)atol(argv[2]), (pin_size_t)atoi(argv[3]));
#else
        CONSOLE.printf("ATIMER 0\r\n");
#endif
    } else if (!strcmp(argv[0], "tcaps") && argc == 2) {
#if PERIPH_AEXT
        const pin_size_t pin = (pin_size_t)atoi(argv[1]);
        const bool valid = digitalPinIsValid(pin);
        CONSOLE.printf("TCAPS %d %d\r\n", valid && CH32RVAdcStream::isExternalTriggerPin(pin) ? 1 : 0,
                           valid && CH32RVAdcStream::isExternalTriggerPin(pin, CHANGE) ? 1 : 0);
#else
        CONSOLE.printf("TCAPS 0 0\r\n");
#endif
    } else if (!strcmp(argv[0], "aext") && argc >= 4) {
#if PERIPH_AEXT
        aext((pin_size_t)atoi(argv[1]), atoi(argv[2]), (pin_size_t)atoi(argv[3]), argc > 4 ? (uint32_t)atol(argv[4]) : 0u);
#else
        CONSOLE.printf("AEXT 0\r\n");
#endif
    } else if (!strcmp(argv[0], "aextread")) {
#if PERIPH_AEXT
        aextread();
#else
        CONSOLE.printf("AEXTREAD 0 -\r\n");
#endif
    } else if (!strcmp(argv[0], "aextrate") && argc == 2) {
#if PERIPH_AEXT
        aextrate((uint32_t)atol(argv[1]));
#else
        CONSOLE.printf("AEXTRATE 0 0\r\n");
#endif
    } else if (!strcmp(argv[0], "aextstop")) {
#if PERIPH_AEXT
        stream.end();
        ext_running = false;
#endif
        CONSOLE.printf("AEXTSTOP\r\n");
    } else if (!strcmp(argv[0], "acont") && argc == 5) {
#if PERIPH_ACONT
        acont((uint32_t)atol(argv[1]), (uint32_t)atol(argv[2]), (uint32_t)atol(argv[3]), (pin_size_t)atoi(argv[4]));
#else
        CONSOLE.printf("ACONT 0\r\n");
#endif
    } else if (!strcmp(argv[0], "awd") && argc == 3) {
#if defined(NUM_ANALOG_INPUTS)
        const uint16_t full = (uint16_t)((1u << CH32RV_ADC_BITS) - 1u);
        const bool high = atoi(argv[2]) != 0;
        const uint16_t lo = high ? (uint16_t)(full - full / 4) : 0, hi = high ? full : (uint16_t)(full / 4);
        bool ok = stream.begin((pin_size_t)atoi(argv[1]), 50000);
        ok = ok && stream.startContinuous(stream_buf, ASTREAM_HALF) && stream.setWatchdog(lo, hi);
        delay(50);
        const bool tripped = ok && stream.watchdogTripped();
        delay(50);
        const bool again = ok && stream.watchdogTripped();
        stream.clearWatchdog();
        stream.end();
        CONSOLE.printf("AWD %d %d %d\r\n", ok ? 1 : 0, tripped ? 1 : 0, again ? 1 : 0);
#else
        CONSOLE.printf("AWD 0 0 0\r\n");
#endif
    } else if (!strcmp(argv[0], "inj") && argc == 2) {
#if PERIPH_INJ
        /* the injected group: one round by software, then rounds at 1 kHz from a timer for 500 ms */
        static CH32RVAdcInjected inj;
        const pin_size_t pin = (pin_size_t)atoi(argv[1]);
        uint16_t v = 0;
        const bool ok = inj.begin(&pin, 1) && inj.convert(&v);
        const bool timed = ok && inj.startTimer(1000);
        const uint32_t r0 = inj.rounds(), t0 = micros();
        delay(500);
        const uint32_t rounds = inj.rounds() - r0, us = micros() - t0;
        const uint16_t last = inj.value(0);
        const uint32_t rate = inj.rate();
        inj.end();
        CONSOLE.printf("INJ %d %u %d %lu %lu %lu %u %d\r\n", ok ? 1 : 0, v, timed ? 1 : 0, (unsigned long)rate,
                           (unsigned long)rounds, (unsigned long)us, last, CH32RV_ADC_BITS);
#else
        CONSOLE.printf("INJ 0 0 0 0 0 0 0 0\r\n");
#endif
    } else if (!strcmp(argv[0], "astream") && argc >= 4) {
#if defined(NUM_ANALOG_INPUTS)
        pin_size_t pins[3] = {};
        for (int i = 3; i < argc; i++) {
            pins[i - 3] = (pin_size_t)atoi(argv[i]);
        }
        astream((uint32_t)atol(argv[1]), (uint32_t)atol(argv[2]), pins, (uint8_t)(argc - 3));
#else
        CONSOLE.printf("ASTREAM 0\r\n");
#endif
    } else {
        CONSOLE.printf("UNKNOWN %s\r\n", argv[0]);
    }
}

void setup()
{
    CONSOLE_BEGIN(115200);
    CONSOLE.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    CONSOLE.println(CH32RV.resetReasonName());
}

void loop()
{
#if defined(ROUTED)
    while (echoing && ROUTED.available()) {
        ROUTED.write((uint8_t)ROUTED.read());
        echoed++;
    }
#endif
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

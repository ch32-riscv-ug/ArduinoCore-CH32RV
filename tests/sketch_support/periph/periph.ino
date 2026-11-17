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
 *   adc <pin>                 ADC <reading> <full scale>
 *   acaps <pin>               ACAPS <ADC channel of the pad, or -1>
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
 *   atimer <rate> <halves> <pin>
 *                             CH32RVAdcStream.beginTimer() on the pin at <rate>, read as
 *                             astream does (one pin): ATIMER <ok> <rate got> <timer>
 *                             <step> <overruns> <rounds> <high> <low> <rises> <first rise>
 *                             <last rise> <conversions> <us> (the last two: conversions()
 *                             counted over micros() from the first half's end to the last's;
 *                             ATIMER 0 when beginTimer() refused)
 *   tcaps <pin>               TCAPS <1 when the pad can start the ADC by its edge, else 0>
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
 *                             (HSTREAM 0 when begin() refused) */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <string.h>
#if defined(NUM_ANALOG_INPUTS)
#include <CH32RVAdcStream.h>
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
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
#define PERIPH_HSADC 1
#include <CH32RVHsAdc.h>
#endif
/* The USART uart2 moves around: Serial1, or Serial where the part has one
 * USART (V003's Serial is USART1; this sketch's console is SerialDMSeq). */
#if defined(SERIAL_PORT_HARDWARE1)
#define ROUTED Serial1
#elif defined(SERIAL_PORT_HARDWARE)
#define ROUTED Serial
#endif

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static char line[64];
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
        SerialDMSeq.printf("ASTREAM 0\r\n");
        return;
    }
    const uint16_t full = (uint16_t)((1u << CH32RV_ADC_BITS) - 1u);
    stream.startContinuous(stream_buf, (uint16_t)(ASTREAM_HALF / count * count));
    Scan r;
    scan(halves, count, 2000, r);
    stream.end();
    SerialDMSeq.printf("ASTREAM %lu %u %lu %lu %lu %lu %lu %lu %lu %u %u %u %lu %lu\r\n", (unsigned long)stream.sampleRate(),
                       stream.sampleTimeStep(), (unsigned long)stream.overruns(), (unsigned long)r.round,
                       (unsigned long)r.high, (unsigned long)r.low, (unsigned long)r.rises, (unsigned long)r.first,
                       (unsigned long)r.last, count > 1 ? r.min1 : 0, count > 1 ? r.max1 : 0, full,
                       (unsigned long)r.high1n, (unsigned long)r.low1n);
}

#if PERIPH_ATIMER
static void atimer(uint32_t rate, uint32_t halves, pin_size_t pin)
{
    if (!stream.beginTimer(pin, rate)) {
        SerialDMSeq.printf("ATIMER 0\r\n");
        return;
    }
    stream.startContinuous(stream_buf, ASTREAM_HALF);
    Scan r;
    /* The halves' time at the rate, and a second more. */
    scan(halves, 1, (uint32_t)((uint64_t)(halves + 1u) * ASTREAM_HALF * 1000u / rate) + 1000u, r);
    const uint32_t got = stream.sampleRate(), overruns = stream.overruns();
    const uint8_t timer = stream.triggerTimer(), step = stream.sampleTimeStep();
    stream.end();
    SerialDMSeq.printf("ATIMER 1 %lu %u %u %lu %lu %lu %lu %lu %lu %lu %lu %lu\r\n", (unsigned long)got, timer, step,
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
    pinMode(trigger, INPUT);
    ext_running = stream.beginExternal(pin, trigger, (PinStatus)edge, max_rate) &&
                  stream.startContinuous(stream_buf, ASTREAM_HALF);
    SerialDMSeq.printf("AEXT %d\r\n", ext_running ? 1 : 0);
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
    SerialDMSeq.printf("AEXTREAD %lu %s\r\n", (unsigned long)n, k ? levels : "-");
}

static void aextrate(uint32_t ms)
{
    const uint32_t c0 = stream.conversions(), t0 = micros();
    while (micros() - t0 < ms * 1000u) {
    }
    const uint32_t c1 = stream.conversions(), t1 = micros();
    SerialDMSeq.printf("AEXTRATE %lu %lu\r\n", (unsigned long)(c1 - c0), (unsigned long)(t1 - t0));
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
        SerialDMSeq.printf("ACONT 0\r\n");
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
    SerialDMSeq.printf("ACONT 1 %lu %lu %d %d %u\r\n", (unsigned long)seen, (unsigned long)span,
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
        SerialDMSeq.printf("HSTREAM 0\r\n");
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
    SerialDMSeq.printf("HSTREAM %lu %lu %lu %lu %lu %lu %lu %lu\r\n", (unsigned long)got,
                       (unsigned long)overruns, (unsigned long)n, (unsigned long)high, (unsigned long)low,
                       (unsigned long)rises, (unsigned long)first, (unsigned long)last);
}
#endif

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
        SerialDMSeq.printf("READY %s %s\r\n", argv[1], BENCH_BUILD_ID);
    } else if (!strcmp(argv[0], "uart2") && argc == 4) {
#if defined(ROUTED)
        ROUTED.begin((unsigned long)atol(argv[3]), SERIAL_8N1, atoi(argv[1]), atoi(argv[2]));
        echoing = (bool)ROUTED;
        echoed = 0;
#endif
#if defined(ROUTED)
        SerialDMSeq.printf("UART2 %d %d\r\n", echoing ? 1 : 0, (int)ROUTED.peripheral());
#else
        SerialDMSeq.printf("UART2 0 0\r\n");
#endif
    } else if (!strcmp(argv[0], "stop")) {
#if defined(ROUTED)
        ROUTED.end();
#endif
        echoing = false;
        SerialDMSeq.printf("STOP %lu\r\n", (unsigned long)echoed);
    } else if (!strcmp(argv[0], "acaps") && argc == 2) {
        const pin_size_t pin = (pin_size_t)atoi(argv[1]);
#if defined(NUM_ANALOG_INPUTS)
        const int channel = digitalPinIsValid(pin) && digitalPinHasADC(pin) ? (int)digitalPinToAnalogChannel(pin) : -1;
#else
        const int channel = -1;
        (void)pin;
#endif
        SerialDMSeq.printf("ACAPS %d\r\n", channel);
    } else if (!strcmp(argv[0], "adc") && argc == 2) {
        const pin_size_t pin = (pin_size_t)atoi(argv[1]);
        pinMode(pin, INPUT);
        analogReadResolution(CH32RV_ADC_BITS);
        SerialDMSeq.printf("ADC %d %d\r\n", analogRead(pin), (1 << CH32RV_ADC_BITS) - 1);
    } else if (!strcmp(argv[0], "hscaps") && argc == 2) {
#if PERIPH_HSADC
        SerialDMSeq.printf("HSCAPS %d\r\n", CH32RVHsAdc::channelOf((pin_size_t)atoi(argv[1])));
#else
        SerialDMSeq.printf("HSCAPS -1\r\n");
#endif
    } else if (!strcmp(argv[0], "hstream") && argc == 4) {
#if PERIPH_HSADC
        hstream((uint32_t)atol(argv[1]), (uint32_t)atol(argv[2]), (pin_size_t)atoi(argv[3]));
#else
        SerialDMSeq.printf("HSTREAM 0\r\n");
#endif
    } else if (!strcmp(argv[0], "atimer") && argc == 4) {
#if PERIPH_ATIMER
        atimer((uint32_t)atol(argv[1]), (uint32_t)atol(argv[2]), (pin_size_t)atoi(argv[3]));
#else
        SerialDMSeq.printf("ATIMER 0\r\n");
#endif
    } else if (!strcmp(argv[0], "tcaps") && argc == 2) {
#if PERIPH_AEXT
        const pin_size_t pin = (pin_size_t)atoi(argv[1]);
        SerialDMSeq.printf("TCAPS %d\r\n", digitalPinIsValid(pin) && CH32RVAdcStream::isExternalTriggerPin(pin) ? 1 : 0);
#else
        SerialDMSeq.printf("TCAPS 0\r\n");
#endif
    } else if (!strcmp(argv[0], "aext") && argc >= 4) {
#if PERIPH_AEXT
        aext((pin_size_t)atoi(argv[1]), atoi(argv[2]), (pin_size_t)atoi(argv[3]), argc > 4 ? (uint32_t)atol(argv[4]) : 0u);
#else
        SerialDMSeq.printf("AEXT 0\r\n");
#endif
    } else if (!strcmp(argv[0], "aextread")) {
#if PERIPH_AEXT
        aextread();
#else
        SerialDMSeq.printf("AEXTREAD 0 -\r\n");
#endif
    } else if (!strcmp(argv[0], "aextrate") && argc == 2) {
#if PERIPH_AEXT
        aextrate((uint32_t)atol(argv[1]));
#else
        SerialDMSeq.printf("AEXTRATE 0 0\r\n");
#endif
    } else if (!strcmp(argv[0], "aextstop")) {
#if PERIPH_AEXT
        stream.end();
        ext_running = false;
#endif
        SerialDMSeq.printf("AEXTSTOP\r\n");
    } else if (!strcmp(argv[0], "acont") && argc == 5) {
#if PERIPH_ACONT
        acont((uint32_t)atol(argv[1]), (uint32_t)atol(argv[2]), (uint32_t)atol(argv[3]), (pin_size_t)atoi(argv[4]));
#else
        SerialDMSeq.printf("ACONT 0\r\n");
#endif
    } else if (!strcmp(argv[0], "astream") && argc >= 4) {
#if defined(NUM_ANALOG_INPUTS)
        pin_size_t pins[3] = {};
        for (int i = 3; i < argc; i++) {
            pins[i - 3] = (pin_size_t)atoi(argv[i]);
        }
        astream((uint32_t)atol(argv[1]), (uint32_t)atol(argv[2]), pins, (uint8_t)(argc - 3));
#else
        SerialDMSeq.printf("ASTREAM 0\r\n");
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
#if defined(ROUTED)
    while (echoing && ROUTED.available()) {
        ROUTED.write((uint8_t)ROUTED.read());
        echoed++;
    }
#endif
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

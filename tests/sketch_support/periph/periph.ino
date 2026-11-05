/* Single-line peripherals on labelled lines (tests/instrumented/periph): a
 * second UART and an ADC input, on the pads the bench labels (bench_lines.py:
 * uart2, adc; analogWrite() is tests/instrumented/signals'). Pins are the
 * sketch's pin numbers.
 *
 *   ready <nonce>             READY <nonce> <build id>
 *   uart2 <rx> <tx> <baud>    UART2 <ok> <USART>: Serial1 on those pads (the
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
 *                             <pin1 min> <pin1 max> <full scale> (rounds counted from the
 *                             first read; ASTREAM 0 when begin() refused) */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <string.h>
#if defined(NUM_ANALOG_INPUTS)
#include <CH32RVAdcStream.h>
#endif

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static char line[64];
static uint8_t used;
static bool echoing;
static uint32_t echoed;

#if defined(NUM_ANALOG_INPUTS)
#define ASTREAM_HALF 256
static CH32RVAdcStream stream;
static uint16_t stream_buf[2 * ASTREAM_HALF];

static void astream(uint32_t rate, uint32_t halves, const pin_size_t *pins, uint8_t count)
{
    if (!stream.begin(pins, count, rate)) {
        SerialDMSeq.printf("ASTREAM 0\r\n");
        return;
    }
    const uint16_t full = (uint16_t)((1u << CH32RV_ADC_BITS) - 1u);
    const uint16_t lo = full / 4, hi = (uint16_t)(full - full / 4);
    const uint16_t per = (uint16_t)(ASTREAM_HALF / count * count);
    stream.startContinuous(stream_buf, per);
    uint32_t round = 0, high = 0, low = 0, rises = 0, first = 0, last = 0;
    uint16_t min1 = 0xFFFF, max1 = 0;
    int8_t state = -1;            /* 0 low, 1 high, -1 not yet known */
    bool skipped = false;
    const uint32_t start = millis();
    while (halves && millis() - start < 2000) {
        const uint16_t *half = stream.readBuffer();
        if (!half) {
            continue;
        }
        if (!skipped) {
            skipped = true;       /* the first half starts mid-setup */
            continue;
        }
        for (uint16_t i = 0; i < per; i += count, round++) {
            const uint16_t v = half[i];
            if (v >= hi) {
                high++;
                if (state == 0) {
                    if (!rises++) {
                        first = round;
                    }
                    last = round;
                }
                state = 1;
            } else if (v <= lo) {
                low++;
                state = 0;
            }
            if (count > 1) {
                min1 = half[i + 1] < min1 ? half[i + 1] : min1;
                max1 = half[i + 1] > max1 ? half[i + 1] : max1;
            }
        }
        halves--;
    }
    stream.end();
    SerialDMSeq.printf("ASTREAM %lu %u %lu %lu %lu %lu %lu %lu %lu %u %u %u\r\n", (unsigned long)stream.sampleRate(),
                       stream.sampleTimeStep(), (unsigned long)stream.overruns(), (unsigned long)round,
                       (unsigned long)high, (unsigned long)low, (unsigned long)rises, (unsigned long)first,
                       (unsigned long)last, count > 1 ? min1 : 0, count > 1 ? max1 : 0, full);
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
#if defined(SERIAL_PORT_HARDWARE1)
        Serial1.begin((unsigned long)atol(argv[3]), SERIAL_8N1, atoi(argv[1]), atoi(argv[2]));
        echoing = (bool)Serial1;
        echoed = 0;
#endif
#if defined(SERIAL_PORT_HARDWARE1)
        SerialDMSeq.printf("UART2 %d %d\r\n", echoing ? 1 : 0, (int)Serial1.peripheral());
#else
        SerialDMSeq.printf("UART2 0 0\r\n");
#endif
    } else if (!strcmp(argv[0], "stop")) {
#if defined(SERIAL_PORT_HARDWARE1)
        Serial1.end();
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

void setup() { SerialDMSeq.begin(115200); }

void loop()
{
#if defined(SERIAL_PORT_HARDWARE1)
    while (echoing && Serial1.available()) {
        Serial1.write((uint8_t)Serial1.read());
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

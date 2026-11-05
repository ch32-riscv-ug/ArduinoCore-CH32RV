/* Single-line peripherals on labelled lines (tests/instrumented/periph): a
 * second UART and an ADC input, on the pads the bench labels (bench_lines.py:
 * uart2, adc; analogWrite() is tests/instrumented/signals'). Pins are the
 * sketch's pin numbers.
 *
 *   ready <nonce>             READY <nonce> <build id>
 *   uart2 <rx> <tx> <baud>    UART2 <ok>: Serial1 on those pads, then every byte
 *                             it receives goes back out (until `stop`)
 *   stop                      STOP <bytes echoed>
 *   adc <pin>                 ADC <reading> <full scale>
 *   acaps <pin>               ACAPS <ADC channel of the pad, or -1> */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static char line[64];
static uint8_t used;
static bool echoing;
static uint32_t echoed;

static void reply(char *command)
{
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
    } else if (!strcmp(argv[0], "uart2") && argc == 4) {
#if defined(SERIAL_PORT_HARDWARE1)
        Serial1.begin((unsigned long)atol(argv[3]), SERIAL_8N1, atoi(argv[1]), atoi(argv[2]));
        echoing = (bool)Serial1;
        echoed = 0;
#endif
        SerialDMSeq.printf("UART2 %d\r\n", echoing ? 1 : 0);
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

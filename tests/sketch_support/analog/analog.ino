/* The DAC through analogWrite() (tests/instrumented/analog), read back by the
 * ADC on the same pad - no wiring: the DAC drives its pin, which is an ADC
 * input on every family that has one. Console on SerialDMSeq:
 *
 *   ready <nonce>             READY <nonce> <build id>
 *   dacpins                   DACPINS <DAC1 pin or -1> <DAC2 pin or -1>
 *   dac <pin> <value> <bits>  analogWriteResolution(bits), analogWrite(pin, value), 2 ms,
 *                             then analogRead(pin) at the ADC's own width, eight times:
 *                             DAC <mean> <min> <max> <full scale> */
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
    } else if (!strcmp(argv[0], "dacpins")) {
#if defined(CH32RV_DAC1_PIN)
        const int dac1 = (int)CH32RV_DAC1_PIN;
#else
        const int dac1 = -1;
#endif
#if defined(CH32RV_DAC2_PIN)
        const int dac2 = (int)CH32RV_DAC2_PIN;
#else
        const int dac2 = -1;
#endif
        SerialDMSeq.printf("DACPINS %d %d\r\n", dac1, dac2);
    } else if (!strcmp(argv[0], "dac") && argc == 4) {
        const pin_size_t pin = (pin_size_t)atoi(argv[1]);
        analogWriteResolution(atoi(argv[3]));
        analogWrite(pin, atoi(argv[2]));
        delay(2);
#if defined(NUM_ANALOG_INPUTS)
        analogReadResolution(CH32RV_ADC_BITS);
        uint32_t sum = 0;
        int lo = 0x7FFF, hi = 0;
        for (int i = 0; i < 8; i++) {
            const int v = analogRead(pin);
            sum += (uint32_t)v;
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
        SerialDMSeq.printf("DAC %lu %d %d %d\r\n", (unsigned long)(sum / 8u), lo, hi, (1 << CH32RV_ADC_BITS) - 1);
#else
        SerialDMSeq.printf("DAC 0 0 0 0\r\n");
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

/* The DAC through analogWrite() (tests/instrumented/analog), read back by the
 * ADC on the same pad - no wiring: the DAC drives its pin, which is an ADC
 * input on every family that has one. Console on SerialDMSeq:
 *
 *   ready <nonce>             READY <nonce> <build id>
 *   dacpins                   DACPINS <DAC1 pin or -1> <DAC2 pin or -1>
 *   dac <pin> <value> <bits>  analogWriteResolution(bits), analogWrite(pin, value), 2 ms,
 *                             then analogRead(pin) at the ADC's own width, eight times:
 *                             DAC <mean> <min> <max> <full scale>
 *   dacwave <channel> <t|n|p> <bits> <hz> <base>
 *                             CH32RVDac: a triangle (t), noise (n) or a ramp of 32 samples played by DMA (p) of
 *                             `bits` above `base` at `hz` steps, 20 ms, then analogRead() on its pad 200 times
 *                             100 us apart, then dac.end(): DACWAVE <started> <steps a second> <min> <max> <mean>
 *                             <changes between reads> <the ADC's full scale> (DACWAVE 0 when it refused)
 *   adcdual s <v1> <v2> <hz>  CH32RVAdcStream's dual mode, simultaneous: analogWrite(12 bits) v1 on DAC1's pad, v2 on
 *                             DAC2's; ADC1 on DAC1's pad free-running (hz 0) or on a timer at hz, ADC2 on DAC2's
 *                             pad with it; one half of 256 values: ADCDUAL 1 <rounds a second> <ADC1 mean> <min>
 *                             <max> <ADC2 mean> <min> <max>
 *   adcdual i <hz>            interleaved: CH32RVDac plays 0, 4095 on DAC1 at hz (a square of hz / 2), both ADCs in
 *                             turn on its pad; one half of 1024 samples cut at mid-scale (with hysteresis) into runs
 *                             high and low: ADCDUAL 1 <samples a second> <runs> <shortest> <longest> (the whole
 *                             runs: not the first and the last) <free-running rate of one ADC at the same step>
 *                             (ADCDUAL 0 when refused or no half came; the command is there only with
 *                             CH32RV_ADC_DUALMOD_SHIFT and a DAC) */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <CH32RVDac.h>
#include <CH32RVAdcStream.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static char line[64];
static uint16_t ramp[32];
static uint8_t used;

#if defined(CH32RV_ADC_DUALMOD_SHIFT) && defined(CH32RV_DAC1_PIN) && defined(CH32RV_DAC2_PIN)
#define ANALOG_DUAL 1
static uint16_t dualBuf[2 * 1024];

static const uint16_t *dualHalf(CH32RVAdcStream &adc)
{
    const uint32_t t0 = millis();
    const uint16_t *half;
    while (!(half = adc.readBuffer()) && millis() - t0 < 500u) {
    }
    return half;
}

static void dualReply(char **argv, int argc)
{
    CH32RVAdcStream adc;
    if (argv[1][0] == 's' && argc == 5) {
        analogWriteResolution(12);
        analogWrite(CH32RV_DAC1_PIN, atoi(argv[2]));
        analogWrite(CH32RV_DAC2_PIN, atoi(argv[3]));
        delay(2);
        const uint32_t hz = (uint32_t)atol(argv[4]);
        bool ok = hz ? adc.beginTimer(CH32RV_DAC1_PIN, hz) : adc.begin(CH32RV_DAC1_PIN, 100000);
        ok = ok && adc.simultaneous(CH32RV_DAC2_PIN) && adc.startContinuous(dualBuf, 256);
        const uint16_t *half = ok ? dualHalf(adc) : nullptr;
        if (half) {
            uint32_t sum[2] = {0, 0};
            int lo[2] = {0x7FFF, 0x7FFF}, hi[2] = {0, 0};
            for (int i = 0; i < 256; i++) {
                const int k = i & 1, v = half[i];
                sum[k] += (uint32_t)v;
                lo[k] = v < lo[k] ? v : lo[k];
                hi[k] = v > hi[k] ? v : hi[k];
            }
            SerialDMSeq.printf("ADCDUAL 1 %lu %lu %d %d %lu %d %d\r\n", (unsigned long)adc.sampleRate(),
                               (unsigned long)(sum[0] / 128u), lo[0], hi[0], (unsigned long)(sum[1] / 128u), lo[1],
                               hi[1]);
            adc.end();
            return;
        }
    } else if (argv[1][0] == 'i' && argc == 3) {
        /* One ADC alone at the step interleaving takes, for comparison. */
        uint32_t single = 0;
        if (adc.begin(CH32RV_DAC1_PIN, 0xFFFFFFFFu)) {
            single = adc.sampleRate();
        }
        adc.end();
        static const uint16_t square[2] = {0, 4095};
        CH32RVDac dac(1);
        bool ok = dac.begin() && dac.play(square, 2, (uint32_t)atol(argv[2]));
        delay(2);
        ok = ok && adc.beginInterleaved(CH32RV_DAC1_PIN) && adc.startContinuous(dualBuf, 1024);
        const uint16_t *half = ok ? dualHalf(adc) : nullptr;
        if (half) {
            int state = half[0] >= 2048, run = 0, runs = 0, shortest = 0x7FFF, longest = 0, edges = 0;
            for (int i = 1; i < 1024; i++) {
                run++;
                const int v = half[i];
                if ((state && v < 1024) || (!state && v > 3072)) {
                    state = !state;
                    if (edges++) {           /* the first run started before the half */
                        runs++;
                        shortest = run < shortest ? run : shortest;
                        longest = run > longest ? run : longest;
                    }
                    run = 0;
                }
            }
            SerialDMSeq.printf("ADCDUAL 1 %lu %d %d %d %lu\r\n", (unsigned long)adc.sampleRate(), runs, shortest,
                               longest, (unsigned long)single);
            adc.end();
            dac.end();
            return;
        }
        dac.end();
    }
    adc.end();
    SerialDMSeq.printf("ADCDUAL 0\r\n");
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
    } else if (!strcmp(argv[0], "dacwave") && argc == 6) {
        const uint8_t channel = (uint8_t)atoi(argv[1]);
        const uint8_t bits = (uint8_t)atoi(argv[3]);
        const uint32_t hz = (uint32_t)atol(argv[4]);
        const uint16_t base = (uint16_t)atoi(argv[5]);
        CH32RVDac dac(channel);
        bool ok = dac.begin();
        if (ok && argv[2][0] == 't') {
            ok = dac.triangle(bits, hz, base);
        } else if (ok && argv[2][0] == 'n') {
            ok = dac.noise(bits, hz, base);
        } else if (ok) {
            for (unsigned i = 0; i < 32u; i++) {
                ramp[i] = (uint16_t)(base + (((1u << bits) - 1u) * i) / 31u);
            }
            ok = dac.play(ramp, 32, hz);
        }
#if defined(NUM_ANALOG_INPUTS)
        if (ok) {
            const pin_size_t pin = (pin_size_t)(channel == 2 ?
#if defined(CH32RV_DAC2_PIN)
                                                CH32RV_DAC2_PIN
#else
                                                0
#endif
                                                :
#if defined(CH32RV_DAC1_PIN)
                                                CH32RV_DAC1_PIN
#else
                                                0
#endif
                                               );
            delay(20);
            analogReadResolution(CH32RV_ADC_BITS);
            uint32_t sum = 0;
            int lo = 0x7FFF, hi = 0, last = -1, changes = 0;
            for (int i = 0; i < 200; i++) {
                const int v = analogRead(pin);
                sum += (uint32_t)v;
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
                changes += last >= 0 && v != last;
                last = v;
                delayMicroseconds(100);
            }
            const uint32_t rate = dac.stepRate();
            dac.end();
            SerialDMSeq.printf("DACWAVE 1 %lu %d %d %lu %d %d\r\n", (unsigned long)rate, lo, hi,
                               (unsigned long)(sum / 200u), changes, (1 << CH32RV_ADC_BITS) - 1);
            return;
        }
#endif
        dac.end();
        SerialDMSeq.printf("DACWAVE 0\r\n");
#if defined(ANALOG_DUAL)
    } else if (!strcmp(argv[0], "adcdual") && argc >= 3) {
        dualReply(argv, argc);
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

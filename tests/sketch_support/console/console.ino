/* The debug consoles (tests/instrumented/console): SerialRTT, SerialDMDATA or
 * SerialSDI as the sketch's only console, chosen by the build (CONSOLE_RTT /
 * CONSOLE_DMDATA / CONSOLE_SDI, run_hardware.py's flags for the case):
 *
 *   ready <nonce>        READY <nonce> <build id>
 *   echo <text>          ECHO <text>: what came in, back
 *   burst <n>            n lines BURST <i> <FNV-1a 32 of "line <i>", hex>, then BURST DONE (on
 *                        RTT each line waits for availableForWrite(): its write() drops what
 *                        does not fit)
 *
 * SerialSDI only sends: it says SDI <build id> <count> every 100 ms. */
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

#if defined(CONSOLE_RTT)
#include <SerialRTT.h>
#define CONSOLE SerialRTT
#elif defined(CONSOLE_DMDATA)
#include <SerialDMDATA.h>
#define CONSOLE SerialDMDATA
#elif defined(CONSOLE_SDI)
#include <SerialSDI.h>
#define CONSOLE SerialSDI
#else
#error the build names the console: CONSOLE_RTT, CONSOLE_DMDATA or CONSOLE_SDI
#endif

static char line[96];
static uint8_t used;

static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h = (h ^ (uint8_t)*s++) * 16777619u;
    }
    return h;
}

static void reply(const char *command)
{
    if (strncmp(command, "ready ", 6) == 0) {
        CONSOLE.print("READY ");
        CONSOLE.print(command + 6);
        CONSOLE.print(' ');
        CONSOLE.println(BENCH_BUILD_ID);
    } else if (strncmp(command, "echo ", 5) == 0) {
        CONSOLE.print("ECHO ");
        CONSOLE.println(command + 5);
    } else if (strncmp(command, "burst ", 6) == 0) {
        const unsigned long n = strtoul(command + 6, nullptr, 10);
        char text[24];
        for (unsigned long i = 0; i < n; i++) {
            snprintf(text, sizeof text, "line %lu", i);
#if defined(CONSOLE_RTT)
            /* RTT's write() never waits - what does not fit is dropped (libraries/SerialRTT) - so a
             * sketch that wants every line waits for room, here up to 1 s a line. */
            for (const uint32_t t0 = millis(); CONSOLE.availableForWrite() < 32 && millis() - t0 < 1000;) {
            }
#endif
            CONSOLE.print("BURST ");
            CONSOLE.print(i);
            CONSOLE.print(' ');
            CONSOLE.println(fnv(text), HEX);
        }
        CONSOLE.println("BURST DONE");
    } else {
        CONSOLE.println("ERROR COMMAND");
    }
}

void setup()
{
    CONSOLE.begin(115200);
}

void loop()
{
#if defined(CONSOLE_SDI)
    static uint32_t last, count;
    if (millis() - last >= 100) {
        last = millis();
        CONSOLE.print("SDI ");
        CONSOLE.print(BENCH_BUILD_ID);
        CONSOLE.print(' ');
        CONSOLE.println(count++);
    }
#else
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
#endif
}

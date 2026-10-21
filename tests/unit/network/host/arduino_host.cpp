/* millis() and friends on the host, and a main() shaped like the core's. */
#include "Arduino.h"
#include <execinfo.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "api/itoa.h"

static uint64_t now_us()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}
static const uint64_t start_us = now_us();

extern "C" unsigned long millis(void) { return (unsigned long)((now_us() - start_us) / 1000u); }
extern "C" unsigned long micros(void) { return (unsigned long)(now_us() - start_us); }
extern "C" void yield(void) {}
extern "C" void delay(unsigned long ms)
{
    const unsigned long t = millis();
    while (millis() - t < ms) {
        if (ch32rv_net_task) {
            ch32rv_net_task();
        }
        yield();
    }
}

/* newlib has these and glibc does not; the core's itoa.c adds only the long forms. */
extern "C" char *itoa(int value, char *buf, int radix) { return ltoa(value, buf, radix); }
extern "C" char *utoa(unsigned value, char *buf, int radix) { return ultoa(value, buf, radix); }

void setup();
void loop();

/* A watchdog for the tests: when one loop() runs for more than five seconds,
 * where it is goes to stdout (once) - a hang shows up as a stack, not just a
 * timeout. */
static volatile unsigned long loop_started;
static volatile bool stack_shown;

static void watchdog(int)
{
    if (!stack_shown && loop_started && millis() - loop_started > 5000) {
        stack_shown = true;
        static const char head[] = "WATCHDOG loop() stuck:\n";
        if (write(1, head, sizeof head - 1) < 0) {
            return;
        }
        void *frames[64];
        backtrace_symbols_fd(frames, backtrace(frames, 64), 1);
    }
}

int main()
{
    signal(SIGALRM, watchdog);
    const struct itimerval every_second = {{1, 0}, {1, 0}};
    setitimer(ITIMER_REAL, &every_second, nullptr);
    setup();
    for (;;) {
        loop_started = millis();
        loop();
        if (ch32rv_net_task) {
            ch32rv_net_task();
        }
    }
}

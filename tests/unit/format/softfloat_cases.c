/* Host driver for tests/unit/test_core_softfloat.py: the core's __clzsi2 and __extendsfdf2
 * (cores/arduino/ch32rv_softfloat.c, built as core_clz and core_ext) against the host's clz and float to
 * double conversion (which keeps a NaN's sign and payload and makes it quiet, as libgcc's does), over
 * every 251st 32-bit value and the edges; prints one line per failure. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int core_clz(unsigned x);
double core_ext(float a);
double core_add(double a, double b)
{
    return a + b;
}

static int failures;

static void one(uint32_t u)
{
    const int want = u ? __builtin_clz(u) : 32;
    if (core_clz(u) != want && failures++ < 20) {
        printf("FAIL clz(%08x) = %d, want %d\n", u, core_clz(u), want);
    }
    float f;
    memcpy(&f, &u, sizeof f);
    const double a = core_ext(f), b = (double)f;
    if (memcmp(&a, &b, sizeof a) && failures++ < 20) {
        printf("FAIL extend(%08x)\n", u);
    }
}

int main(void)
{
    static const uint32_t edges[] = {0, 1, 0x80000000u, 0x7f800000u, 0xff800000u, 0x7f800001u, 0xff800001u,
                                     0x7fc00000u, 0xffffffffu, 0x007fffffu, 0x00800000u, 0x7f7fffffu, 0x80000001u};
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++) {
        one(edges[i]);
    }
    for (uint64_t u = 0; u <= 0xffffffffull; u += 251) {
        one((uint32_t)u);
        one((uint32_t)u << 9 | (uint32_t)u >> 23); /* the low bits too */
    }
    printf("failures %d\n", failures);
    return failures != 0;
}

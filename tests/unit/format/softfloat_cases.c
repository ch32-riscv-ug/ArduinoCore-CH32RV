/* Host driver for tests/unit/test_core_softfloat.py: the core's __clzsi2, __ctzsi2, __extendsfdf2 and
 * __truncdfsf2 (cores/arduino/ch32rv_softfloat.c, built as core_clz, core_ctz, core_ext and core_trunc)
 * against the host's clz, ctz and float <-> double conversions (IEEE 754 fixes every result but a NaN's, so
 * they are libgcc's too; a NaN keeps its sign and payload, made quiet, from float to double as libgcc's does,
 * and becomes the canonical 0x7fc00000 from double to float as libgcc's does on RISC-V), over every 251st 32-bit
 * value and the edges; for double to float, each such value as the high word with low words at and
 * around the rounding point. Prints one line per failure. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int core_clz(unsigned x);
int core_ctz(unsigned x);
double core_ext(float a);
float core_trunc(double a);

static int failures;

static void one(uint32_t u)
{
    const int want = u ? __builtin_clz(u) : 32;
    if (core_clz(u) != want && failures++ < 20) {
        printf("FAIL clz(%08x) = %d, want %d\n", u, core_clz(u), want);
    }
    if (u && core_ctz(u) != __builtin_ctz(u) && failures++ < 20) {
        printf("FAIL ctz(%08x) = %d, want %d\n", u, core_ctz(u), __builtin_ctz(u));
    }
    float f;
    memcpy(&f, &u, sizeof f);
    const double a = core_ext(f), b = (double)f;
    if (memcmp(&a, &b, sizeof a) && failures++ < 20) {
        printf("FAIL extend(%08x)\n", u);
    }
    /* u as a double's high word; the low words put the dropped 29 bits at, just under and just over half
     * (a tie when the rest is 0), at 0, at their least and most, and spill into the kept bits. */
    static const uint32_t lows[] = {0, 1, 0x0fffffffu, 0x10000000u, 0x10000001u, 0x1fffffffu, 0x20000000u,
                                    0x30000000u, 0xe0000000u, 0xffffffffu};
    for (size_t i = 0; i < sizeof lows / sizeof lows[0]; i++) {
        const uint32_t w[2] = {lows[i], u};
        double d;
        memcpy(&d, w, sizeof d);
        float y = (float)d;
        if (y != y) { /* libgcc on RISC-V gives the canonical NaN, not the host's sign and payload */
            const uint32_t canonical = 0x7fc00000u;
            memcpy(&y, &canonical, sizeof y);
        }
        const float x = core_trunc(d);
        if (memcmp(&x, &y, sizeof x) && failures++ < 20) {
            uint32_t xb, yb;
            memcpy(&xb, &x, 4);
            memcpy(&yb, &y, 4);
            printf("FAIL trunc(%08x %08x) = %08x, want %08x\n", u, lows[i], xb, yb);
        }
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

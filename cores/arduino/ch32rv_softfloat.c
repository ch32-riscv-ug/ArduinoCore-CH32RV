/* Soft-float calls GCC makes itself, smaller than libgcc's.
 *
 * Subtraction: libgcc's (__subsf3, __subdf3) is a second copy of its addition,
 * 1.2 KB in float and 2.2 KB in double on RV32: here it is the addition with
 * the sign of b turned over, which IEEE 754 makes the same result (checked bit for bit against
 * libgcc's on V003 and X035, NaN, infinity, subnormals and cancellation among
 * 400,000 pairs). A sketch that both adds and subtracts is 1.2 KB (float) or
 * 2.2 KB (double) smaller; one that only subtracts takes libgcc's addition in
 * place of its subtraction: 100 B smaller in float, 50-120 B larger in double.
 *
 * float to double (__extendsfdf2, for Serial.print(float) and any float
 * passed as a double): the bits moved over; a NaN keeps its sign and payload
 * and is made quiet, as libgcc's does (the same as every float on the host,
 * all 2^32 of them).
 *
 * Leading zeros (__clzsi2), for libgcc's own soft-float: without libgcc's
 * 256-byte table, the same counts (all 2^32 inputs, and 32 for 0).
 *
 * GCC emits these calls when LTO is done; core.a is built with fat objects,
 * so the linker takes them from here before it reaches libgcc. Each one is
 * `used`: once a non-LTO object (libc's strtod, with __subdf3) brings this
 * object in, its definitions are LTO's, and one that nothing called before
 * LTO would be dropped and leave the later call undefined. Float only
 * without an FPU, double unless the FPU does double (V30x and others do
 * single only). */
#include <stdint.h>
#include <string.h>

#if !defined(__riscv_flen) || __riscv_flen < 64
/* libgcc's soft-float counts leading zeros with __clzsi2 and its 256-byte
 * table; this one, in the object that subtraction and float-to-double bring
 * in, is defined before the linker reaches libgcc, so the table stays out. */
__attribute__((used)) int __clzsi2(unsigned x)
{
    int n = 0;
    if (!(x >> 16)) {
        n += 16;
        x <<= 16;
    }
    if (!(x >> 24)) {
        n += 8;
        x <<= 8;
    }
    if (!(x >> 28)) {
        n += 4;
        x <<= 4;
    }
    if (!(x >> 30)) {
        n += 2;
        x <<= 2;
    }
    return n + !(x >> 31) + !x; /* 32 for 0, as libgcc's */
}
#endif

#if !defined(__riscv_flen)
float __addsf3(float a, float b);

__attribute__((used)) float __subsf3(float a, float b)
{
    uint32_t u;
    memcpy(&u, &b, sizeof u);
    u ^= 0x80000000u;
    memcpy(&b, &u, sizeof u);
    return __addsf3(a, b);
}
#endif

#if !defined(__riscv_flen) || __riscv_flen < 64
double __adddf3(double a, double b);

__attribute__((used)) double __extendsfdf2(float a)
{
    uint32_t u;
    memcpy(&u, &a, sizeof u);
    uint32_t e = u >> 23 & 0xffu, m = u & 0x7fffffu, w[2];
    w[1] = u & 0x80000000u;
    w[0] = 0;
    if (e == 0xffu) { /* infinity, or NaN made quiet: sign and payload kept, as libgcc does */
        w[1] |= 0x7ff00000u | (m ? 0x80000u : 0u) | m >> 3;
        w[0] = m << 29;
    } else if (e || m) {
        if (!e) { /* subnormal: normalised */
            e = 1;
            while (!(m & 0x800000u)) {
                m <<= 1;
                e--;
            }
            m &= 0x7fffffu;
        }
        w[1] |= (e + 896u) << 20 | m >> 3;
        w[0] = m << 29;
    }
    double d;
    memcpy(&d, w, sizeof d);
    return d;
}

__attribute__((used)) double __subdf3(double a, double b)
{
    uint32_t w[2];
    memcpy(w, &b, sizeof w);
    w[1] ^= 0x80000000u; /* little-endian: the sign is in the high word */
    memcpy(&b, w, sizeof w);
    return __adddf3(a, b);
}
#endif

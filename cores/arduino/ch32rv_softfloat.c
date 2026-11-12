/* Calls the compiler makes itself (soft-float and bit counting), smaller
 * than the runtime library's (compiler-rt; sizes on rv32ec at -Oz, against
 * this file's): __truncdfsf2 332 B (190), __extendsfdf2 210 B (98),
 * __clzsi2 104 B (78), __ctzsi2 82 B (72). They were first written against
 * libgcc, whose tables and second copies made the difference larger.
 *
 * float to double (__extendsfdf2, for Serial.print(float) and any float
 * passed as a double): the bits moved over; a NaN keeps its sign and payload
 * and is made quiet (the same as every float on the host, all 2^32 of them).
 *
 * Leading zeros (__clzsi2), for the runtime's own soft-float: the same counts
 * (all 2^32 inputs, and 32 for 0).
 *
 * Trailing zeros (__ctzsi2, for __builtin_ctz: the EXTI dispatch): a few
 * compares. Not built where Zbb makes it one instruction.
 *
 * double to float (__truncdfsf2, for printf's %f under Float, where the
 * argument comes as a double, and any double stored in a float): rounded to
 * nearest, ties to even, subnormals and overflow to infinity as IEEE 754 has
 * them, so every non-NaN result is any IEEE 754 implementation's bit for bit;
 * a NaN of any sign and payload becomes the canonical 0x7fc00000, as an FPU's
 * fcvt.s.d does on RISC-V (and libgcc's did: its code run in an emulator
 * against this one, 2026-11-12). It always rounds to nearest and sets no
 * flags (nothing in the core or Arduino changes frm).
 *
 * The code generator emits these calls after LTO. lld extracts an archive
 * member of LTO bitcode that defines a runtime library call before LTO
 * (handleLibcall in lld/ELF/Driver.cpp), so the linker takes them from here
 * before it reaches compiler-rt. Each one is `used`: once a non-LTO object
 * (picolibc's strtod) brings this object in, its definitions are LTO's, and
 * one that nothing called before LTO would be dropped and leave the later
 * call undefined. Float only without an FPU, double unless the FPU does
 * double (V30x and others do single only). Subtraction is the runtime's: in
 * compiler-rt it is already the addition with b's sign turned over. */
#include <stdint.h>
#include <string.h>

#if !defined(__riscv_flen) || __riscv_flen < 64
/* The runtime's soft-float counts leading zeros with __clzsi2; this one, in
 * the object that float-to-double brings in, is defined before the linker
 * reaches compiler-rt. */
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
    return n + !(x >> 31) + !x; /* 32 for 0 */
}
#endif

#if !defined(__riscv_zbb)
/* Undefined for 0, as __builtin_ctz is. */
__attribute__((used)) int __ctzsi2(unsigned x)
{
    int n = 0;
    if (!(x & 0xffffu)) {
        n += 16;
        x >>= 16;
    }
    if (!(x & 0xffu)) {
        n += 8;
        x >>= 8;
    }
    if (!(x & 0xfu)) {
        n += 4;
        x >>= 4;
    }
    if (!(x & 3u)) {
        n += 2;
        x >>= 2;
    }
    return n + !(x & 1u);
}
#endif

#if !defined(__riscv_flen) || __riscv_flen < 64

__attribute__((used)) double __extendsfdf2(float a)
{
    uint32_t u;
    memcpy(&u, &a, sizeof u);
    uint32_t e = u >> 23 & 0xffu, m = u & 0x7fffffu, w[2];
    w[1] = u & 0x80000000u;
    w[0] = 0;
    if (e == 0xffu) { /* infinity, or NaN made quiet: sign and payload kept */
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

__attribute__((used)) float __truncdfsf2(double a)
{
    uint32_t w[2];
    memcpy(w, &a, sizeof w);
    const uint32_t s = w[1] & 0x80000000u;
    const int de = (int)(w[1] >> 20 & 0x7ffu);
    uint32_t m = (w[1] & 0xfffffu) << 3 | w[0] >> 29; /* the 23 bits a float keeps */
    uint32_t rest = w[0] & 0x1fffffffu;              /* the 29 below them */
    uint32_t u;
    if (de == 0x7ff) { /* infinity, or NaN: the canonical one, as RISC-V's fcvt.s.d */
        u = (m | rest) ? 0x7fc00000u : s | 0x7f800000u;
    } else if (de == 0) { /* zero or a double subnormal: far below a float's */
        u = s;
    } else {
        int e = de - 896; /* the float's biased exponent */
        m |= 0x800000u;
        if (e <= 0) { /* a float subnormal, or 0: shifted down, the rest sticky */
            const int sh = 1 - e;
            if (sh > 25) { /* under half the least subnormal: 0 */
                m = 0;
                rest = 0;
            } else {
                rest = (rest != 0) | (m << (32 - sh)); /* what falls off m, at the top */
                m >>= sh;
            }
            e = 0;
        } else {
            rest <<= 3; /* the guard bit at the top, as above */
        }
        if (rest > 0x80000000u || (rest == 0x80000000u && (m & 1u))) {
            m++; /* may carry into the exponent: a subnormal becomes normal, 2^24 the next binade */
        }
        if (e >= 0xff || (e == 0xfe && m >> 24)) {
            u = s | 0x7f800000u;
        } else {
            u = s | (((uint32_t)e << 23) + m - (e ? 0x800000u : 0u));
        }
    }
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}
#endif

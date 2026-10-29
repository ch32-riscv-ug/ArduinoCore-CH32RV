/* See ch32rv_format.h. */
#include "ch32rv_format.h"

#include <stdbool.h>
#include <stdint.h>

#if CH32RV_FP_FLOAT
typedef float fp_t;
#define FP(x) x##f
#else
typedef double fp_t;
#define FP(x) x
#endif

#define FMT_ALT 0x100u /* '#': 0x / 0X before hex, a leading 0 for octal */

struct out {
    ch32rv_sink_fn sink;
    void *ctx;
    size_t n;
};

static void put(struct out *o, const char *s, size_t n)
{
    if (n) {
        o->sink(o->ctx, s, n);
        o->n += n;
    }
}

static void fill(struct out *o, char c, int k)
{
    while (k-- > 0) {
        put(o, &c, 1);
    }
}

/* Spaces, the prefix (sign, 0x), zeros, the body, padded out to width. */
static void field(struct out *o, const char *pre, size_t npre, int zeros, const char *s, size_t n, int width,
                  unsigned flags)
{
    const int pad = width - (int)(npre + n) - zeros;
    if (!(flags & (CH32RV_FMT_LEFT | CH32RV_FMT_ZERO))) {
        fill(o, ' ', pad);
    }
    put(o, pre, npre);
    if ((flags & (CH32RV_FMT_LEFT | CH32RV_FMT_ZERO)) == CH32RV_FMT_ZERO) {
        fill(o, '0', pad);
    }
    fill(o, '0', zeros);
    put(o, s, n);
    if (flags & CH32RV_FMT_LEFT) {
        fill(o, ' ', pad);
    }
}

/* Float builds without an FPU do fixed()'s float steps on the bits (below). */
#ifndef CH32RV_FIXED_BITS
#if CH32RV_FP_FLOAT && !defined(__riscv_flen)
#define CH32RV_FIXED_BITS 1
#else
#define CH32RV_FIXED_BITS 0
#endif
#endif

#if CH32RV_FIXED_BITS
/* m rounded to 24 significant bits, to nearest, ties to even: what a float
 * operation does to its exact result. */
static uint32_t round24(uint32_t m)
{
    uint32_t unit = 1;
    for (uint32_t t = m >> 24; t; t >>= 1) {
        unit <<= 1;
    }
    const uint32_t low = m & (unit - 1);
    m -= low;
    if (low > unit / 2 || (low && low == unit / 2 && (m & unit))) {
        m += unit;
    }
    return m;
}

/* The float steps of the other fixed() - (float)number, adding the rounding,
 * splitting off the integer part, rem * 10 for each digit - carried out on
 * the bits with the same rounding: the same output, and %f brings no
 * soft-float in. A value is m * 2^e. */
static void fixed(struct out *o, double number, int digits, int width, unsigned flags)
{
    union {
        double d;
        uint32_t w[2]; /* little-endian: w[1] holds the sign and exponent */
    } b = {number};
    const uint32_t hi = b.w[1], lo = b.w[0];
    const int be = (int)(hi >> 20 & 0x7ff);
    uint32_t m = (hi & 0xfffff) << 3 | lo >> 29;
    int e = be - 1046;
    char sign = 0;
    const char *word = 0;
    if (be == 0x7ff) {
        word = (m | lo) ? "nan" : "inf";
    } else if (be < 873 || (be == 873 && !(m | lo))) {
        m = 0; /* 2^-150 or less: a float 0 */
    } else {
        /* Below 2^-126 a float has fewer bits; the rounding added next is
         * far larger, so they never show. */
        m |= 0x800000;
        const uint32_t low = lo & 0x1fffffff;
        if (low > 0x10000000 || (low == 0x10000000 && (m & 1))) {
            m++;
        }
        if (e > 104 || (e == 104 && m >> 24)) {
            word = "inf";
        } else if (e > 8 || (e == 8 && m >> 24)) {
            word = "ovf"; /* 2^32 or more */
        }
    }
    if (!(word && word[0] == 'n')) {
        if ((flags & CH32RV_FMT_PRINTFLOAT) ? hi >> 31 && m : hi >> 31) {
            sign = '-';
        } else if (flags & CH32RV_FMT_PLUS) {
            sign = '+';
        } else if (flags & CH32RV_FMT_SPACE) {
            sign = ' ';
        }
    }
#else
static void fixed(struct out *o, double number, int digits, int width, unsigned flags)
{
    fp_t v = (fp_t)number;
    char sign = 0;
    const char *word = 0;
    if (__builtin_isnan(v)) {
        word = "nan";
    } else {
        if ((flags & CH32RV_FMT_PRINTFLOAT) ? v < 0 : __builtin_signbit(v)) {
            sign = '-';
            v = -v;
        } else if (flags & CH32RV_FMT_PLUS) {
            sign = '+';
        } else if (flags & CH32RV_FMT_SPACE) {
            sign = ' ';
        }
        if (__builtin_isinf(v)) {
            word = "inf";
        } else if (v > FP(4294967040.0)) {
            word = "ovf";
        }
    }
#endif
    if (word) {
        if (flags & CH32RV_FMT_PRINTFLOAT) {
            sign = 0;
        }
        field(o, &sign, sign != 0, 0, word, 3, width, flags & ~CH32RV_FMT_ZERO);
        return;
    }
#if CH32RV_FIXED_BITS
    /* The bits of 0.5f, 0.05f, ... 5e-10f, added as a float addition does
     * it: the one with the lower exponent shifted down under the other, what
     * falls off kept as a sticky bit, the sum rounded. */
    static const uint32_t rounding[10] = {0x3f000000, 0x3d4ccccd, 0x3ba3d70a, 0x3a03126f, 0x3851b717,
                                          0x36a7c5ac, 0x350637bd, 0x3356bf95, 0x31abcc77, 0x3009705f};
    const uint32_t r = rounding[digits < 9 ? digits : 9];
    uint32_t big = (r & 0x7fffff) | 0x800000, small = m << 6;
    int eb = (int)(r >> 23) - 150, d = eb - e;
    if (d < 0) {
        small = big << 6;
        big = m;
        eb = e;
        d = -d;
    }
    small = d == 0 ? small : d < 31 ? small >> d | (small << (32 - d) != 0) : small != 0;
    m = round24((big << 6) + small);
    e = eb - 6;
    while (e < 0 && !(m & 1)) {
        m >>= 1;
        e++;
    }
    /* At least 2^-32 now, so its last bit is no lower than 2^-55. */
    int s = e < 0 ? -e : 0;
    uint32_t ip = e < 0 ? (s < 32 ? m >> s : 0) : m << e;
    uint32_t rem = m & (s < 32 ? (1u << s) - 1 : ~0u);
#elif CH32RV_FP_FLOAT
    /* A table, not divisions: __divsf3 would come in for this alone. */
    static const float rounding[10] = {0.5f, 0.05f, 0.005f, 5e-4f, 5e-5f, 5e-6f, 5e-7f, 5e-8f, 5e-9f, 5e-10f};
    v += rounding[digits < 9 ? digits : 9];
    uint32_t ip = (uint32_t)v;
    fp_t rem = v - (fp_t)ip;
#else
    /* Print::printFloat's own steps, so its output stays what it was. */
    double rounding = 0.5;
    for (int i = 0; i < digits; i++) {
        rounding /= 10.0;
    }
    v += rounding;
    uint32_t ip = (uint32_t)v;
    fp_t rem = v - (fp_t)ip;
#endif
    char ib[10];
    char *p = ib + sizeof ib;
    do {
        *--p = (char)('0' + ip % 10u);
        ip /= 10u;
    } while (ip);
    const size_t ni = (size_t)(ib + sizeof ib - p);
    const int pad = width - (int)ni - (digits > 0 ? digits + 1 : 0) - (sign != 0);
    if (!(flags & (CH32RV_FMT_LEFT | CH32RV_FMT_ZERO))) {
        fill(o, ' ', pad);
    }
    put(o, &sign, sign != 0);
    if ((flags & (CH32RV_FMT_LEFT | CH32RV_FMT_ZERO)) == CH32RV_FMT_ZERO) {
        fill(o, '0', pad);
    }
    put(o, p, ni);
    if (digits > 0) {
        put(o, ".", 1);
    }
    while (digits-- > 0) {
#if CH32RV_FIXED_BITS
        rem = round24(rem * 10);
        const char c = (char)('0' + (s < 32 ? rem >> s : 0));
        rem &= s < 32 ? (1u << s) - 1 : ~0u;
        put(o, &c, 1);
        while (rem && !(rem & 1)) { /* keeps it within 24 bits */
            rem >>= 1;
            s--;
        }
#else
        rem *= FP(10.0);
        const unsigned d = (unsigned)rem;
        const char c = (char)('0' + d);
        put(o, &c, 1);
        rem -= (fp_t)d;
#endif
    }
    if (flags & CH32RV_FMT_LEFT) {
        fill(o, ' ', pad);
    }
}

size_t ch32rv_format_fixed(ch32rv_sink_fn sink, void *ctx, double v, int digits, int width, unsigned flags)
{
    struct out o = {sink, ctx, 0};
    fixed(&o, v, digits, width, flags);
    return o.n;
}

static int number(const char **f)
{
    int n = 0;
    while (**f >= '0' && **f <= '9') {
        n = n * 10 + (*(*f)++ - '0');
    }
    return n;
}

size_t ch32rv_vformat(ch32rv_sink_fn sink, void *ctx, const char *f, va_list ap)
{
    struct out o = {sink, ctx, 0};
    while (*f) {
        const char *lit = f;
        while (*f && *f != '%') {
            f++;
        }
        put(&o, lit, (size_t)(f - lit));
        if (!*f) {
            break;
        }
        const char *spec = f++;
        unsigned flags = 0;
        for (;; f++) {
            if (*f == '-') {
                flags |= CH32RV_FMT_LEFT;
            } else if (*f == '0') {
                flags |= CH32RV_FMT_ZERO;
            } else if (*f == '+') {
                flags |= CH32RV_FMT_PLUS;
            } else if (*f == ' ') {
                flags |= CH32RV_FMT_SPACE;
            } else if (*f == '#') {
                flags |= FMT_ALT;
            } else {
                break;
            }
        }
        int width;
        if (*f == '*') {
            f++;
            width = va_arg(ap, int);
            if (width < 0) {
                flags |= CH32RV_FMT_LEFT;
                width = -width;
            }
        } else {
            width = number(&f);
        }
        int prec = -1;
        if (*f == '.') {
            f++;
            if (*f == '*') {
                f++;
                prec = va_arg(ap, int);
                if (prec < 0) {
                    prec = -1;
                }
            } else {
                prec = number(&f);
            }
        }
        int h = 0, l = 0;
        while (*f == 'h') {
            h++, f++;
        }
        while (*f == 'l') {
            l++, f++;
        }
        if (*f == 'z' || *f == 't') {
            f++;
        } else if (*f == 'j') {
            f++, l = 2;
        }
        const char c = *f;
        if (!c) {
            put(&o, spec, (size_t)(f - spec));
            break;
        }
        f++;
        bool raw = false;
        switch (c) {
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
        case 'p': {
            if (l >= 2) { /* 64 bits: taken off the list so what follows lines up, then shown as written */
                (void)va_arg(ap, long long);
                raw = true;
                break;
            }
            char pre[2];
            size_t npre = 0;
            unsigned long u;
            unsigned base = 10;
            if (c == 'd' || c == 'i') {
                long v = l ? va_arg(ap, long) : va_arg(ap, int);
                if (h == 1) {
                    v = (short)v;
                } else if (h >= 2) {
                    v = (signed char)v;
                }
                u = v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;
                if (v < 0) {
                    pre[npre++] = '-';
                } else if (flags & CH32RV_FMT_PLUS) {
                    pre[npre++] = '+';
                } else if (flags & CH32RV_FMT_SPACE) {
                    pre[npre++] = ' ';
                }
            } else {
                if (c == 'p') {
                    u = (unsigned long)(uintptr_t)va_arg(ap, void *);
                    flags |= FMT_ALT;
                } else {
                    u = l ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                    if (h == 1) {
                        u = (unsigned short)u;
                    } else if (h >= 2) {
                        u = (unsigned char)u;
                    }
                }
                base = c == 'o' ? 8u : (c == 'u' ? 10u : 16u);
                if ((flags & FMT_ALT) && base == 16u && (u || c == 'p')) {
                    pre[npre++] = '0';
                    pre[npre++] = c == 'X' ? 'X' : 'x';
                }
            }
            const char *digits = c == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char buf[sizeof(unsigned long) * 8 / 3 + 2];
            char *p = buf + sizeof buf;
            if (u || prec != 0) {
                do {
                    *--p = digits[u % base];
                    u /= base;
                } while (u);
            }
            const int n = (int)(buf + sizeof buf - p);
            int zeros = prec > n ? prec - n : 0;
            if ((flags & FMT_ALT) && base == 8u && !zeros && (n == 0 || *p != '0')) {
                zeros = 1;
            }
            field(&o, pre, npre, zeros, p, (size_t)n, width, prec >= 0 ? flags & ~CH32RV_FMT_ZERO : flags);
            break;
        }
        case 'c': {
            const char ch = (char)va_arg(ap, int);
            field(&o, 0, 0, 0, &ch, 1, width, flags & ~CH32RV_FMT_ZERO);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            size_t n = 0;
            while (s[n] && (prec < 0 || n < (size_t)prec)) {
                n++;
            }
            field(&o, 0, 0, 0, s, n, width, flags & ~CH32RV_FMT_ZERO);
            break;
        }
        case '%':
            put(&o, "%", 1);
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            /* Always taken off the list, so what follows lines up. */
            const double v = va_arg(ap, double);
#if CH32RV_PRINTF_FLOAT
            if (c == 'f' || c == 'F') {
                fixed(&o, v, prec < 0 ? 6 : prec, width, flags);
                break;
            }
#else
            (void)v;
#endif
            raw = true;
            break;
        }
        case 'n':
            (void)va_arg(ap, void *);
            raw = true;
            break;
        default:
            raw = true;
            break;
        }
        if (raw) {
            put(&o, spec, (size_t)(f - spec));
        }
    }
    return o.n;
}

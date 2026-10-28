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
    if (word) {
        if (flags & CH32RV_FMT_PRINTFLOAT) {
            sign = 0;
        }
        field(o, &sign, sign != 0, 0, word, 3, width, flags & ~CH32RV_FMT_ZERO);
        return;
    }
#if CH32RV_FP_FLOAT
    /* A table, not divisions: __divsf3 would come in for this alone. */
    static const float rounding[10] = {0.5f, 0.05f, 0.005f, 5e-4f, 5e-5f, 5e-6f, 5e-7f, 5e-8f, 5e-9f, 5e-10f};
    v += rounding[digits < 9 ? digits : 9];
#else
    /* Print::printFloat's own steps, so its output stays what it was. */
    double rounding = 0.5;
    for (int i = 0; i < digits; i++) {
        rounding /= 10.0;
    }
    v += rounding;
#endif
    uint32_t ip = (uint32_t)v;
    fp_t rem = v - (fp_t)ip;
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
        rem *= FP(10.0);
        const unsigned d = (unsigned)rem;
        const char c = (char)('0' + d);
        put(o, &c, 1);
        rem -= (fp_t)d;
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

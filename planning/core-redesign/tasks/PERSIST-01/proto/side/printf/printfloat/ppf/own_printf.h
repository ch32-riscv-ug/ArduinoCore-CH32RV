// Own small printf into a Print, for measuring. OWN_LEVEL 0: %[-0][width][l](d i u x X c s %);
// 1: + precision, '*' width/precision, '+' and ' ' flags; 2: + %f (via double); 3: + %f computed in float.
#pragma once
#include <stdarg.h>
#include <string.h>
#ifndef OWN_LEVEL
#define OWN_LEVEL 1
#endif

struct OwnOut {
  Print &o;
  size_t n;
  void put(char c) { n += o.write((uint8_t)c); }
  void rep(char c, int k) { while (k-- > 0) put(c); }
};

// One field: sign/prefix, zeros, digits, with width padding.
static void ownField(OwnOut &w, const char *s, int len, char sign, int zeros, int width, bool left, bool zeroPad) {
  int pad = width - len - zeros - (sign != 0);
  if (!left && !zeroPad) w.rep(' ', pad);
  if (sign) w.put(sign);
  if (!left && zeroPad) w.rep('0', pad);
  w.rep('0', zeros);
  w.n += w.o.write((const uint8_t *)s, len);
  if (left) w.rep(' ', pad);
}

static size_t ownVprintf(Print &o, const char *f, va_list ap) {
  OwnOut w{o, 0};
  for (; *f; f++) {
    if (*f != '%') { w.put(*f); continue; }
    bool left = false, zero = false;
    char plus = 0;
    for (f++;; f++) {
      if (*f == '-') left = true;
      else if (*f == '0') zero = true;
#if OWN_LEVEL >= 1
      else if (*f == '+') plus = '+';
      else if (*f == ' ') { if (!plus) plus = ' '; }
#endif
      else break;
    }
    int width = 0, prec = -1;
#if OWN_LEVEL >= 1
    if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = true; width = -width; } f++; }
    else
#endif
    while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
#if OWN_LEVEL >= 1
    if (*f == '.') {
      f++;
      prec = 0;
      if (*f == '*') { prec = va_arg(ap, int); f++; }
      else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
    }
#endif
    if (*f == 'l') f++;
    char buf[24], *p = buf + sizeof buf, sign = 0;
    switch (*f) {
      case 'd': case 'i': case 'u': case 'x': case 'X': {
        unsigned long u;
        if (*f == 'd' || *f == 'i') {
          const long v = va_arg(ap, long);
          u = v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;
          sign = v < 0 ? '-' : plus;
        } else {
          u = va_arg(ap, unsigned long);
        }
        const unsigned base = (*f == 'x' || *f == 'X') ? 16 : 10;
        const char *digits = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
        if (u || prec != 0) do { *--p = digits[u % base]; u /= base; } while (u);
        const int len = buf + sizeof buf - p;
        const int zeros = prec > len ? prec - len : 0;
        ownField(w, p, len, sign, zeros, width, left, zero && prec < 0);
        break;
      }
#if OWN_LEVEL >= 2
#if OWN_LEVEL == 2
      case 'f': {
        // Print::printFloat's way and results (0.5 added at the last digit, "ovf" past 4294967040), with the
        // double operations a sketch that prints doubles has already linked.
        double v = va_arg(ap, double);
        if (prec < 0) prec = 6;
        if (prec > 9) prec = 9;
        if (__builtin_isnan(v)) { ownField(w, "nan", 3, 0, 0, width, left, false); break; }
        if (__builtin_signbit(v)) { sign = '-'; v = -v; } else sign = plus;
        if (__builtin_isinf(v)) { ownField(w, "inf", 3, sign, 0, width, left, false); break; }
        if (v > 4294967040.0) { ownField(w, "ovf", 3, sign, 0, width, left, false); break; }
        // 0.5 at the last printed digit, from a table: a division would link __divdf3 (1.8 KB on rv32ec).
        static const double rounding[10] = {0.5, 0.05, 0.005, 5e-4, 5e-5, 5e-6, 5e-7, 5e-8, 5e-9, 5e-10};
        v += rounding[prec];
        uint32_t ip = (uint32_t)v;
        double rem = v - (double)ip;
        char *q = buf + 12;  // fraction digits after the integer part's room
        char *e = q;
        if (prec) *e++ = '.';
        for (int i = 0; i < prec; i++) {
          rem *= 10.0;
          const unsigned d = (unsigned)rem;
          *e++ = '0' + d;
          rem -= d;
        }
        p = q;
        do { *--p = '0' + ip % 10; ip /= 10; } while (ip);
        ownField(w, p, e - p, sign, 0, width, left, zero);
        break;
      }
#else
      case 'f': {
        // Print::printFloat's way and results (0.5 added at the last digit, "ovf" past 4294967040), with the
        // double operations a sketch that prints doubles has already linked.
        float v = (float)va_arg(ap, double);  // varargs carry a double; one conversion, then float
        if (prec < 0) prec = 6;
        if (prec > 9) prec = 9;
        if (__builtin_isnan(v)) { ownField(w, "nan", 3, 0, 0, width, left, false); break; }
        if (__builtin_signbit(v)) { sign = '-'; v = -v; } else sign = plus;
        if (__builtin_isinf(v)) { ownField(w, "inf", 3, sign, 0, width, left, false); break; }
        if (v > 4294967040.0f) { ownField(w, "ovf", 3, sign, 0, width, left, false); break; }
        // 0.5 at the last printed digit, from a table: a division would link __divdf3 (1.8 KB on rv32ec).
        static const float rounding[10] = {0.5f, 0.05f, 0.005f, 5e-4f, 5e-5f, 5e-6f, 5e-7f, 5e-8f, 5e-9f, 5e-10f};
        v += rounding[prec];
        uint32_t ip = (uint32_t)v;
        float rem = v - (float)ip;
        char *q = buf + 12;  // fraction digits after the integer part's room
        char *e = q;
        if (prec) *e++ = '.';
        for (int i = 0; i < prec; i++) {
          rem *= 10.0f;
          const unsigned d = (unsigned)rem;
          *e++ = '0' + d;
          rem -= d;
        }
        p = q;
        do { *--p = '0' + ip % 10; ip /= 10; } while (ip);
        ownField(w, p, e - p, sign, 0, width, left, zero);
        break;
      }
#endif
#endif
      case 'c': buf[0] = (char)va_arg(ap, int); ownField(w, buf, 1, 0, 0, width, left, false); break;
      case 's': {
        const char *s = va_arg(ap, const char *);
        if (!s) s = "(null)";
        int len = 0;
        while (s[len] && (prec < 0 || len < prec)) len++;
        ownField(w, s, len, 0, 0, width, left, false);
        break;
      }
      case '%': w.put('%'); break;
      default: return w.n;
    }
  }
  return w.n;
}

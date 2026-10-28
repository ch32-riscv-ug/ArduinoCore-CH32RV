// Where printf on a Print should go: one candidate per build (-DCAND=n), the same cases, stack depth and time.
//   0 the core's Serial.printf          1 small[32] + heap        2 heap only (length first)
//   3 funopen stream (newlib vfprintf)  4 own small formatter     5 eyalroz/printf, integers
//   6 eyalroz/printf with floats        -1 none (size base)
// 'g': every case prints its line, then "C <case> <stack depth>"; then "T <us for 20 calls into a null Print>".
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef CAND
#define CAND 0
#endif
#if CAND == 5 || CAND == 6
#include "printf.h"
#endif

#if CAND == 0
#include <ch32rv_printf.h>
static size_t vout(Print &o, const char *f, va_list ap) { return ch32rv_vprintf(o, f, ap); }
#elif CAND == 1 || CAND == 2
static size_t vout(Print &o, const char *f, va_list ap) {
#if CAND == 1
  char small[32];
  va_list c;
  va_copy(c, ap);
  const int n = vsnprintf(small, sizeof small, f, c);
  va_end(c);
  if (n < 0) return 0;
  if ((size_t)n < sizeof small) return o.write((const uint8_t *)small, n);
#else
  va_list c;
  va_copy(c, ap);
  const int n = vsnprintf(nullptr, 0, f, c);
  va_end(c);
  if (n < 0) return 0;
#endif
  char *b = (char *)malloc(n + 1);
  if (!b) return 0;
  vsnprintf(b, n + 1, f, ap);
  const size_t w = o.write((const uint8_t *)b, n);
  free(b);
  return w;
}
#elif CAND == 3
static int cookieWrite(void *c, const char *b, int n) { return (int)((Print *)c)->write((const uint8_t *)b, n); }
static size_t vout(Print &o, const char *f, va_list ap) {
  FILE *fp = funopen(&o, nullptr, cookieWrite, nullptr, nullptr);
  if (!fp) return 0;
  setvbuf(fp, nullptr, _IONBF, 0);
  const int n = vfprintf(fp, f, ap);
  fclose(fp);
  return n < 0 ? 0 : n;
}
#elif CAND == 4
// %[-0][width][l](d i u x X c s %): enough for logs; no precision, no floats.
static size_t vout(Print &o, const char *f, va_list ap) {
  size_t n = 0;
  for (; *f; f++) {
    if (*f != '%') { n += o.write(*f); continue; }
    bool left = false, zero = false;
    for (f++;; f++) {
      if (*f == '-') left = true;
      else if (*f == '0') zero = true;
      else break;
    }
    int width = 0;
    while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
    if (*f == 'l') f++;
    char buf[12], *p = buf + sizeof buf, sign = 0;
    const char *s;
    int len;
    switch (*f) {
      case 'd': case 'i': {
        long v = va_arg(ap, long);
        unsigned long u = v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;
        if (v < 0) sign = '-';
        do { *--p = '0' + u % 10; u /= 10; } while (u);
        s = p; len = buf + sizeof buf - p;
        break;
      }
      case 'u': case 'x': case 'X': {
        unsigned long u = va_arg(ap, unsigned long);
        const unsigned base = *f == 'u' ? 10 : 16;
        const char *digits = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
        do { *--p = digits[u % base]; u /= base; } while (u);
        s = p; len = buf + sizeof buf - p;
        break;
      }
      case 'c': buf[0] = (char)va_arg(ap, int); s = buf; len = 1; zero = false; break;
      case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; len = strlen(s); zero = false; break;
      case '%': s = "%"; len = 1; break;
      default: return n;
    }
    int pad = width - len - (sign != 0);
    if (sign && zero) { n += o.write(sign); sign = 0; }
    if (!left) while (pad-- > 0) n += o.write(zero ? '0' : ' ');
    if (sign) n += o.write(sign);
    n += o.write((const uint8_t *)s, len);
    if (left) while (pad-- > 0) n += o.write(' ');
  }
  return n;
}
#elif CAND == 5 || CAND == 6
static void outChar(char c, void *p) { ((Print *)p)->write(c); }
static size_t vout(Print &o, const char *f, va_list ap) {
  const int n = vfctprintf(outChar, &o, f, ap);
  return n < 0 ? 0 : n;
}
#endif

#if CAND >= 0
static size_t outf(Print &o, const char *f, ...) __attribute__((format(printf, 2, 3)));
static size_t outf(Print &o, const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  const size_t n = vout(o, f, ap);
  va_end(ap);
  return n;
}
#define OUT(o, ...) outf(o, __VA_ARGS__)
#endif

#ifndef SIZE_ONLY
extern "C" char _eusrstack[], _heap_end[];
static const char longLine[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ab";
volatile long vneg = -42;
struct NullPrint : Print {
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t *, size_t n) override { return n; }
} nullOut;

static void runCase(Print &o, int i) {
  switch (i) {
    case 0: OUT(o, "%d|%i|%u\n", (int)vneg, 7, 3000000000u); break;
    case 1: OUT(o, "%5d|%-5d|%05d\n", 42, 42, (int)vneg); break;
    case 2: OUT(o, "%x|%X|%08lx\n", 0xbeefu, 0xbeefu, 0x1234abcdul); break;
    case 3: OUT(o, "%ld|%lu\n", -123456789l, 4000000000ul); break;
    case 4: OUT(o, "%c|%s|%10s|%-4s|\n", 'A', "hey", "right", "l"); break;
    case 5: OUT(o, "%%|%.3s|\n", "abcdef"); break;
    case 6: OUT(o, "%s\n", longLine); break;
    case 7: OUT(o, "%.2f\n", 3.14159); break;
  }
}

static uint32_t depth(int i) {
  uint32_t sp;
  asm volatile("mv %0, sp" : "=r"(sp));
  uint32_t *lo = (uint32_t *)((uintptr_t)_heap_end - 256), *hi = (uint32_t *)(sp - 64);
  for (uint32_t *p = lo; p < hi; p++) *p = 0xA5A5A5A5u;
  if (i >= 0) runCase(Serial, i);
  Serial.flush();
  uint32_t *p = lo;
  while (p < hi && *p == 0xA5A5A5A5u) p++;
  return (uint32_t)((uintptr_t)_eusrstack - (uintptr_t)p);
}

void setup() {
  Serial.begin(115200);
  delay(300);
}
void loop() {
  if (Serial.read() != 'g') return;
  Serial.print("C none ");
  Serial.println(depth(-1));
  for (int i = 0; i < 8; i++) {
    const uint32_t d = depth(i);
    Serial.print("C ");
    Serial.print(i);
    Serial.print(' ');
    Serial.println(d);
  }
  const uint32_t t0 = micros();
  for (int k = 0; k < 20; k++) runCase(nullOut, 2);
  Serial.print("T ");
  Serial.println(micros() - t0);
  Serial.println("END");
}
#else
// Size only: one call site with integers and strings, the way a sketch logs.
volatile int v = 3;
void setup() {
  Serial.begin(115200);
#if CAND >= 0
  OUT(Serial, "v=%d s=%s x=%08lx\n", v, "ok", (unsigned long)v);
#endif
  Serial.println(v);
}
void loop() {}
#endif

// Own printf levels against newlib-nano. -DIMPL=0 newlib (the core's ch32rv_vprintf), 1 own (OWN_LEVEL picks the
// level); -DMODE: 0 test (cases, stack depth, time), 1 size: ints and strings, 2 size: one %f, 3 size base for a sketch
// that already prints doubles, 4 = 3 plus one %f through the formatter, 5 size base without any formatter,
// 6 size base for a sketch with float math, 7 = 6 plus one %f.
#include <stdarg.h>
#ifndef IMPL
#define IMPL 1
#endif
#ifndef MODE
#define MODE 0
#endif
#if IMPL == 0
#include <ch32rv_printf.h>
static size_t vout(Print &o, const char *f, va_list ap) { return ch32rv_vprintf(o, f, ap); }
#else
#include "own_printf.h"
static size_t vout(Print &o, const char *f, va_list ap) { return ownVprintf(o, f, ap); }
#endif
static size_t outf(Print &o, const char *f, ...) __attribute__((format(printf, 2, 3)));
static size_t outf(Print &o, const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  const size_t n = vout(o, f, ap);
  va_end(ap);
  return n;
}
#define OUT(o, ...) outf(o, __VA_ARGS__)

#if MODE == 0
volatile long vneg = -42;
#include "cases.h"
extern "C" char _eusrstack[], _heap_end[];
struct NullPrint : Print {
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t *, size_t n) override { return n; }
} nullOut;
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
  for (int i = 0; i < NCASES; i++) {
    const uint32_t d = depth(i);
    Serial.print("C ");
    Serial.print(i);
    Serial.print(' ');
    Serial.println(d);
  }
  uint32_t t0 = micros();
  for (int k = 0; k < 20; k++) runCase(nullOut, 2);
  Serial.print("T ");
  Serial.print(micros() - t0);
  t0 = micros();
  for (int k = 0; k < 20; k++) runCase(nullOut, 9);
  Serial.print(' ');
  Serial.println(micros() - t0);
  Serial.println("END");
}
#else
volatile int v = 3;
volatile double d = 2.5;
volatile float fa = 1.5f;
void setup() {
  Serial.begin(115200);
#if MODE == 1
  OUT(Serial, "v=%d s=%s x=%08lx\n", v, "ok", (unsigned long)v);
#elif MODE == 2
  OUT(Serial, "v=%d f=%.2f\n", v, d);
#elif MODE == 6 || MODE == 7
  Serial.println((int)(fa * v + fa / 3));  // float math, no float printing
#if MODE == 7
  OUT(Serial, "f=%.2f\n", (double)fa);
#endif
#elif MODE == 3 || MODE == 4
  Serial.println(d * v, 2);
#if MODE == 4
  OUT(Serial, "f=%.2f\n", d);
#endif
#endif
  Serial.println(v);
}
void loop() {}
#endif

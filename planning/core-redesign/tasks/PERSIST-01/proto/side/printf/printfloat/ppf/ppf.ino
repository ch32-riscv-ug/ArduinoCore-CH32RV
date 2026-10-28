#define OWN_LEVEL 3
#include "own_printf.h"
volatile float fa = 1.5f;
volatile int v = 3;
static size_t outf(Print &o, const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  const size_t n = ownVprintf(o, f, ap);
  va_end(ap);
  return n;
}
void setup() {
  Serial.begin(115200);
  Serial.println(fa * v, 3);
  outf(Serial, "f=%.2f\n", (double)(fa * v));
}
void loop() {}

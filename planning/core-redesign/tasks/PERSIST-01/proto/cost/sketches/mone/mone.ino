#include "minflash.h"
// One page, no header: save() erases and writes; load() copies. AVR-like contract (no power-loss guarantee).
__attribute__((section(".ch32rv_store"), aligned(64))) uint8_t area[64];
static void load(void *p, uint32_t n) { memcpy(p, area, n); }
static bool save(const void *p, uint32_t n) {
  if (!memcmp(area, p, n)) return true;
  uint32_t w[16]; memset(w, 0xFF, sizeof w); memcpy(w, p, n);
  return mf_write_page((uint32_t)area, w);
}
struct Cfg { uint32_t a; uint16_t b; uint8_t c, d; };
volatile uint8_t v;
void setup() { Cfg c; load(&c, sizeof c); c.a += v; save(&c, sizeof c); v = c.c; }
void loop() { v++; }

#include "minflash.h"
// Two one-page banks; the last 4 B of a bank: generation (16 bit) and CRC-16 over data + generation. The newest
// bank whose CRC holds (read twice) wins. A cut leaves the other bank intact.
__attribute__((section(".ch32rv_store"), aligned(64))) uint8_t area[128];
static uint16_t crc16(const uint8_t *p, uint32_t n) {
  uint16_t c = 0xFFFF;
  while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xA001u & (0u - (c & 1u))); }
  return c;
}
static int ok(const uint8_t *b) {  // 0: invalid, else 1 + generation
  const uint16_t c = crc16(b, 62);
  return (c == *(const uint16_t *)(b + 62) && c == crc16(b, 62)) ? 1 + *(const uint16_t *)(b + 60) : 0;
}
static int cur() { const int a = ok(area), b = ok(area + 64); return (a || b) ? ((int16_t)(a - b) >= 0 ? 0 : 1) : -1; }
static bool load(void *p, uint32_t n) { const int k = cur(); if (k < 0) return false; memcpy(p, area + 64 * k, n); return true; }
static bool save(const void *p, uint32_t n) {
  const int k = cur();
  if (k >= 0 && !memcmp(area + 64 * k, p, n)) return true;
  uint32_t w[16]; uint8_t *b = (uint8_t *)w; memset(w, 0xFF, sizeof w); memcpy(w, p, n);
  *(uint16_t *)(b + 60) = k < 0 ? 0 : *(const uint16_t *)(area + 64 * k + 60) + 1;
  *(uint16_t *)(b + 62) = crc16(b, 62);
  return mf_write_page((uint32_t)(area + 64 * (k == 0)), w);
}
struct Cfg { uint32_t a; uint16_t b; uint8_t c, d; };
volatile uint8_t v;
void setup() { Cfg c; if (!load(&c, sizeof c)) memset(&c, 0, sizeof c); c.a += v; save(&c, sizeof c); v = c.c; }
void loop() { v++; }

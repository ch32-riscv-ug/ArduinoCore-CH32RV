// PERSIST-01 cost ladder: stores on top of lf_write_page(). ST_KIND picks one; ST_* switches add one item each.
//   1  one page, load/save of a struct, no header
//   2  two banks, generation + check in the last 4 B (ST_CHECK: 1 = sum16, 2 = CRC-16)
//   (kinds 3 and 4 are written for V003's 64 B page only)
//   3  AVR EEPROM API over one page (read direct, write/update/put rewrite the page from a stack copy)
//   4  log (Teensy LC-like), AVR API: 16-bit records {offset, value} appended without erase over two pages; when
//      full, both pages are erased and the live bytes written back as records (not power-safe, as Teensy)
#pragma once
#include "lf.h"

typedef uint16_t __attribute__((may_alias)) st_u16;

#ifndef ST_NOLIBC
#define ST_NOLIBC 0  // byte loops instead of memcpy / memset / memcmp (newlib's are 298 / 92 / ~40 B on rv32imac)
#endif
#if ST_NOLIBC
#define ST_NODIST __attribute__((noinline, optimize("no-tree-loop-distribute-patterns")))
ST_NODIST static void *st_cpy(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; while (n--) *a++ = *b++; return d; }
ST_NODIST static void *st_set(void *d, int v, uint32_t n) { uint8_t *a = (uint8_t *)d; while (n--) *a++ = (uint8_t)v; return d; }
ST_NODIST static int st_cmp(const void *x, const void *y, uint32_t n) { const uint8_t *a = (const uint8_t *)x, *b = (const uint8_t *)y; while (n--) if (*a++ != *b++) return 1; return 0; }
#define memcpy st_cpy
#define memset st_set
#define memcmp st_cmp
#endif  // halfwords inside the uint32_t page image

#ifndef ST_KIND
#define ST_KIND 0    // raw only
#endif
#ifndef ST_SKIP
#define ST_SKIP 0    // do not write when nothing changed
#endif
#ifndef ST_BLANK
#define ST_BLANK 0   // kind 1: a marker byte, so load() tells a never-written store
#endif
#ifndef ST_CHECK
#define ST_CHECK 2
#endif
#ifndef ST_VERH
#define ST_VERH 0    // kinds 1, 2: helper keeps {version, size} in the page; load() fails when either differs
#endif
#ifndef ST_VERSION
#define ST_VERSION 1
#endif
#define ST_HDR (ST_KIND == 1 ? LF_PAGE - 4 : LF_PAGE - 8)  // where the {version, size} halfwords go
static inline bool st_hdr_ok(const uint8_t *b, uint32_t n) {
  return *(const uint16_t *)(b + ST_HDR) == ST_VERSION && *(const uint16_t *)(b + ST_HDR + 2) == n;
}

#if ST_KIND == 1 || ST_KIND == 3
__attribute__((section(".ch32rv_store"), aligned(LF_PAGE))) uint8_t st_area[LF_PAGE];
#elif ST_KIND == 2 || ST_KIND == 4
__attribute__((section(".ch32rv_store"), aligned(LF_PAGE))) uint8_t st_area[2 * LF_PAGE];
#endif

#if ST_KIND == 1
static bool st_load(void *p, uint32_t n) {
#if ST_BLANK
  if (st_area[LF_PAGE - 1] != 0x5A) return false;
#endif
#if ST_VERH
  if (!st_hdr_ok(st_area, n)) return false;
#endif
  memcpy(p, st_area, n);
  return true;
}
static void st_save(const void *p, uint32_t n) {
#if ST_SKIP
  if (!memcmp(st_area, p, n)) return;
#endif
  uint32_t w[LF_WORDS]; memset(w, 0xFF, sizeof w); memcpy(w, p, n);
#if ST_BLANK
  ((uint8_t *)w)[LF_PAGE - 1] = 0x5A;
#endif
#if ST_VERH
  ((st_u16 *)w)[ST_HDR / 2] = ST_VERSION; ((st_u16 *)w)[ST_HDR / 2 + 1] = (uint16_t)n;
#endif
  lf_write_page((uint32_t)st_area, w);
}
#endif

#if ST_KIND == 2
static uint16_t st_sum(const uint8_t *p) {
#if ST_CHECK == 1
  uint16_t c = 0x1234;
  for (uint32_t i = 0; i < LF_PAGE - 2; i++) c = (uint16_t)((c << 1 | c >> 15) + p[i]);
  return c;
#else
  uint16_t c = 0xFFFF;
  for (uint32_t i = 0; i < LF_PAGE - 2; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xA001u & (0u - (c & 1u))); }
  return c;
#endif
}
static int st_ok(const uint8_t *b) { return st_sum(b) == *(const uint16_t *)(b + LF_PAGE - 2) ? 1 + *(const uint16_t *)(b + LF_PAGE - 4) : 0; }
static int st_cur() { const int a = st_ok(st_area), b = st_ok(st_area + LF_PAGE); return (a || b) ? ((int16_t)(a - b) >= 0 ? 0 : 1) : -1; }
static bool st_load(void *p, uint32_t n) {
  const int k = st_cur(); if (k < 0) return false;
#if ST_VERH
  if (!st_hdr_ok(st_area + LF_PAGE * k, n)) return false;
#endif
  memcpy(p, st_area + LF_PAGE * k, n); return true;
}
static void st_save(const void *p, uint32_t n) {
  const int k = st_cur();
#if ST_SKIP
  if (k >= 0 && !memcmp(st_area + LF_PAGE * k, p, n)) return;
#endif
  uint32_t w[LF_WORDS]; uint8_t *b = (uint8_t *)w; memset(w, 0xFF, sizeof w); memcpy(w, p, n);
#if ST_VERH
  *(st_u16 *)(b + ST_HDR) = ST_VERSION; *(st_u16 *)(b + ST_HDR + 2) = (uint16_t)n;
#endif
  *(st_u16 *)(b + LF_PAGE - 4) = k < 0 ? 0 : *(const uint16_t *)(st_area + LF_PAGE * k + LF_PAGE - 4) + 1;
  *(st_u16 *)(b + LF_PAGE - 2) = st_sum(b);
  lf_write_page((uint32_t)(st_area + LF_PAGE * (k == 0)), w);
}
#endif

#if ST_KIND == 3
struct StEeprom {
  uint8_t read(int i) const { return st_area[i]; }
  void write(int i, uint8_t v) { put(i, v); }
  void update(int i, uint8_t v) { put(i, v); }
  template <class T> T &get(int i, T &t) const { memcpy(&t, st_area + i, sizeof t); return t; }
  template <class T> const T &put(int i, const T &t) { commit(i, &t, sizeof t); return t; }
  uint16_t length() const { return 64; }
  __attribute__((noinline)) static void commit(int i, const void *p, uint32_t n) {
    if (!memcmp(st_area + i, p, n)) return;  // update(): AVR write() also skips nothing, but rewriting a page is not one byte
    uint32_t w[16]; memcpy(w, st_area, 64); memcpy((uint8_t *)w + i, p, n);
    lf_write_page((uint32_t)st_area, w);
  }
};
static StEeprom EEPROM;
#endif

#if ST_KIND == 4
#define ST_CAP 16u  // bytes; the log holds 64 records
// Program n words from w at word i0 of the page at a, without erasing (0xFF families: unloaded words stay as they are).
__attribute__((noinline)) static void st_prog(uint32_t a, uint32_t i0, const uint32_t *w, uint32_t n) {
  a |= 0x08000000u;
  volatile LfRegs *f = LF;
  const uint32_t k1 = 0x45670123u, k2 = 0xCDEF89ABu;
  f->KEYR = k1; f->KEYR = k2; f->MODEKEYR = k1; f->MODEKEYR = k2;
  f->CTLR = LF_PG; f->CTLR = LF_PG | LF_RST; lf_busy();
  volatile uint32_t *p = (volatile uint32_t *)a;
  for (uint32_t i = 0; i < n; i++) { p[i0 + i] = w[i]; f->CTLR = LF_PG | LF_LOAD; lf_busy(); }
  f->ADDR = a; f->CTLR = LF_PG | LF_STRT; lf_busy();
  f->CTLR = LF_LOCK;
}
struct StEeprom {
  static const uint16_t *log() { return (const uint16_t *)st_area; }
  __attribute__((noinline)) static uint8_t read(int i) {
    uint8_t v = 0xFF;
    for (const uint16_t *h = log(); h < log() + 64 && *h != 0xFFFF; h++) if ((*h >> 8) == (unsigned)i) v = (uint8_t)*h;
    return v;
  }
  void update(int i, uint8_t v) { write(i, v); }
  template <class T> T &get(int i, T &t) const { uint8_t *b = (uint8_t *)&t; for (uint32_t k = 0; k < sizeof t; k++) b[k] = read(i + k); return t; }
  template <class T> const T &put(int i, const T &t) { const uint8_t *b = (const uint8_t *)&t; for (uint32_t k = 0; k < sizeof t; k++) write(i + k, b[k]); return t; }
  uint16_t length() const { return ST_CAP; }
  __attribute__((noinline)) static void write(int i, uint8_t v) {
    if (read(i) == v) return;
    uint32_t k = 0;
    while (k < 64 && log()[k] != 0xFFFF) k++;
    if (k < 64) {
      const uint32_t r = (uint32_t)i << 8 | v, w = (k & 1) ? (r << 16 | 0xFFFFu) : (0xFFFF0000u | r);
      st_prog((uint32_t)st_area + (k & 32 ? 64 : 0), (k & 31) >> 1, &w, 1);
      return;
    }
    uint8_t img[ST_CAP];
    for (uint32_t j = 0; j < ST_CAP; j++) img[j] = read(j);
    img[i] = v;
    uint32_t w[16]; st_u16 *h = (st_u16 *)w; memset(w, 0xFF, sizeof w);
    for (uint32_t j = 0, n = 0; j < ST_CAP; j++) if (img[j] != 0xFF) h[n++] = (uint16_t)(j << 8 | img[j]);
    lf_write_page((uint32_t)st_area, w);
    memset(w, 0xFF, sizeof w);
    lf_write_page((uint32_t)st_area + 64, w);  // page 1 left blank
  }
};
static StEeprom EEPROM;
#endif

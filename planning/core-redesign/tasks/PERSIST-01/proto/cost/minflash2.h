// Minimal V003 flash store, written for size: raw page erase / program, then one-page and two-bank stores.
#pragma once
#include <Arduino.h>
#include <string.h>
#include "ch32rv_registers.h"
#define MF(o) CH32RV_REG32(0x40022000u + (o))
#define MF_PAGE 64u
extern "C" void ch32rv_millis_advance(uint32_t ms);
static bool mf_wait() {
#if NO_BOUND
  while (MF(0x0C) & 1u) {}
  return true;
#endif
  for (uint32_t n = 0x400000u; n; n--) {
    const uint32_t s = MF(0x0C);
    if (s & 0x10u) return false;  // WRPRTERR
    if (!(s & 1u)) return true;
  }
  return false;
}
static bool mf_op(uint32_t bit, uint32_t a) {  // page erase (PAGE_ER) or program start (PAGE_PG) at a
  MF(0x10) |= bit; MF(0x14) = a; MF(0x10) |= 1u << 6;
  const bool ok = mf_wait();
  MF(0x10) &= ~bit;
  return ok;
}
// Erase one 64 B page and program it from w (16 words). Interrupts off throughout; millis() gets the stall back.
__attribute__((noinline)) static bool mf_write_page(uint32_t a, const uint32_t *w) {
  a |= 0x08000000u;
  const uint32_t irq = ch32rv_irq_save();
  const uint32_t t0 = CH32RV_SYSTICK_CNT;
  MF(0x04) = 0x45670123u; MF(0x04) = 0xCDEF89ABu; MF(0x24) = 0x45670123u; MF(0x24) = 0xCDEF89ABu;
  MF(0x0C) = 0x30u;
  bool ok = mf_op(1u << 17, a);
  MF(0x10) |= 1u << 16; MF(0x10) |= 1u << 19; ok = ok && mf_wait();
  for (uint32_t i = 0; ok && i < 16u; i++) { ((volatile uint32_t *)a)[i] = w[i]; MF(0x10) |= 1u << 18; ok = mf_wait(); }
  MF(0x10) &= ~(1u << 16);
  ok = ok && mf_op(1u << 16, a);
  MF(0x10) = 0; MF(0x0C) = 0x30u; MF(0x10) = (1u << 7) | (1u << 15);
#if !NO_MILLIS
  uint32_t t = CH32RV_SYSTICK_CNT - t0, ms = 0;
  while (t >= F_CPU / 1000u) { t -= F_CPU / 1000u; ms++; }
  ch32rv_irq_restore(irq);
  if (ms) ch32rv_millis_advance(ms - 1u);
#else
  (void)t0; ch32rv_irq_restore(irq);
#endif
#if !NO_VERIFY
  for (uint32_t i = 0; ok && i < 16u; i++) ok = ((volatile uint32_t *)a)[i] == w[i];
#endif
  return ok;
}

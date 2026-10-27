// PERSIST-01 cost ladder, V003 (64 B fast page). The floor writes one page with nothing else; each LF_* switch adds
// one safety item so its byte cost can be measured alone. Experiment only, not the core.
#pragma once
#include <Arduino.h>
#include <string.h>
#include "ch32rv_registers.h"

#ifndef LF_IRQ
#define LF_IRQ 0     // interrupts off around the whole sequence
#endif
#ifndef LF_CLR
#define LF_CLR 0     // clear STATR before, CTLR after (M10: a WRPRTERR left behind misleads ch32rv)
#endif
#ifndef LF_BOUND
#define LF_BOUND 0   // bounded wait, WRPRTERR checked, result returned
#endif
#ifndef LF_MILLIS
#define LF_MILLIS 0  // give millis() back the time the CPU was stalled: 1 measured, 2 a fixed 5 ms (V003 page: ~6 ms)
#endif
#ifndef LF_VERIFY
#define LF_VERIFY 0  // read the page back
#endif
#define LF_RESULT (LF_BOUND || LF_VERIFY)

struct LfRegs { uint32_t ACTLR, KEYR, OBKEYR, STATR, CTLR, ADDR, RSV, OBR, WPR, MODEKEYR; };
#define LF ((volatile LfRegs *)0x40022000u)
#define LF_PAGE 64u
enum : uint32_t { LF_PG = 1u << 16, LF_ER = 1u << 17, LF_LOAD = 1u << 18, LF_RST = 1u << 19, LF_STRT = 1u << 6,
                  LF_LOCK = (1u << 7) | (1u << 15) };
extern "C" void ch32rv_millis_advance(uint32_t ms);

#if LF_BOUND
static bool lf_busy() {
  for (uint32_t n = 0x400000u; n; n--) {
    const uint32_t s = LF->STATR;
    if (s & 0x10u) return false;
    if (!(s & 1u)) return true;
  }
  return false;
}
#define LF_WAIT(ok) ok = ok && lf_busy()
#else
static inline void lf_busy() { while (LF->STATR & 1u) {} }
#define LF_WAIT(ok) lf_busy()
#endif

#if LF_RESULT
#define LF_RET bool
#else
#define LF_RET void
#endif

// Erase the page at a (link address, 64 B aligned) and program it from w (16 words).
__attribute__((noinline)) static LF_RET lf_write_page(uint32_t a, const uint32_t *w) {
  a |= 0x08000000u;  // the controller wants the 0x08000000 address; the 0 alias does nothing
  bool ok = true; (void)ok;
#if LF_IRQ
  const uint32_t irq = ch32rv_irq_save();
#endif
#if LF_MILLIS == 1
  const uint32_t t0 = CH32RV_SYSTICK_CNT;
#endif
  volatile LfRegs *f = LF;
  const uint32_t k1 = 0x45670123u, k2 = 0xCDEF89ABu;
  f->KEYR = k1; f->KEYR = k2; f->MODEKEYR = k1; f->MODEKEYR = k2;
#if LF_CLR
  f->STATR = 0x30u;
#endif
  f->CTLR = LF_ER; f->ADDR = a; f->CTLR = LF_ER | LF_STRT; LF_WAIT(ok);
  f->CTLR = LF_PG; f->CTLR = LF_PG | LF_RST; LF_WAIT(ok);
  volatile uint32_t *p = (volatile uint32_t *)a;
  for (uint32_t i = 0; i < 16u; i++) { p[i] = w[i]; f->CTLR = LF_PG | LF_LOAD; LF_WAIT(ok); }
  f->ADDR = a; f->CTLR = LF_PG | LF_STRT; LF_WAIT(ok);
#if LF_CLR
  f->CTLR = 0; f->STATR = 0x30u;
#endif
  f->CTLR = LF_LOCK;
#if LF_MILLIS == 1
  uint32_t t = CH32RV_SYSTICK_CNT - t0, ms = 0;
  while (t >= F_CPU / 1000u) { t -= F_CPU / 1000u; ms++; }
#endif
#if LF_IRQ
  ch32rv_irq_restore(irq);
#endif
#if LF_MILLIS == 1
  if (ms) ch32rv_millis_advance(ms - 1u);
#elif LF_MILLIS == 2
  ch32rv_millis_advance(5);
#endif
#if LF_VERIFY
  for (uint32_t i = 0; i < 16u; i++) ok = ok && p[i] == w[i];
#endif
#if LF_RESULT
  return ok;
#endif
}

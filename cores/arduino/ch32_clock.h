/* Which AHB prescaler setting gets HCLK from the internal oscillator to F_CPU.
 *
 * Its own header rather than a block inside wiring_time.c because it is a
 * table of silicon facts with no code attached, and a table is worth testing
 * on its own: tests/test_clock_prescaler.py compiles this against every ratio
 * both encodings can and cannot express.
 *
 * Milestone 1 has no PLL, so F_CPU can only be the oscillator divided down.
 * Inputs are the three -D values boards.txt carries per family.
 */
#pragma once

#ifndef F_CPU
#error "F_CPU is required (build.f_cpu in boards.txt)"
#endif
#ifndef CH32_HSI_HZ
#error "CH32_HSI_HZ is required (see build.core_defines in boards.txt)"
#endif
#ifndef CH32_CLOCK_SYSCLK_HZ
#error "CH32_CLOCK_SYSCLK_HZ is required (see build.core_defines in boards.txt)"
#endif
#ifndef CH32_CLOCK_USE_PLL
#error "CH32_CLOCK_USE_PLL is required (see build.core_defines in boards.txt)"
#endif

#ifndef CH32_HPRE_LINEAR
#error "CH32_HPRE_LINEAR is required (see build.core_defines in boards.txt)"
#endif

/* SYSCLK is the oscillator, or the PLL output when boards.txt asked for one;
 * the generator resolved which from clock_configs.csv. HCLK is SYSCLK divided
 * by the AHB prescaler, and F_CPU is HCLK - so the prescaler is still derived
 * rather than configured, exactly as it was before the PLL existed.
 */
#if CH32_CLOCK_SYSCLK_HZ != CH32_HSI_HZ && !CH32_CLOCK_USE_PLL
#error "CH32_CLOCK_SYSCLK_HZ differs from the oscillator but no PLL setting \
came with it. Both are generated together; regenerate boards.txt."
#endif
#if F_CPU > CH32_CLOCK_SYSCLK_HZ
#error "F_CPU is above SYSCLK. The AHB prescaler can only divide, so this \
needs a different clock configuration (see docs/todo.ja.md, clock section)."
#endif
#if (CH32_CLOCK_SYSCLK_HZ % F_CPU) != 0
#error "F_CPU must divide SYSCLK exactly: the AHB prescaler is the only thing \
between them."
#endif

#define CH32_AHB_DIV (CH32_CLOCK_SYSCLK_HZ / F_CPU)

/* The AHB prescaler is four bits in RCC_CFGR0[7:4], and the CH32 line encodes
 * them two different ways. They agree only on /1, so choosing the wrong one is
 * not a near miss - 0x1 is /2 on one and /1 on the other, which would run the
 * core at twice the clock every timing calculation here assumes.
 *
 *   CH32_HPRE_LINEAR=1   V00x (V002-V007, M007), M030, X03x
 *     0x0..0x7 -> /1../8, i.e. the field is the divider minus one
 *     0xB /16   0xC /32   0xD /64   0xE /128   0xF /256
 *
 *   CH32_HPRE_LINEAR=0   V10x, V20x, V30x, V4x7, L103, V205, X3x5
 *     0x0 /1   0x8 /2   0x9 /4   0xA /8   0xB /16
 *     0xC /64  0xD /128 0xE /256 0xF /512          - note there is no /32
 *
 * Read off each family's EVT header (RCC_HPRE_DIVn); every family the
 * generator emits is covered by one that was checked, none are inferred.
 */
#if CH32_HPRE_LINEAR
#  if CH32_AHB_DIV <= 8
#    define CH32_HPRE_FIELD (CH32_AHB_DIV - 1u)
#  elif CH32_AHB_DIV == 16
#    define CH32_HPRE_FIELD 0xBu
#  elif CH32_AHB_DIV == 32
#    define CH32_HPRE_FIELD 0xCu
#  elif CH32_AHB_DIV == 64
#    define CH32_HPRE_FIELD 0xDu
#  elif CH32_AHB_DIV == 128
#    define CH32_HPRE_FIELD 0xEu
#  elif CH32_AHB_DIV == 256
#    define CH32_HPRE_FIELD 0xFu
#  endif
#else
#  if CH32_AHB_DIV == 1
#    define CH32_HPRE_FIELD 0x0u
#  elif CH32_AHB_DIV == 2
#    define CH32_HPRE_FIELD 0x8u
#  elif CH32_AHB_DIV == 4
#    define CH32_HPRE_FIELD 0x9u
#  elif CH32_AHB_DIV == 8
#    define CH32_HPRE_FIELD 0xAu
#  elif CH32_AHB_DIV == 16
#    define CH32_HPRE_FIELD 0xBu
#  elif CH32_AHB_DIV == 64
#    define CH32_HPRE_FIELD 0xCu
#  elif CH32_AHB_DIV == 128
#    define CH32_HPRE_FIELD 0xDu
#  elif CH32_AHB_DIV == 256
#    define CH32_HPRE_FIELD 0xEu
#  elif CH32_AHB_DIV == 512
#    define CH32_HPRE_FIELD 0xFu
#  endif
#endif

#ifndef CH32_HPRE_FIELD
#error "SYSCLK / F_CPU is not a ratio this family's AHB prescaler can \
encode. The usable dividers are listed just above this line."
#endif

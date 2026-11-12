/* CH32RV_INTERRUPT: the attribute of an interrupt handler.
 *
 * Every series but CH32V103 turns HPE on at startup (INTSYSCR bit 0,
 * crt0_ch32.S): on entry the QingKe core saves ra, t0-t6 and a0-a7 (ra,
 * t0-t2 and a0-a5 on RV32E) itself and restores them on mret. Such a handler
 * is WCH's "WCH-Interrupt-fast" (clang from ch32rv-toolchain, patches/): it saves
 * only the callee-saved and floating-point registers it uses. A handler that
 * calls a function keeps 7 instructions of prologue and epilogue where a
 * plain `interrupt` had 29 (rv32ec) to 41 (rv32imac); on a core with an FPU
 * the 20 caller-saved float registers are still saved there, as HPE does not.
 *
 * The nesting the startup code allows never goes past HPE's depth: where it
 * could (V4F at 4 levels against 3), INTSYSCR's HWSTKOVEN stays 0, so the
 * level that would overflow is not taken. CH32V103 (QingKe V3A) has no HPE:
 * a plain `interrupt`, saving everything it uses. */
#pragma once

#if CH32RV_HPE
#define CH32RV_INTERRUPT __attribute__((interrupt("WCH-Interrupt-fast")))
#else
#define CH32RV_INTERRUPT __attribute__((interrupt))
#endif

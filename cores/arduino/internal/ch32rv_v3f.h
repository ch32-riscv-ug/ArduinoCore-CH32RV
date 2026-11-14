/* CH32H41x with the sketch on the V5F (CPU Core = V5F): the V3F as a second
 * core. At reset the V3F wakes the V5F and waits in stop (crt0_ch32.S); these
 * hand it a function to run.
 *
 *   - One image: the function is any function of the sketch, with the
 *     instruction set both cores share (rv32imafc + B; -march is that).
 *   - It runs with interrupts off, on the stack given. When it returns the V3F
 *     waits again and can be given the next one.
 *   - millis(), micros(), delay() and delayMicroseconds() work there (the
 *     V5F's SysTick counts). delay() on the V3F does not run the system
 *     services (USB, the IP stack): those are the V5F's.
 *   - What else it may call is what touches no peripheral the V5F uses and no
 *     interrupt: Serial, Wire, SPI and the rest stay on the V5F. Data between
 *     the two through volatile variables or the A extension's atomics (the
 *     shared SRAM has no cache on either side).
 *   - Nothing stops it from outside: a function that never returns keeps the
 *     V3F until a reset.
 *
 * Elsewhere ch32rv_v3f_launch() is 0 and ch32rv_on_v3f() 0. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CH32RV_RUN_V5F)
/* PFIC_SCTLR bit 16: the core reading it (1 the V5F, 0 the V3F; the EVT's
 * NVIC_GetCurrentCoreID()). mhartid is not readable on the V3F. */
static inline int ch32rv_on_v3f(void)
{
    return (*(volatile uint32_t *)0xE000ED10u & (1u << 16)) == 0u;
}
#else
static inline int ch32rv_on_v3f(void)
{
    return 0;
}
#endif

/* Start fn(arg) on the V3F with its stack ending at stack_top (16-byte
 * aligned; the stack grows down). 0 when the V3F is busy, when called from the
 * V3F itself, or where there is no V3F to start. */
int ch32rv_v3f_launch(void (*fn)(void *), void *arg, void *stack_top);
/* Whether the V3F has a function (taken or not yet taken). */
int ch32rv_v3f_running(void);

#ifdef __cplusplus
}
#endif

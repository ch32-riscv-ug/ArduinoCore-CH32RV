/* CH32H41x with the sketch on the V5F (CPU Core = V5F): the V3F as a second
 * core. At reset the V3F wakes the V5F and waits in stop (crt0_ch32.S); these
 * hand it a function to run.
 *
 *   - One image: the function is any function of the sketch, with the
 *     instruction set both cores share (rv32imafc + B; -march is that).
 *   - It runs in machine mode with interrupts on, on the stack given. When it
 *     returns the V3F masks them and waits again, and can be given the next.
 *   - Interrupts: an interrupt enabled from the V3F is taken on the V3F - the
 *     low 32 (IPC, HSEM, SysTick0) are each core's own, and enabling a shared
 *     one moves it to the enabling core (PFIC_IALLOCR, RM 4.7.5.45). So a
 *     driver begun on the V3F (Serial1.begin(), attachInterrupt(), a timer's
 *     callback) has its handler run there. Priorities are the V3F's two levels
 *     (bit 7); CH32RV_IRQ_PRIORITY() counts the V5F's eight.
 *   - millis(), micros(), delay() and delayMicroseconds() work there (the
 *     V5F's SysTick counts). delay() on the V3F does not run the system
 *     services (USB, the IP stack, USB PD): those are the V5F's.
 *   - Peripherals: one core per peripheral. Each USART, timer, SPI, I2C and
 *     ADC is used from one core only - the V5F's Serial (and printf, and the
 *     core's console) stays the V5F's, the V3F takes another (Serial1, ...).
 *     The registers several drivers share - RCC's clock enables, a port's
 *     configuration, AFIO's pin functions, EXTICR and EXTI - are written under
 *     the cores' lock (internal/ch32rv_cores.h), so pins of one port and the
 *     EXTI lines may be split between the cores; one pin, or one EXTI vector
 *     group (EXTI9_5, EXTI15_10), belongs to one core.
 *   - Not on the V3F: the system services and what drives them (USB, the IP
 *     stack, USB PD), sleep, restart, the watchdogs, the RTC and the flash
 *     (CH32RV.*), and anything that times itself by counting instructions at
 *     F_CPU (rgbLedWrite() / neopixelWrite(): F_CPU is the V5F's 400 MHz).
 *   - The heap: malloc, new and String are the V5F's (picolibc's malloc has
 *     no lock); the V3F uses static or stack memory.
 *   - Data between the two: volatile variables, the A extension's atomics
 *     (the shared SRAM has no cache on either side), the HSEM and the IPC
 *     (ch32rv_multicore.h).
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

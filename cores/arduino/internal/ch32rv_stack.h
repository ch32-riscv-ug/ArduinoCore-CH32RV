/* The stack the linker reserves above the heap (Stack Size menu) and the
 * Stack Check menu's overflow check (ch32rv_stack.c).
 *
 * The heap may grow up to the reservation's bottom, so a stack deeper than
 * the reservation writes over the heap's top without a word. With Stack Check
 * on (CH32RV_STACK_CHECK), main() fills the reservation below its own frame
 * with a pattern before setup(), and the SysTick interrupt looks at the
 * reservation's bottom 64 bytes every millisecond: once the stack has reached
 * them, the part resets and CH32RV.resetReason() says panic (ESP32's
 * ESP_RST_PANIC). The fill also gives CH32RV.getMinFreeStack(). */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH32RV_STACK_FILL 0xA5C3A5C3u

/* Bytes of the reservation (__stack_size). */
uint32_t ch32rv_stack_size(void);
/* Bytes at the reservation's bottom the stack has never reached since the
 * fill; 0 without Stack Check (nothing filled). */
uint32_t ch32rv_stack_min_free(void);

#if CH32RV_STACK_CHECK
void ch32rv_stack_fill(void);
/* From SysTick: resets through ch32rv_stack_overflow() once the stack has
 * reached the reservation's bottom. */
void ch32rv_stack_check(void);
/* Weak: the default records the panic and resets. */
void ch32rv_stack_overflow(void) __attribute__((noreturn));
#endif

/* ch32rv_stack_overflow() leaves {MAGIC, ~MAGIC} in .noinit before its
 * reset; the next boot's ch32rv_stack_fill() takes it into
 * ch32rv_stack_panicked and clears it, so CH32RV.resetReason() says panic for
 * that boot only (two words: .noinit holds whatever was there before, the
 * last sketch's data included). */
#define CH32RV_PANIC_MAGIC 0x50414E43u   /* "PANC" */
extern uint8_t ch32rv_stack_panicked;

#ifdef __cplusplus
}
#endif

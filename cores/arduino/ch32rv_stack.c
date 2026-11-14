/* internal/ch32rv_stack.h. */
#include "internal/ch32rv_stack.h"
#include "internal/ch32rv_registers.h"

extern uint32_t _heap_end[];    /* the reservation's bottom (linker script) */
extern uint32_t _eusrstack[];   /* its top */

static uint32_t panic_record[2] __attribute__((section(".noinit")));
uint8_t ch32rv_stack_panicked;
#if CH32RV_STACK_CHECK
static uint8_t ch32rv_stack_filled;
#endif

uint32_t ch32rv_stack_size(void)
{
    return (uint32_t)((char *)_eusrstack - (char *)_heap_end);
}

uint32_t ch32rv_stack_min_free(void)
{
#if CH32RV_STACK_CHECK
    if (!ch32rv_stack_filled) {
        return 0;
    }
    const uint32_t *p = _heap_end;
    while (p < _eusrstack && *p == CH32RV_STACK_FILL) {
        p++;
    }
    return (uint32_t)((char *)p - (char *)_heap_end);
#else
    return 0;
#endif
}

#if CH32RV_STACK_CHECK

/* The reservation's bottom 64 bytes: one word alone was stepped over by a
 * frame whose unwritten part lay on it (H417, 2026-11-14: 250 frames of 64
 * bytes came back with the word intact and 12 bytes above it reached). */
#define GUARD_WORDS 16u

void ch32rv_stack_check(void)
{
    if (!ch32rv_stack_filled) {
        return;
    }
    for (uint32_t i = 0; i < GUARD_WORDS; i++) {
        if (_heap_end[i] != CH32RV_STACK_FILL) {
            ch32rv_stack_overflow();
        }
    }
}

/* From main(), before setup(): the reservation below this frame, less a
 * margin for the calls this makes. */
void ch32rv_stack_fill(void)
{
    ch32rv_stack_panicked = panic_record[0] == CH32RV_PANIC_MAGIC && panic_record[1] == ~CH32RV_PANIC_MAGIC;
    panic_record[0] = panic_record[1] = 0;
    uint32_t *sp;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    for (uint32_t *p = _heap_end; p < sp - 16; p++) {
        *p = CH32RV_STACK_FILL;
    }
    ch32rv_stack_filled = 1;
}

__attribute__((weak, noreturn)) void ch32rv_stack_overflow(void)
{
    panic_record[0] = CH32RV_PANIC_MAGIC;
    panic_record[1] = ~CH32RV_PANIC_MAGIC;
    CH32RV_SYSTEM_RESET();
    for (;;) {
    }
}
#endif

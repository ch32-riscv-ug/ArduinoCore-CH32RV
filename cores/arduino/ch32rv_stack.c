/* internal/ch32rv_stack.h. */
#include "internal/ch32rv_stack.h"
#include "internal/ch32rv_registers.h"

extern uint32_t _susrstack[];   /* the reservation's bottom (linker script) */
extern uint32_t _eusrstack[];   /* its top */

uint8_t ch32rv_stack_panicked;
#if CH32RV_STACK_CHECK
static uint32_t panic_record[2] __attribute__((section(".noinit")));
static uint8_t ch32rv_stack_filled;
/* The reservation's bottom 64 bytes: one word alone was stepped over by a
 * frame whose unwritten part lay on it (H417, 2026-11-14: 250 frames of 64
 * bytes came back with the word intact and 12 bytes above it reached). */
#define GUARD_WORDS 16u
uint8_t ch32rv_stack_guarded;
#endif

uint32_t ch32rv_stack_size(void)
{
    return (uint32_t)((char *)_eusrstack - (char *)_susrstack);
}

uint32_t ch32rv_stack_min_free(void)
{
#if CH32RV_STACK_CHECK
    if (!ch32rv_stack_filled) {
        return 0;
    }
    /* The PMP guard is no-access: counted as never reached (it is the bottom, and an access there resets). */
    const uint32_t *p = _susrstack + (ch32rv_stack_guarded ? GUARD_WORDS : 0u);
    while (p < _eusrstack && *p == CH32RV_STACK_FILL) {
        p++;
    }
    return (uint32_t)((char *)p - (char *)_susrstack);
#else
    return 0;
#endif
}

#if CH32RV_STACK_CHECK


void ch32rv_stack_check(void)
{
    if (!ch32rv_stack_filled || ch32rv_stack_guarded) {
        return;                     /* the PMP guard resets on the access itself */
    }
    for (uint32_t i = 0; i < GUARD_WORDS; i++) {
        if (_susrstack[i] != CH32RV_STACK_FILL) {
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
    for (uint32_t *p = _susrstack + (ch32rv_stack_guarded ? GUARD_WORDS : 0u); p < sp - 16; p++) {
        *p = CH32RV_STACK_FILL;
    }
    ch32rv_stack_filled = 1;
}

/* The guard in hardware where the core has PMP (QingKe V3 / V4 / V5 but V4B and V3B: X035, L103, V30x, V4x7, H41x,
 * ... - read back to tell, 2026-11-19): from crt0 in machine mode, before main(). The same 64 bytes as above, no
 * access at all, locked - so the handlers (machine mode) are held to it too, and it stays until reset - then a
 * region that allows everything else: with any region set, a user-mode access no region matches is refused, and
 * the sketch runs in user mode on these families. The first store into the guard faults at once (mcause 7, a
 * load 5) instead of being seen by the next SysTick; HardFault_Handler below turns it into the same panic reset.
 * TOR: _susrstack is aligned to 4 only. */
void ch32rv_stack_guard(void)
{
    uint32_t back;
    __asm__ volatile ("csrw pmpaddr0, %1\n\tcsrr %0, pmpaddr0" : "=r"(back) : "r"(0xFFFFFFFFu));
    if (back == 0u) {
        return;                     /* no PMP (V2, V4B, V3B): the SysTick check alone */
    }
    const uint32_t lo = (uint32_t)(uintptr_t)_susrstack;
    const uint32_t hi = lo + GUARD_WORDS * 4u;
    __asm__ volatile ("csrw pmpaddr0, %0" :: "r"(lo >> 2));
    __asm__ volatile ("csrw pmpaddr1, %0" :: "r"(hi >> 2));
    __asm__ volatile ("csrw pmpaddr2, %0" :: "r"(0xFFFFFFFFu));      /* NAPOT: all of it */
    /* entry 0 OFF (TOR's base), entry 1 L | TOR and no R/W/X, entry 2 NAPOT | R | W | X */
    __asm__ volatile ("csrw pmpcfg0, %0" :: "r"(0x00u | (0x88u << 8) | (0x1Fu << 16)));
    ch32rv_stack_guarded = 1;
}

/* A fault on the guard - its address (mtval) in it, or the stack pointer already at it - is an overflow: off the
 * guard first (the stack's top: nothing there matters now), then the panic reset. Anything else waits as the
 * default handler does. Naked: no frame of its own on a stack that has just run out. */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm__ volatile (
        "csrr t0, mcause\n\t"
        "addi t0, t0, -5\n\t"             /* load access fault */
        "beqz t0, 1f\n\t"
        "addi t0, t0, -2\n\t"             /* store access fault */
        "bnez t0, 3f\n"
        "1:\n\t"
        "la t1, _susrstack\n\t"
        "addi t1, t1, 64\n\t"             /* the guard's top (GUARD_WORDS * 4) */
        "bleu sp, t1, 2f\n\t"
        "csrr t0, mtval\n\t"
        "bgeu t0, t1, 3f\n\t"
        "addi t1, t1, -64\n\t"
        "bltu t0, t1, 3f\n"
        "2:\n\t"
        "la sp, _eusrstack\n\t"
        "j ch32rv_stack_overflow\n"
        "3:\n\t"
        "j 3b\n");
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

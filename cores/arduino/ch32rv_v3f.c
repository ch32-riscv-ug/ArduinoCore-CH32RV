/* The V3F as a second core (internal/ch32rv_v3f.h). */
#include "internal/ch32rv_v3f.h"
#include "internal/ch32rv_registers.h"

#if defined(CH32RV_RUN_V5F)

/* crt0_ch32.S: {function, argument, stack top, running}. The V3F clears the
 * function when it takes it, sets running before the call and clears it
 * after. */
extern volatile uint32_t ch32rv_v3f_mailbox[4];

#define PFIC_SCTLR      (*(volatile uint32_t *)0xE000ED10u)
#define SCTLR_SETEVENT  (1u << 5)

/* ch32rv_ipc.c, when the sketch uses the IPC: its channels set up before the
 * V3F runs anything that could send or listen. */
void ch32rv_ipc_init(void) __attribute__((weak));

int ch32rv_v3f_running(void)
{
    return ch32rv_v3f_mailbox[0] != 0u || ch32rv_v3f_mailbox[3] != 0u;
}

int ch32rv_v3f_launch(void (*fn)(void *), void *arg, void *stack_top)
{
    if (!fn || !stack_top || ch32rv_on_v3f() || ch32rv_v3f_running()) {
        return 0;
    }
    if (ch32rv_ipc_init) {
        ch32rv_ipc_init();
    }
    /* From here on the shared registers are written under the cores' lock
     * (internal/ch32rv_cores.h), the V5F's writes too. */
    ch32rv_cores_shared = 1u;
    ch32rv_v3f_mailbox[1] = (uint32_t)(uintptr_t)arg;
    ch32rv_v3f_mailbox[2] = (uint32_t)(uintptr_t)stack_top & ~15u;
    __asm__ volatile ("fence w, w" ::: "memory");
    ch32rv_v3f_mailbox[0] = (uint32_t)(uintptr_t)fn;   /* last: the V3F looks at this one */
    __asm__ volatile ("fence w, w" ::: "memory");
    PFIC_SCTLR |= SCTLR_SETEVENT;                         /* wakes the V3F's wfi (WFITOWFE) */
    return 1;
}

#else

int ch32rv_v3f_launch(void (*fn)(void *), void *arg, void *stack_top)
{
    (void)fn;
    (void)arg;
    (void)stack_top;
    return 0;
}

int ch32rv_v3f_running(void)
{
    return 0;
}

#endif

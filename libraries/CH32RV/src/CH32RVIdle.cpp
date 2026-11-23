/* CH32RV.idle() (CH32RVSystem.h): the core's sleep mode - the CPU waits in
 * WFI, SLEEPDEEP clear, so every clock and peripheral keeps running and any
 * interrupt (SysTick's each millisecond among them) wakes it. A file of its
 * own, linked only when called. */
#include "CH32RVSystem.h"

#include "Arduino.h"
#include "internal/ch32rv_registers.h"

namespace arduino {

void CH32RVSystem::idle(uint32_t ms)
{
#if defined(CH32RV_SERIES_CH32V103)
    /* QingKe V3A: SysTick does not end a WFI (ch32rv_power.c, 2026-11-18),
     * so a WFI with nothing else to wake it never comes back: waited out. */
    const uint32_t t0 = millis();
    do {
        yield();
    } while (millis() - t0 < ms);
#else
    CH32RV_PFIC_SCTLR &= ~(1u << 2);   /* SLEEPDEEP clear: sleep, not stop */
    const uint32_t t0 = millis();
    do {
        yield();
        __asm__ volatile("wfi");
    } while (millis() - t0 < ms);
#endif
}

}   // namespace arduino

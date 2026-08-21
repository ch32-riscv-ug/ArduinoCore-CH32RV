/* Clock setup and the millis/micros/delay family.
 *
 * Milestone 1 clocking (docs/todo.ja.md): internal oscillator only, no PLL.
 * F_CPU is the target HCLK and the AHB prescaler is *derived* from it, so a
 * different clock is a boards.txt change and nothing else - which is the whole
 * point, because the clock menu that comes later is then just more boards.txt
 * lines. A ratio the hardware cannot encode is a compile error rather than a
 * runtime surprise: a wrong HCLK shows up as garbled Serial output, since the
 * baud divisor is computed from F_CPU, and that is expensive to debug.
 */
#include "Arduino.h"
#include "ch32_clock.h"
#include "ch32_registers.h"

#ifndef CH32_FLASH_LATENCY
#error "CH32_FLASH_LATENCY is required (see build.core_defines in boards.txt)"
#endif

#define CH32_TICKS_PER_MS ((uint32_t)(F_CPU / 1000u))
#define CH32_TICKS_PER_US ((uint32_t)(F_CPU / 1000000u))

static volatile uint32_t ch32_millis_counter;

void SystemInit(void)
{
    /* HSI is already the reset default; make it explicit so the core does not
     * depend on whatever a debugger or bootloader left behind. */
    CH32_RCC_CTLR |= CH32_RCC_CTLR_HSION;
    while ((CH32_RCC_CTLR & CH32_RCC_CTLR_HSIRDY) == 0u) {
    }

    /* Wait states first: the core comes out of reset on a divided clock, and
     * the prescaler below may raise it. Too few wait states at 48 MHz makes the
     * CPU fetch garbage - measured on CH32X035, which needs two and hangs
     * before setup() without them. Too many are merely slow, which is why
     * CH32_FLASH_LATENCY is sized for the family's fastest clock and left alone
     * when F_CPU asks for a slower one, and why setting it before the switch is
     * safe rather than after. */
    CH32_FLASH_ACTLR = (CH32_FLASH_ACTLR & ~CH32_FLASH_ACTLR_LATENCY_MASK) |
                       CH32_FLASH_LATENCY;

    CH32_RCC_CFGR0 &= ~(CH32_RCC_CFGR0_SW_MASK | CH32_RCC_CFGR0_HPRE_MASK |
                        CH32_RCC_CFGR0_PPRE1_MASK | CH32_RCC_CFGR0_PPRE2_MASK);
    CH32_RCC_CFGR0 |= CH32_RCC_CFGR0_SW_HSI |
                      CH32_RCC_CFGR0_HPRE(CH32_HPRE_FIELD);
    while ((CH32_RCC_CFGR0 & CH32_RCC_CFGR0_SWS_MASK) !=
           (CH32_RCC_CFGR0_SW_HSI << 2)) {
    }

    /* 1 kHz tick straight off HCLK, which the prescaler above made F_CPU. */
    CH32_SYSTICK_CTLR = 0u;
    CH32_SYSTICK_SR = 0u;
    CH32_SYSTICK_CNT = 0u;
    CH32_SYSTICK_CMP = CH32_TICKS_PER_MS - 1u;
#if CH32_SYSTICK_64
    CH32_SYSTICK_CNT_HI = 0u;
    CH32_SYSTICK_CMP_HI = 0u;
#endif
    ch32_irq_enable(CH32_IRQN_SysTick);
    CH32_SYSTICK_CTLR = CH32_SYSTICK_CTLR_STE | CH32_SYSTICK_CTLR_STIE |
                        CH32_SYSTICK_CTLR_STCLK;
}

/* TODO(docs/todo.ja.md): use the hardware auto-reload bit where the family has
 * one instead of rewinding the counter by hand. */
__attribute__((interrupt)) void SysTick_Handler(void)
{
    CH32_SYSTICK_SR = 0u;
    CH32_SYSTICK_CNT = 0u;
#if CH32_SYSTICK_64
    CH32_SYSTICK_CNT_HI = 0u;
#endif
    ch32_millis_counter++;
}

unsigned long millis(void)
{
    return ch32_millis_counter;
}

/* Wraps every 2^32 us (about 71 minutes), same as the AVR core. Differences
 * stay correct across the wrap because the arithmetic is modulo 2^32. */
unsigned long micros(void)
{
    uint32_t ms, ticks;

    do {
        ms = ch32_millis_counter;
        ticks = CH32_SYSTICK_CNT;
    } while (ms != ch32_millis_counter);

    return ms * 1000u + ticks / CH32_TICKS_PER_US;
}

void delay(unsigned long ms)
{
    const uint32_t start = ch32_millis_counter;

    while ((uint32_t)(ch32_millis_counter - start) < (uint32_t)ms) {
        yield();
    }
}

void delayMicroseconds(unsigned int us)
{
    const uint32_t start = micros();

    while ((uint32_t)(micros() - start) < (uint32_t)us) {
    }
}

/* Weak so a sketch, or a cooperative scheduler, can take it over. delay()
 * calls it, which is the whole point: without the weak attribute a sketch that
 * defines its own yield() fails to link, and that is a contract several
 * libraries rely on. */
__attribute__((weak)) void yield(void)
{
}

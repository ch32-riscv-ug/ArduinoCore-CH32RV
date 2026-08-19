/* Clock setup and the millis/micros/delay family.
 *
 * Milestone 1 clocking (docs/todo.ja.md): internal oscillator only, no PLL,
 * no AHB/APB division. F_CPU therefore has to equal the family's HSI, and a
 * mismatch is a compile error rather than a runtime surprise - a wrong F_CPU
 * shows up as garbled Serial output, which is expensive to debug.
 */
#include "Arduino.h"
#include "ch32_registers.h"

#if F_CPU != CH32_HSI_HZ
#error "Milestone 1 runs the core straight off HSI: F_CPU must equal CH32_HSI_HZ. \
Prescalers and PLL are not implemented yet (see docs/todo.ja.md, clock section)."
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

    CH32_RCC_CFGR0 &= ~(CH32_RCC_CFGR0_SW_MASK | CH32_RCC_CFGR0_HPRE_MASK |
                        CH32_RCC_CFGR0_PPRE1_MASK | CH32_RCC_CFGR0_PPRE2_MASK);
    CH32_RCC_CFGR0 |= CH32_RCC_CFGR0_SW_HSI | CH32_RCC_CFGR0_HPRE_DIV1;
    while ((CH32_RCC_CFGR0 & CH32_RCC_CFGR0_SWS_MASK) !=
           (CH32_RCC_CFGR0_SW_HSI << 2)) {
    }

    /* 1 kHz tick straight off HCLK. */
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

void yield(void)
{
}

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
#ifndef CH32_CLOCK_INIT
#error "CH32_CLOCK_INIT is required (see build.clock_init in boards.txt)"
#endif
#include CH32_GENERATED_(CH32_CLOCK_INIT)

#ifndef CH32_FLASH_LATENCY
#error "CH32_FLASH_LATENCY is required (see build.core_defines in boards.txt)"
#endif

#define CH32_TICKS_PER_MS ((uint32_t)(F_CPU / 1000u))
#define CH32_TICKS_PER_US ((uint32_t)(F_CPU / 1000000u))

static volatile uint32_t ch32_millis_counter;

void SystemInit(void)
{
    /* Put RCC back where reset leaves it before touching anything. This used
     * to be one line - enable HSI - on the theory that the rest was already at
     * its default. It is not: a program that ran before can leave HSE and the
     * PLL running, and reflashing does not clear them.
     *
     * That is not cosmetic. PLLSRC and PLLMULL are read-only while PLLON is
     * set, so the PLL configuration written below is silently ignored and the
     * part keeps whatever multiplier it already had. Measured on CH32V103: a
     * leftover HSE x9 survived, RCC->CFGR0's upper half never changed, and the
     * core ran at a frequency it had not chosen.
     *
     * The sequence is EVT's own and differs per family, so it is generated
     * (clock_init_<family>.h, from clock_init.csv). */
    CH32_CLOCK_INIT_RESET();
    while ((CH32_RCC_CTLR & CH32_RCC_CTLR_HSIRDY) == 0u) {
    }

    /* Wait states first: the core comes out of reset on a divided clock, and
     * the prescaler below may raise it. Too few wait states at 48 MHz makes the
     * CPU fetch garbage - measured on CH32X035, which needs two and hangs
     * before setup() without them. Too many are merely slow, which is why
     * CH32_FLASH_LATENCY is sized for the family's fastest clock and left alone
     * when F_CPU asks for a slower one, and why setting it before the switch is
     * safe rather than after.
     *
     * Families whose flash needs no wait states have a zero mask and are not
     * touched at all: EVT never writes ACTLR on CH32V20x/V307/V407, so what
     * the low bits mean there is not established. */
#if CH32_FLASH_ACTLR_LATENCY_MASK
    CH32_FLASH_ACTLR = (CH32_FLASH_ACTLR & ~(uint32_t)CH32_FLASH_ACTLR_LATENCY_MASK) |
                       CH32_FLASH_LATENCY;
#endif

    /* Both APB prescalers stay at /1, so PCLK1 == PCLK2 == HCLK == F_CPU and
     * every peripheral divisor in this core can be computed from F_CPU alone.
     *
     * EVT does not do this: CH32V103/V20x/V30x/L103 set PPRE1=/2 at every PLL
     * frequency, /1 only at 24 MHz. That is exactly where STM32F1's 36 MHz
     * APB1 ceiling falls, and these families are STM32F1-shaped, whereas
     * CH32V205 (/1 to 192 MHz) and CH32V407 (/1 to 480 MHz) are not and do not
     * halve anything. The datasheets are unambiguous that /1 is in spec -
     * operating_conditions.csv gives F_PCLK1 max == F_HCLK max on every one of
     * them (CH32V103 80, CH32L103 96, CH32V20x and CH32V30x 144). So this
     * follows the datasheet rather than EVT.
     *
     * TODO(docs/todo.ja.md): confirm on hardware at 144 MHz, and ask upstream
     * why EVT halves it. If APB1 turns out to have a real ceiling, PCLK1 stops
     * being F_CPU and USART2-5, I2C, SPI2/3 and the APB1 timers each need
     * their own divisor - which is why this is worth settling before it
     * spreads. */
    CH32_RCC_CFGR0 &= ~(CH32_RCC_CFGR0_SW_MASK | CH32_RCC_CFGR0_HPRE_MASK |
                        CH32_RCC_CFGR0_PPRE1_MASK | CH32_RCC_CFGR0_PPRE2_MASK);
    CH32_RCC_CFGR0 |= CH32_RCC_CFGR0_SW_HSI |
                      CH32_RCC_CFGR0_HPRE(CH32_HPRE_FIELD);
    while ((CH32_RCC_CFGR0 & CH32_RCC_CFGR0_SWS_MASK) !=
           (CH32_RCC_CFGR0_SW_HSI << 2)) {
    }

/* The flag, not the value: CH32V30x_D8C encodes a x18 multiplier as field
 * 0 (RCC_PLLMULL18_EXTEN), so a zero PLL word is a real configuration. */
#if CH32_CLOCK_USE_PLL
    /* Some families gate the oscillator's path into the PLL from outside RCC:
     * CH32L103/V103/V205/V20x/V30x take HSI/2 unless EXTEN_PLL_HSI_PRE says
     * otherwise, and every HSI multiplier the tables list assumes the whole
     * oscillator. Without this, SYSCLK comes out at half. The register is not
     * even called the same thing on all of them (CH32V205 spells it CTLR0),
     * so its address arrives as a number rather than a name. */
#if CH32_CLOCK_EXTEN_ADDR
    CH32_REG32(CH32_CLOCK_EXTEN_ADDR) |= (uint32_t)CH32_CLOCK_EXTEN_BITS;
#endif
    CH32_RCC_CFGR0 = (CH32_RCC_CFGR0 & ~(uint32_t)CH32_CLOCK_PLL_MASK) |
                     (uint32_t)CH32_CLOCK_PLL_VALUE;
    CH32_RCC_CTLR |= CH32_RCC_CTLR_PLLON;
    while ((CH32_RCC_CTLR & CH32_RCC_CTLR_PLLRDY) == 0u) {
    }
    CH32_RCC_CFGR0 = (CH32_RCC_CFGR0 & ~CH32_RCC_CFGR0_SW_MASK) |
                     CH32_RCC_CFGR0_SW_PLL;
    while ((CH32_RCC_CFGR0 & CH32_RCC_CFGR0_SWS_MASK) !=
           (CH32_RCC_CFGR0_SW_PLL << 2)) {
    }
#endif

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

/* CH32RVLowPowerTimer.h. The LPTIM of CH32L103 / M103 and CH32H41x: the
 * STM32 LPTIM's registers (ISR, ICR, IER, CFGR, CR, CMP, ARR, CNT at 0x00 ..
 * 0x1C; CH32H417RM 17, CH32L103RM), with CFGR[26:25] choosing the internal
 * clock (PCLK1, HSI, LSE, LSI). ARR and CMP are written through to the
 * timer's own clock: a second write waits for ARROK / CMPOK (RM 17.3.8),
 * and the start bits need the timer enabled. */
#include "CH32RVLowPowerTimer.h"

#if CH32RV_HAS_LPTIM

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_interrupt.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_exti_line.h"

#define LPT(b, off) CH32RV_REG32((b) + (off))
#define ISR(b)  LPT(b, 0x00u)
#define ICR(b)  LPT(b, 0x04u)
#define IER(b)  LPT(b, 0x08u)
#define CFGR(b) LPT(b, 0x0Cu)
#define CR(b)   LPT(b, 0x10u)
#define CMP(b)  LPT(b, 0x14u)
#define ARR(b)  LPT(b, 0x18u)
#define CNT(b)  LPT(b, 0x1Cu)

#define F_CMPM    (1u << 0)
#define F_ARRM    (1u << 1)
#define F_CMPOK   (1u << 3)
#define F_ARROK   (1u << 4)
#define F_DIRSYNC (1u << 7)

#define CFGR_CKSEL          (1u << 0)          /* the clock is CH1 itself */
#define CFGR_CKPOL(n)       ((uint32_t)(n) << 1)
#define CFGR_PRESC(n)       ((uint32_t)(n) << 9)
#define CFGR_TIMOUT         (1u << 19)         /* a trigger restarts the count (none used here) */
#define CFGR_WAVPOL         (1u << 21)
#define CFGR_COUNTMODE      (1u << 23)         /* count CH1's edges, sampled */
#define CFGR_ENC            (1u << 24)
#define CFGR_CLKSEL(n)      ((uint32_t)(n) << 25)

#define CR_ENABLE  (1u << 0)
#define CR_SNGSTRT (1u << 1)
#define CR_CNTSTRT (1u << 2)
#define CR_OUTEN   (1u << 3)
#define CR_DIREXT  (1u << 4)

namespace {

struct Pad {
    uint8_t role;   /* 0 CH1, 1 CH2, 2 ETR, 3 OUT */
    uint8_t pad;
    uint32_t value, value2;
};

/* A timer with no pad on the series (LPTIM1 on CH32H416) still times and wakes. */
#ifdef CH32RV_LPTIM1_PADS
const Pad lpt1_pads[] = CH32RV_LPTIM1_PADS;
#define LPT1_PADS lpt1_pads, CH32RV_LPTIM1_PAD_COUNT
#else
#define LPT1_PADS nullptr, 0
#endif
#ifdef CH32RV_LPTIM2_PADS
const Pad lpt2_pads[] = CH32RV_LPTIM2_PADS;
#define LPT2_PADS lpt2_pads, CH32RV_LPTIM2_PAD_COUNT
#else
#define LPT2_PADS nullptr, 0
#endif

struct Instance {
    uint32_t base;
    uint32_t clken_addr, clken_mask;
    uint32_t irqn;
    uint8_t wake;
    const Pad *pads;
    uint8_t pad_count;
};

#if defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
#define LSE_BDCTLR CH32RV_REG32(0x40021024u)
const Instance instances[] = {
    {0x40002400u, CH32RV_CLKEN_LPTIM1_ADDR, CH32RV_CLKEN_LPTIM1_MASK, CH32RV_IRQN_LPTIM1,
     CH32RV_EXTI_LPTIM_WAKEUP, LPT1_PADS},
    {0x40003400u, CH32RV_CLKEN_LPTIM2_ADDR, CH32RV_CLKEN_LPTIM2_MASK, CH32RV_IRQN_LPTIM2,
     CH32RV_EXTI_LPTIM2_WAKEUP, LPT2_PADS},
};
#define LPT_HANDLER1 LPTIM1_IRQHandler
#define LPT_HANDLER2 LPTIM2_IRQHandler
#else   /* L103 / M103 */
#define LSE_BDCTLR CH32RV_REG32(0x40021020u)
const Instance instances[] = {
    {0x40007C00u, CH32RV_CLKEN_LPTIM_ADDR, CH32RV_CLKEN_LPTIM_MASK, CH32RV_IRQN_LPTIM,
     CH32RV_EXTI_LPTIM_WAKEUP, LPT1_PADS},
};
#define LPT_HANDLER1 LPTIM_IRQHandler
#endif
constexpr uint8_t COUNT = sizeof(instances) / sizeof(instances[0]);

CH32RVLowPowerTimer *owners[COUNT];

/* The clock runs (LSI started here; LSE only if something started it). */
bool clock_on(CH32RVLowPowerTimerClock clock)
{
    switch (clock) {
    case CH32RVLowPowerTimerClock::Lsi:
        CH32RV_RCC_RSTSCKR |= 1u;   /* LSION */
        for (uint32_t spin = 0; !(CH32RV_RCC_RSTSCKR & 2u); spin++) {
            if (spin > 1000000u) {
                return false;
            }
        }
        return true;
    case CH32RVLowPowerTimerClock::Lse:
        return (LSE_BDCTLR & 2u) != 0;   /* LSERDY */
    case CH32RVLowPowerTimerClock::Hsi:
        CH32RV_RCC_CTLR |= 1u;    /* HSION */
        for (uint32_t spin = 0; !(CH32RV_RCC_CTLR & 2u); spin++) {
            if (spin > 1000000u) {
                return false;
            }
        }
        return true;
    default:
        return true;
    }
}

uint32_t clock_hz(CH32RVLowPowerTimerClock clock)
{
    switch (clock) {
    case CH32RVLowPowerTimerClock::Lsi:
        return CH32RV_LSI_HZ;
    case CH32RVLowPowerTimerClock::Lse:
        return 32768u;
    case CH32RVLowPowerTimerClock::Hsi:
        return CH32RV_HSI_HZ;
    default:
        return CH32RV_PCLK1_HZ;
    }
}

/* Wait for a write to reach the timer's clock, a few of its ticks; given up
 * after 5 ms (an asynchronous counter has no clock until its pin moves). */
void sync(uint32_t base, uint32_t flag)
{
    const uint32_t t0 = micros();
    while (!(ISR(base) & flag) && micros() - t0 < 5000u) {
    }
    ICR(base) = flag;
}

/* The timer off, CFGR set, on again. CLKMX_SEL[26:25] changes the timer's
 * clock only when it changes while the timer is on (H417, 2026-11-23: written
 * with the rest while off, or the same value again once on, the prescaler took
 * and the clock stayed PCLK1; 0 first and the clock after, both on, LSI / 128
 * counted 350 Hz) - EVT's LPTIM_TimeBaseInit writes CFGR after
 * LPTIM_Cmd(ENABLE), on a timer just reset. */
void enable_with(uint32_t base, uint32_t cfgr, uint32_t cr)
{
    CR(base) = 0;
    CFGR(base) = cfgr & ~CFGR_CLKSEL(3);
    /* a write right after the RCC reset was lost once (H417): until it reads back */
    for (uint8_t i = 0; i < 8 && !(CR(base) & CR_ENABLE); i++) {
        CR(base) = CR_ENABLE | cr;
    }
    delayMicroseconds(10);      /* the enable takes two of the timer's clocks (RM 17.3.10) */
    CFGR(base) = cfgr;
}

}   // namespace

uint8_t CH32RVLowPowerTimer::wakeLine() const
{
    return (_number >= 1 && _number <= COUNT) ? instances[_number - 1].wake : 0xFFu;
}

bool CH32RVLowPowerTimer::open(uint32_t cfgr)
{
    end();
    if (_number < 1 || _number > COUNT || owners[_number - 1]) {
        return false;
    }
    const Instance &t = instances[_number - 1];
    _index = (int8_t)(_number - 1);
    owners[_index] = this;
    ch32rv_clock_enable_at(t.clken_addr, t.clken_mask);
    CH32RV_REG32(t.clken_addr - 0x0Cu) |= t.clken_mask;   /* reset (PRSTR, 0x0C below) */
    CH32RV_REG32(t.clken_addr - 0x0Cu) &= ~t.clken_mask;
    (void)CR(t.base);           /* the reset's end before the first write */
    IER(t.base) = (_on_period ? F_ARRM : 0u) | (_on_compare ? F_CMPM : 0u);
    /* TIMOUT on: without it the timer's wake-up never reached its EXTI line
     * (H417, 2026-11-23: the flag polled with INTENR on, none in 2.5 s;
     * with it, one a period - EVT's LPTIM_LP_WakeUp sets it too). It only
     * changes what an external trigger does, and no trigger is used here. */
    enable_with(t.base, cfgr | CFGR_TIMOUT, _encoder ? CR_DIREXT : 0u);
    write_arr(_period - 1u);
    write_cmp(_compare);
    ICR(t.base) = 0x7Fu;
    ch32rv_irq_clear_pending(t.irqn);
    ch32rv_irq_enable(t.irqn);
    return true;
}

void CH32RVLowPowerTimer::end()
{
    if (_index < 0) {
        return;
    }
    const Instance &t = instances[_index];
    ch32rv_irq_disable(t.irqn);
    CR(t.base) = 0;
    IER(t.base) = 0;
    ch32rv_clock_disable_at(t.clken_addr, t.clken_mask);
    owners[_index] = nullptr;
    _index = -1;
    _route_set = false;
    _hz = 0;
    _encoder = false;
}


/* `pin` as the instance's pad in `role`: its function (per-pin AF, or the
 * route's AFIO value - one route for all the pads used), input or output. */
bool CH32RVLowPowerTimer::pad(int pin, uint8_t role)
{
    if (_index < 0 || pin < 0) {
        return false;
    }
    const Instance &t = instances[_index];
    for (uint8_t i = 0; i < t.pad_count; i++) {
        const Pad &p = t.pads[i];
        if (p.role != role || p.pad != (uint8_t)pin) {
            continue;
        }
#if !CH32RV_GPIO_PER_PIN_AF
        if (_route_set && (p.value != _route_value || p.value2 != _route_value2)) {
            continue;
        }
        if (!_route_set) {
            /* the field's bits: every value the table has, OR'd */
            uint32_t m1 = 0, m2 = 0;
            for (uint8_t k = 0; k < t.pad_count; k++) {
                m1 |= t.pads[k].value;
                m2 |= t.pads[k].value2;
            }
            ch32rv_clock_enable(AFIO);
            if (m1) {
                CH32RV_AFIO_PCFR1 = (CH32RV_AFIO_PCFR1 & ~m1) | p.value;
            }
#ifdef CH32RV_AFIO_PCFR2
            if (m2) {
                CH32RV_AFIO_PCFR2 = (CH32RV_AFIO_PCFR2 & ~m2) | p.value2;
            }
#endif
        }
#endif
        _route_set = true;
        _route_value = p.value;
        _route_value2 = p.value2;
        ch32rv_pin_set_af(p.pad, p.value);
        const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin);
        ch32rv_gpio_clock_enable(port);
        ch32rv_gpio_set_config(port, (uint8_t)CH32RV_PIN_BIT(pin),
                               role == 3 ? CH32RV_GPIO_CFG_AF_PP_50M : CH32RV_GPIO_CFG_IN_FLOAT);
        return true;
    }
    return false;
}

bool CH32RVLowPowerTimer::begin(CH32RVLowPowerTimerClock clock, uint8_t divider)
{
    uint8_t presc = 0;
    while (presc < 7 && (1u << presc) < divider) {
        presc++;
    }
    if ((1u << presc) != divider || !clock_on(clock)) {
        return false;
    }
    _encoder = false;
    if (!open(CFGR_CLKSEL((uint32_t)clock) | CFGR_PRESC(presc))) {
        return false;
    }
    _hz = clock_hz(clock) >> presc;
    return true;
}

bool CH32RVLowPowerTimer::beginCounter(int pin, PinStatus edge, bool asynchronous, CH32RVLowPowerTimerClock clock)
{
    const uint8_t pol = edge == FALLING ? 1u : edge == CHANGE ? 2u : 0u;
    if ((asynchronous && pol == 2u) || (!asynchronous && !clock_on(clock))) {
        return false;
    }
    _encoder = false;
    const uint32_t cfgr = asynchronous ? (CFGR_CKSEL | CFGR_CKPOL(pol))
                                       : (CFGR_COUNTMODE | CFGR_CKPOL(pol) | CFGR_CLKSEL((uint32_t)clock));
    if (!open(cfgr)) {
        return false;
    }
    if (!pad(pin, 0)) {
        end();
        return false;
    }
    _hz = 0;
    return start();
}

bool CH32RVLowPowerTimer::beginEncoder(int pinA, int pinB, CH32RVLowPowerTimerClock clock)
{
    if (!clock_on(clock)) {
        return false;
    }
    _encoder = true;
    if (!open(CFGR_ENC | CFGR_CKPOL(2) | CFGR_CLKSEL((uint32_t)clock))) {
        return false;
    }
    if (!pad(pinA, 0) || !pad(pinB, 1)) {
        end();
        return false;
    }
    _hz = 0;
    return start();
}

void CH32RVLowPowerTimer::write_arr(uint32_t v)
{
    const uint32_t b = instances[_index].base;
    ICR(b) = F_ARROK;
    ARR(b) = v;
    sync(b, F_ARROK);
}

void CH32RVLowPowerTimer::write_cmp(uint32_t v)
{
    const uint32_t b = instances[_index].base;
    ICR(b) = F_CMPOK;
    CMP(b) = v;
    sync(b, F_CMPOK);
}

bool CH32RVLowPowerTimer::setPeriod(uint32_t ticks)
{
    if (ticks < 1u || ticks > 65536u || (_compare_set && _compare >= ticks)) {
        return false;
    }
    _period = ticks;
    if (_index >= 0) {
        write_arr(ticks - 1u);
    }
    if (!_compare_set) {
        /* The compare at the period's end, with the reload: a compare of 0
         * matched as the count started, and the match wakes the chip whether
         * its interrupt is on or not - lightSleep() came straight back
         * (H417, 2026-11-23). */
        _compare = ticks - 1u;
        if (_index >= 0) {
            write_cmp(_compare);
        }
    }
    return true;
}

bool CH32RVLowPowerTimer::setCompare(uint32_t ticks)
{
    if (ticks >= _period) {
        return false;
    }
    _compare = ticks;
    _compare_set = true;
    if (_index >= 0) {
        write_cmp(ticks);
    }
    return true;
}

bool CH32RVLowPowerTimer::output(int pin, bool inverted)
{
    if (_index < 0 || !pad(pin, 3)) {
        return false;
    }
    const uint32_t b = instances[_index].base;
    /* WAVPOL takes effect at once (RM 17.3.7); the rest of CFGR as it is */
    const uint32_t cr = CR(b) & (CR_OUTEN | CR_DIREXT);
    enable_with(b, (CFGR(b) & ~CFGR_WAVPOL) | (inverted ? CFGR_WAVPOL : 0u), cr | CR_OUTEN);
    write_arr(_period - 1u);
    write_cmp(_compare);
    return true;
}

bool CH32RVLowPowerTimer::start()
{
    if (_index < 0) {
        return false;
    }
    const uint32_t b = instances[_index].base;
    CR(b) |= CR_CNTSTRT;
    return true;
}

bool CH32RVLowPowerTimer::startOnce()
{
    if (_index < 0) {
        return false;
    }
    const uint32_t b = instances[_index].base;
    CR(b) |= CR_SNGSTRT;
    return true;
}

void CH32RVLowPowerTimer::stop()
{
    if (_index < 0) {
        return;
    }
    const uint32_t b = instances[_index].base;
    /* off and on: the internal logic reset, the counter back to 0 */
    enable_with(b, CFGR(b), CR(b) & (CR_OUTEN | CR_DIREXT));
    write_arr(_period - 1u);
    write_cmp(_compare);
}

uint16_t CH32RVLowPowerTimer::count()
{
    if (_index < 0) {
        return 0;
    }
    /* Clocked apart from the bus: the same value twice in a row (RM 17.5.8). */
    const uint32_t b = instances[_index].base;
    uint32_t a = CNT(b) & 0xFFFFu;
    for (uint8_t i = 0; i < 8; i++) {
        const uint32_t c = CNT(b) & 0xFFFFu;
        if (c == a) {
            break;
        }
        a = c;
    }
    return (uint16_t)a;
}

int CH32RVLowPowerTimer::direction()
{
    if (_index < 0 || !_encoder) {
        return 0;
    }
    return (ISR(instances[_index].base) & F_DIRSYNC) ? 1 : -1;
}

void CH32RVLowPowerTimer::onPeriod(void (*callback)())
{
    _on_period = callback;
    if (_index >= 0) {
        const uint32_t b = instances[_index].base;
        IER(b) = (IER(b) & ~F_ARRM) | (callback ? F_ARRM : 0u);
    }
}

void CH32RVLowPowerTimer::onCompare(void (*callback)())
{
    _on_compare = callback;
    if (_index >= 0) {
        const uint32_t b = instances[_index].base;
        IER(b) = (IER(b) & ~F_CMPM) | (callback ? F_CMPM : 0u);
    }
}

void CH32RVLowPowerTimer::isr()
{
    if (_index < 0) {
        return;
    }
    const uint32_t b = instances[_index].base;
    const uint32_t f = ISR(b) & IER(b) & (F_CMPM | F_ARRM);
    ICR(b) = f;
    if ((f & F_CMPM) && _on_compare) {
        _on_compare();
    }
    if ((f & F_ARRM) && _on_period) {
        _on_period();
    }
}

namespace {
void serve(uint8_t i)
{
    if (owners[i]) {
        owners[i]->isr();
    } else {
        ICR(instances[i].base) = 0x7Fu;
    }
}
}   // namespace

extern "C" CH32RV_INTERRUPT void LPT_HANDLER1(void)
{
    serve(0);
}
#ifdef LPT_HANDLER2
extern "C" CH32RV_INTERRUPT void LPT_HANDLER2(void)
{
    serve(1);
}
#endif

#endif   // CH32RV_HAS_LPTIM

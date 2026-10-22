/* CH32RVLogicCapture.h. One timer's update request moves the port into RAM
 * through one DMA channel; the timer comes from the core's timer manager.
 * Which timer and which channel (ch32-device-data index/dma.csv: the fixed
 * request table of each family; on CH32H41x, DMAMUX request 122 = TIM6_UP):
 *
 *   CH32H415-H417               TIM6 -> DMA2 channel 8 through DMAMUX channel 16
 *   CH32V303/V305/V307/V317,
 *   CH32V407/V467               TIM6 -> DMA2 channel 3
 *   CH32V103, V203/V208, V205,
 *   CH32L103/M103, X305/X315    TIM4 -> DMA1 channel 7
 *   CH32V002-V007, M007, V003,
 *   CH32X033/X035               TIM2 -> DMA1 channel 2
 *   CH32M030                    TIM3 -> DMA1 channel 3
 *
 * A basic timer where there is one; elsewhere the general-purpose timer whose
 * request lands on a channel below 9 (the flags of channels 9 and up are in
 * another register on V30x's DMA2 and X3x5's DMA1). Measured on CH32H417
 * only; the other families are built, not yet run. */
#include "CH32RVLogicCapture.h"

#if CH32RV_LOGIC_CAPTURE

#include "ch32rv_registers.h"

#if CH32RV_LOGIC_DMAMUX
#define LC_TIMER 6u
#define LC_DMA   0x40020400u
#define LC_CH    8u
#define LC_IRQN  CH32RV_IRQN_DMA2_Channel8
#define LC_HANDLER DMA2_Channel8_IRQHandler
#define DMAMUX_C12_15 CH32RV_REG32(0x4002080Cu)   /* mux channels 13..16, a byte each */
#define MUX_SHIFT     24u                         /* channel 16 -> DMA2 channel 8 */
#define REQ_TIM6_UP   122u
#elif defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || \
    defined(CH32RV_SERIES_CH32V317) || defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
#define LC_TIMER 6u
#define LC_DMA   0x40020400u
#define LC_CH    3u
#define LC_IRQN  CH32RV_IRQN_DMA2_Channel3
#define LC_HANDLER DMA2_Channel3_IRQHandler
#elif defined(CH32RV_SERIES_CH32V103) || defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || \
    defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103) || \
    defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315)
#define LC_TIMER 4u
#define LC_DMA   0x40020000u
#define LC_CH    7u
#define LC_IRQN  CH32RV_IRQN_DMA1_Channel7
#define LC_HANDLER DMA1_Channel7_IRQHandler
#elif defined(CH32RV_SERIES_CH32M030)
#define LC_TIMER 3u
#define LC_DMA   0x40020000u
#define LC_CH    3u
#define LC_IRQN  CH32RV_IRQN_DMA1_Channel3
#define LC_HANDLER DMA1_Channel3_IRQHandler
#else   /* V002-V007, M007, V003, X033/X035 */
#define LC_TIMER 2u
#define LC_DMA   0x40020000u
#define LC_CH    2u
#define LC_IRQN  CH32RV_IRQN_DMA1_Channel2
#define LC_HANDLER DMA1_Channel2_IRQHandler
#endif

#define DMA_INTFR    CH32RV_REG32(LC_DMA + 0x00u)
#define DMA_INTFCR   CH32RV_REG32(LC_DMA + 0x04u)
#define CH           (LC_DMA + 0x08u + (LC_CH - 1u) * 0x14u)
#define CH_CFGR      CH32RV_REG32(CH + 0x00u)
#define CH_CNTR      CH32RV_REG32(CH + 0x04u)
#define CH_PADDR     CH32RV_REG32(CH + 0x08u)
#define CH_MADDR     CH32RV_REG32(CH + 0x0Cu)
#define CH_SHIFT     ((LC_CH - 1u) * 4u)

#define CH_TCIF (1u << (CH_SHIFT + 1u))
#define CH_HTIF (1u << (CH_SHIFT + 2u))
#define CH_ALL  (0xFu << CH_SHIFT)

#define CFGR_EN       (1u << 0)
#define CFGR_TCIE     (1u << 1)
#define CFGR_HTIE     (1u << 2)
#define CFGR_CIRC     (1u << 5)
#define CFGR_MINC     (1u << 7)
#define CFGR_16BIT    ((1u << 8) | (1u << 10))    /* PSIZE, MSIZE */
#define CFGR_PL_VHIGH (3u << 12)
#define CFGR_MEM2MEM  (1u << 14)

#define TIM_DMAINTENR 0x0Cu
#define TIM_UDE       (1u << 8)

/* The DMA wrote memory the compiler knows nothing about: keep it from
 * reusing what the CPU had there before (CH32RVHsAdc's read() returned a
 * stale marker for exactly this reason). */
#define DMA_DONE() __asm__ volatile("fence rw, rw" ::: "memory")

static CH32RVLogicCapture *active;

static void quiesce(void *context, uint8_t, uint8_t)
{
    static_cast<CH32RVLogicCapture *>(context)->stop();
}

bool CH32RVLogicCapture::begin(CH32RVLogicPort port, uint32_t sampleRate)
{
    static const uint32_t present[] = {CH32RV_PORT_MASK_A, CH32RV_PORT_MASK_B, CH32RV_PORT_MASK_C,
                                       CH32RV_PORT_MASK_D, CH32RV_PORT_MASK_E, CH32RV_PORT_MASK_F};
    if ((uint8_t)port > (uint8_t)PORT_F || !present[port] || sampleRate == 0) {
        return false;
    }
    end();
    if (sampleRate > maxContinuousRate()) {
        sampleRate = maxContinuousRate();
    }
    /* F_CPU / rate ticks a sample, split into PSC x ARR. */
    uint32_t ticks = (F_CPU + sampleRate / 2u) / sampleRate;
    if (ticks < 8u) {
        ticks = 8u;
    }
    uint32_t psc = 1;
    while (ticks / psc > 65536u) {
        psc++;
    }
    const uint32_t arr = ticks / psc;
    _rate = F_CPU / (psc * arr);
    const CH32RVTimerOwner me = {this, quiesce, this};   /* copied by the manager */
    const CH32RVTimerRequest request = {LC_TIMER, CH32RV_TIMER_WHOLE, {(uint16_t)(psc - 1u), arr - 1u, 0}};
    _lease = ch32TimerTryAcquire(&request, &me);
    if (!ch32TimerLeaseValid(&_lease) || !ch32TimerApplyBase(&_lease)) {
        return false;
    }
    _port = 0x40010808u + 0x400u * (uint32_t)port;     /* GPIOx_INDR, the same on every family */
    CH32RV_REG32(CH32RV_CLKEN_GPIO_ADDR) |= 1u << (CH32RV_CLKEN_GPIO_BIT0 + (uint32_t)port);
#if LC_DMA == 0x40020400u
    ch32rv_clock_enable(DMA2);
#else
    ch32rv_clock_enable(DMA1);
#endif
    _begun = true;
    active = this;
    return true;
}

void CH32RVLogicCapture::end()
{
    stop();
    if (ch32TimerLeaseValid(&_lease)) {
        ch32TimerRelease(&_lease);
    }
    _begun = false;
    if (active == this) {
        active = nullptr;
    }
}

bool CH32RVLogicCapture::startContinuous(uint16_t *buffer, uint32_t samples)
{
    if (!_begun || !buffer || samples < 2u || samples > 65534u || (samples & 1u)) {
        return false;
    }
    stop();
    _buf = buffer;
    _half = samples / 2u;
    _ready = 0;
    _overruns = _filled = 0;
#if CH32RV_LOGIC_DMAMUX
    DMAMUX_C12_15 = (DMAMUX_C12_15 & ~(0x7Fu << MUX_SHIFT)) | ((REQ_TIM6_UP - 1u) << MUX_SHIFT);
#endif
    DMA_INTFCR = CH_ALL;
    CH_PADDR = _port;
    CH_MADDR = (uint32_t)(uintptr_t)buffer;
    CH_CNTR = samples;
    CH_CFGR = CFGR_PL_VHIGH | CFGR_16BIT | CFGR_MINC | CFGR_CIRC | CFGR_HTIE | CFGR_TCIE;
    CH_CFGR |= CFGR_EN;
    ch32rv_irq_enable(LC_IRQN);
    const uint32_t tim = ch32TimerCapabilityFor(LC_TIMER)->register_base;
    CH32RV_REG32(tim + TIM_DMAINTENR) |= TIM_UDE;
    _running = true;
    ch32TimerStart(&_lease);
    return true;
}

void CH32RVLogicCapture::stop()
{
    if (!_running) {
        return;
    }
    if (ch32TimerLeaseValid(&_lease)) {
        ch32TimerStop(&_lease);
        CH32RV_REG32(ch32TimerCapabilityFor(LC_TIMER)->register_base + TIM_DMAINTENR) &= ~TIM_UDE;
    }
    ch32rv_irq_disable(LC_IRQN);
    CH_CFGR = 0;
    DMA_INTFCR = CH_ALL;
    _running = false;
}

const uint16_t *CH32RVLogicCapture::readBuffer()
{
    noInterrupts();
    const uint8_t r = _ready;
    uint8_t take = 0xFF;
    if (r) {
        take = (r == 3u) ? (uint8_t)(_last ^ 1u) : (uint8_t)(r == 1u ? 0u : 1u);
        _ready = (uint8_t)(r & ~(1u << take));
    }
    interrupts();
    DMA_DONE();
    return take == 0xFF ? nullptr : _buf + (take ? _half : 0u);
}

void CH32RVLogicCapture::isr()
{
    const uint32_t f = DMA_INTFR & (CH_HTIF | CH_TCIF);
    DMA_INTFCR = CH_ALL;
    /* Both at once: the handler came late by a whole half. */
    for (uint8_t k = 0; k < 2; k++) {
        const bool done = k == 0 ? (f & CH_HTIF) : (f & CH_TCIF);
        if (!done) {
            continue;
        }
        if (_ready & (1u << k)) {
            _overruns++;
        }
        _ready |= (uint8_t)(1u << k);
        _last = k;
        _filled++;
    }
}

uint32_t CH32RVLogicCapture::captureBurst(uint16_t *buffer, uint16_t samples)
{
    if (!_begun || !buffer || samples == 0) {
        return 0;
    }
    stop();
    DMA_INTFCR = CH_ALL;
    CH_PADDR = _port;
    CH_MADDR = (uint32_t)(uintptr_t)buffer;
    CH_CNTR = samples;
    CH_CFGR = CFGR_MEM2MEM | CFGR_PL_VHIGH | CFGR_16BIT | CFGR_MINC;
    noInterrupts();
    const uint32_t t0 = micros();
    CH_CFGR |= CFGR_EN;
    bool done = true;
    while (!(DMA_INTFR & CH_TCIF)) {
        if ((uint32_t)(micros() - t0) > (uint32_t)samples + 1000u) {   /* far slower than any DMA */
            done = false;
            break;
        }
    }
    const uint32_t us = micros() - t0;
    interrupts();
    CH_CFGR = 0;
    DMA_INTFCR = CH_ALL;
    DMA_DONE();
    return done && us ? (uint32_t)((uint64_t)samples * 1000000u / us) : 0;
}

extern "C" __attribute__((interrupt)) void LC_HANDLER(void)
{
    if (active) {
        active->isr();
    } else {
        DMA_INTFCR = CH_ALL;
    }
}

#endif

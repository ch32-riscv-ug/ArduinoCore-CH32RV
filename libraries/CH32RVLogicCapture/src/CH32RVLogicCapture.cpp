/* CH32RVLogicCapture.h. DMA2 channel 8 through DMAMUX channel 16
 * (CH32H417RM chapter 10: request 122 = TIM6_UP); TIM6 through the core's
 * timer manager. */
#include "CH32RVLogicCapture.h"

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)

#include "ch32rv_registers.h"

#define DMA2_INTFR    CH32RV_REG32(0x40020400u)
#define DMA2_INTFCR   CH32RV_REG32(0x40020404u)
#define CH8           (0x40020408u + 7u * 0x14u)
#define CH8_CFGR      CH32RV_REG32(CH8 + 0x00u)
#define CH8_CNTR      CH32RV_REG32(CH8 + 0x04u)
#define CH8_PADDR     CH32RV_REG32(CH8 + 0x08u)
#define CH8_MADDR     CH32RV_REG32(CH8 + 0x0Cu)
#define DMAMUX_C12_15 CH32RV_REG32(0x4002080Cu)   /* mux channels 13..16, a byte each */
#define MUX_SHIFT     24u                         /* channel 16 -> DMA2 channel 8 */
#define REQ_TIM6_UP   122u

#define CH8_GIF  (1u << 28)
#define CH8_TCIF (1u << 29)
#define CH8_HTIF (1u << 30)
#define CH8_ALL  (0xFu << 28)

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

static CH32RVLogicCapture *active;

static void quiesce(void *context, uint8_t, uint8_t)
{
    static_cast<CH32RVLogicCapture *>(context)->stop();
}

bool CH32RVLogicCapture::begin(CH32RVLogicPort port, uint32_t sampleRate)
{
    if ((uint8_t)port > (uint8_t)PORT_F || sampleRate == 0) {
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
    const CH32RVTimerRequest request = {6, CH32RV_TIMER_WHOLE, {(uint16_t)(psc - 1u), arr - 1u, 0}};
    _lease = ch32TimerTryAcquire(&request, &me);
    if (!ch32TimerLeaseValid(&_lease) || !ch32TimerApplyBase(&_lease)) {
        return false;
    }
    _port = 0x40010808u + 0x400u * (uint32_t)port;     /* GPIOx_INDR */
    ch32rv_clock_enable(DMA2);
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
    DMAMUX_C12_15 = (DMAMUX_C12_15 & ~(0x7Fu << MUX_SHIFT)) | ((REQ_TIM6_UP - 1u) << MUX_SHIFT);
    DMA2_INTFCR = CH8_ALL;
    CH8_PADDR = _port;
    CH8_MADDR = (uint32_t)(uintptr_t)buffer;
    CH8_CNTR = samples;
    CH8_CFGR = CFGR_PL_VHIGH | CFGR_16BIT | CFGR_MINC | CFGR_CIRC | CFGR_HTIE | CFGR_TCIE;
    CH8_CFGR |= CFGR_EN;
    ch32rv_irq_enable(CH32RV_IRQN_DMA2_Channel8);
    const uint32_t tim = ch32TimerCapabilityFor(6)->register_base;
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
        CH32RV_REG32(ch32TimerCapabilityFor(6)->register_base + TIM_DMAINTENR) &= ~TIM_UDE;
    }
    ch32rv_irq_disable(CH32RV_IRQN_DMA2_Channel8);
    CH8_CFGR = 0;
    DMA2_INTFCR = CH8_ALL;
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
    return take == 0xFF ? nullptr : _buf + (take ? _half : 0u);
}

void CH32RVLogicCapture::isr()
{
    const uint32_t f = DMA2_INTFR & (CH8_HTIF | CH8_TCIF);
    DMA2_INTFCR = CH8_ALL;
    /* Both at once: the handler came late by a whole half. */
    for (uint8_t k = 0; k < 2; k++) {
        const bool done = k == 0 ? (f & CH8_HTIF) : (f & CH8_TCIF);
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
    DMA2_INTFCR = CH8_ALL;
    CH8_PADDR = _port;
    CH8_MADDR = (uint32_t)(uintptr_t)buffer;
    CH8_CNTR = samples;
    CH8_CFGR = CFGR_MEM2MEM | CFGR_PL_VHIGH | CFGR_16BIT | CFGR_MINC;
    noInterrupts();
    const uint32_t t0 = micros();
    CH8_CFGR |= CFGR_EN;
    bool done = true;
    while (!(DMA2_INTFR & CH8_TCIF)) {
        if ((uint32_t)(micros() - t0) > (uint32_t)samples + 1000u) {   /* far slower than any DMA */
            done = false;
            break;
        }
    }
    const uint32_t us = micros() - t0;
    interrupts();
    CH8_CFGR = 0;
    DMA2_INTFCR = CH8_ALL;
    return done && us ? (uint32_t)((uint64_t)samples * 1000000u / us) : 0;
}

extern "C" __attribute__((interrupt)) void DMA2_Channel8_IRQHandler(void)
{
    if (active) {
        active->isr();
    } else {
        DMA2_INTFCR = CH8_ALL;
    }
}

#endif

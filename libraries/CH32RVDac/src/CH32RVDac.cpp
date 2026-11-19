#include "CH32RVDac.h"

#include "CH32RVTimer.h"
#include "internal/ch32rv_dma.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

namespace {

#if defined(CH32RV_DAC1_PIN) || defined(CH32RV_DAC2_PIN)
#define CH32RV_DAC_READY 1

/* DAC_CTLR, channel 1's bits (channel 2's are 16 up): the reference manuals'
 * (device-data registers.csv's DAC CTLR). */
constexpr uint32_t EN = 1u << 0, TEN = 1u << 2, TSEL_SHIFT = 3, WAVE_SHIFT = 6, MAMP_SHIFT = 8, DMAEN = 1u << 12;
constexpr uint32_t WAVE_NOISE = 1u, WAVE_TRIANGLE = 2u;
constexpr uint32_t CHANNEL_BITS = 0xFFFFu;
#define TIM_CTLR2(b) CH32RV_REG16((b) + 0x04u)
constexpr uint16_t MMS_UPDATE = 2u << 4;     /* TRGO: the update event */
/* DMA: 16-bit on both sides (a half word) */
constexpr uint32_t PSIZE_16 = 1u << 8, MSIZE_16 = 1u << 10;

int pinOf(uint8_t channel)
{
#if defined(CH32RV_DAC1_PIN)
    if (channel == 1) {
        return CH32RV_DAC1_PIN;
    }
#endif
#if defined(CH32RV_DAC2_PIN)
    if (channel == 2) {
        return CH32RV_DAC2_PIN;
    }
#endif
    return -1;
}

inline uint32_t shiftOf(uint8_t channel) { return channel == 2 ? 16u : 0u; }

volatile uint32_t &holding(uint8_t channel)
{
    return channel == 2 ? CH32RV_DAC_R12BDHR2 : CH32RV_DAC_R12BDHR1;
}

/* The channel's control bits, the rest (the other channel's) kept. */
void setControl(uint8_t channel, uint32_t bits)
{
    const uint32_t s = shiftOf(channel);
    CH32RV_DAC_CTLR = (CH32RV_DAC_CTLR & ~(CHANNEL_BITS << s)) | (bits << s);
}

const ch32rv_dma_line_t *dmaOf(uint8_t channel)
{
#if defined(CH32RV_DAC1_DMA)
    static const ch32rv_dma_line_t dma1 = CH32RV_DAC1_DMA;
    if (channel == 1) {
        return &dma1;
    }
#endif
#if defined(CH32RV_DAC2_DMA)
    static const ch32rv_dma_line_t dma2 = CH32RV_DAC2_DMA;
    if (channel == 2) {
        return &dma2;
    }
#endif
    (void)channel;
    return nullptr;
}

#if defined(CH32RV_DAC_TRIGGERS)
struct Trigger {
    uint8_t timer, tsel;
};
const Trigger triggers[] = CH32RV_DAC_TRIGGERS;
/* The basic timers first: they do nothing else. */
const uint8_t preference[] = {6, 7, 8, 5, 2, 4};
#endif

CH32RVTimerLease leases[2];
const CH32RVTimerOwner owners[2] = {{&owners[0], nullptr, nullptr}, {&owners[1], nullptr, nullptr}};
#endif

}  // namespace

bool CH32RVDac::begin()
{
#if defined(CH32RV_DAC_READY)
    const int pin = pinOf(_channel);
    if (pin < 0) {
        return false;
    }
    end();
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin), bit = (uint8_t)CH32RV_PIN_BIT(pin);
    ch32rv_gpio_clock_enable(port);
    ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_IN_ANALOG);
    ch32rv_clock_enable(DAC);
    holding(_channel) = 0;
    setControl(_channel, EN);
    _begun = true;
    return true;
#else
    return false;
#endif
}

void CH32RVDac::end()
{
#if defined(CH32RV_DAC_READY)
    if (!_begun) {
        return;
    }
    stop();
    setControl(_channel, 0);
    _begun = false;
#endif
}

bool CH32RVDac::write(uint16_t value)
{
#if defined(CH32RV_DAC_READY)
    if (!_begun || value > 4095u) {
        return false;
    }
    stop();
    holding(_channel) = value;
    return true;
#else
    (void)value;
    return false;
#endif
}

bool CH32RVDac::startTimer(uint32_t hz, uint8_t &tsel)
{
#if defined(CH32RV_DAC_READY) && defined(CH32RV_DAC_TRIGGERS)
    if (!hz) {
        return false;
    }
    CH32RVTimerLease &lease = leases[_channel - 1u];
    for (uint8_t want : preference) {
        for (const Trigger &t : triggers) {
            if (t.timer != want) {
                continue;
            }
            const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(t.timer);
            if (!cap) {
                continue;
            }
            /* prescaler x period = the timer's clock / hz, each within 1 .. 2^16 */
            const uint32_t clock = ch32rv_timer_clock_hz(cap->number);
            const uint64_t ticks = ((uint64_t)clock + hz / 2u) / hz;
            const uint64_t most = cap->counter_bits == 32u ? 0x100000000ull : 0x10000ull;
            if (ticks < 1u) {
                continue;
            }
            const uint64_t psc = (ticks + most - 1u) / most;
            if (psc > 0x10000u) {
                continue;
            }
            const uint32_t period = (uint32_t)((ticks + psc / 2u) / psc);
            const CH32RVTimerRequest request = {
                cap->number, CH32RV_TIMER_WHOLE, {(uint16_t)(psc - 1u), period - 1u, 0u}
            };
            lease = ch32rv_timer_try_acquire(&request, &owners[_channel - 1u]);
            if (!ch32rv_timer_lease_valid(&lease)) {
                continue;
            }
            ch32rv_timer_apply_base(&lease);
            TIM_CTLR2(cap->register_base) = MMS_UPDATE;
            _timer = cap->number;
            _rate = (uint32_t)(((uint64_t)clock + (psc * period) / 2u) / (psc * period));
            tsel = t.tsel;
            return true;
        }
    }
#else
    (void)hz;
    (void)tsel;
#endif
    return false;
}

void CH32RVDac::stopTimer()
{
#if defined(CH32RV_DAC_READY)
    if (_timer) {
        CH32RVTimerLease &lease = leases[_channel - 1u];
        ch32rv_timer_stop(&lease);
        ch32rv_timer_release(&lease);
        _timer = 0;
        _rate = 0;
    }
#endif
}

void CH32RVDac::stop()
{
#if defined(CH32RV_DAC_READY)
    if (!_begun) {
        return;
    }
    stopTimer();
    if (_dma) {
        const ch32rv_dma_line_t *d = dmaOf(_channel);
        ch32rv_dma_close(d);
        ch32rv_dma_release(d->dma, d->channel);
        _dma = false;
    }
    /* The wave's value stays: the converter's output register holds it until
     * the next write. */
    setControl(_channel, EN);
#endif
}

bool CH32RVDac::triangle(uint8_t bits, uint32_t stepHz, uint16_t base)
{
#if defined(CH32RV_DAC_READY)
    if (!_begun || bits < 1u || bits > 12u || base + ((1u << bits) - 1u) > 4095u) {
        return false;
    }
    stop();
    uint8_t tsel;
    if (!startTimer(stepHz, tsel)) {
        return false;
    }
    holding(_channel) = base;
    setControl(_channel, EN | TEN | ((uint32_t)tsel << TSEL_SHIFT) | (WAVE_TRIANGLE << WAVE_SHIFT) |
                             ((uint32_t)(bits - 1u) << MAMP_SHIFT));
    ch32rv_timer_start(&leases[_channel - 1u]);
    return true;
#else
    (void)bits;
    (void)stepHz;
    (void)base;
    return false;
#endif
}

bool CH32RVDac::noise(uint8_t bits, uint32_t stepHz, uint16_t base)
{
#if defined(CH32RV_DAC_READY)
    if (!_begun || bits < 1u || bits > 12u || base + ((1u << bits) - 1u) > 4095u) {
        return false;
    }
    stop();
    uint8_t tsel;
    if (!startTimer(stepHz, tsel)) {
        return false;
    }
    holding(_channel) = base;
    setControl(_channel, EN | TEN | ((uint32_t)tsel << TSEL_SHIFT) | (WAVE_NOISE << WAVE_SHIFT) |
                             ((uint32_t)(bits - 1u) << MAMP_SHIFT));
    ch32rv_timer_start(&leases[_channel - 1u]);
    return true;
#else
    (void)bits;
    (void)stepHz;
    (void)base;
    return false;
#endif
}

bool CH32RVDac::play(const uint16_t *samples, uint16_t count, uint32_t sampleHz)
{
#if defined(CH32RV_DAC_READY)
    const ch32rv_dma_line_t *d = dmaOf(_channel);
    if (!_begun || !d || !samples || !count) {
        return false;
    }
    stop();
    if (!ch32rv_dma_claim(d->dma, d->channel)) {
        return false;
    }
    uint8_t tsel;
    if (!startTimer(sampleHz, tsel)) {
        ch32rv_dma_release(d->dma, d->channel);
        return false;
    }
    _dma = true;
    ch32rv_dma_open(d);
    CH32RV_DMA_PADDR(d) = (uint32_t)(uintptr_t)&holding(_channel);
    CH32RV_DMA_MADDR(d) = (uint32_t)(uintptr_t)samples;
    CH32RV_DMA_CNTR(d) = count;
    CH32RV_DMA_CFGR(d) = CH32RV_DMA_CFGR_DIR | CH32RV_DMA_CFGR_CIRC | CH32RV_DMA_CFGR_MINC | PSIZE_16 | MSIZE_16 |
                         CH32RV_DMA_CFGR_PL(2) | CH32RV_DMA_CFGR_EN;
    holding(_channel) = samples[0];
    setControl(_channel, EN | TEN | ((uint32_t)tsel << TSEL_SHIFT) | DMAEN);
    ch32rv_timer_start(&leases[_channel - 1u]);
    return true;
#else
    (void)samples;
    (void)count;
    (void)sampleHz;
    return false;
#endif
}
